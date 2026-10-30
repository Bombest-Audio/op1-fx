#include "drumsampler_core.h"

#include <math.h>
#include <string.h>

/* The control rate. The bend and the output level are recomputed every CTRL samples; the level is ramped
 * linearly between ticks, so nothing steps. */
#define CTRL 32

/* Voice states. V_DECAY covers decay and sustain. V_FAST is the quick fade used when a voice is stolen or its slot is
 * retriggered. V_TAIL plays back a fade-out that was rendered ahead of time (see ds_set_sample). */
enum { V_IDLE = 0, V_ATTACK = 1, V_DECAY = 2, V_RELEASE = 3, V_FAST = 4, V_TAIL = 5 };

#define FAST_FADE_SECONDS 0.002f     /* time to fall 60 dB when a voice is stolen or retriggered */
#define END_TAPER_SECONDS 0.001f     /* a sample that plays to its end fades over this long, so it never ends on a step */
#define LOOP_XFADE_SECONDS 0.010f    /* loop seam crossfade, at most a quarter of the loop */
#define KILL_LEVEL 1e-4f             /* an envelope below this (-80 dB) is treated as finished */

/* Output stage: the voice sum is scaled by the level control, then limited by a soft ceiling that is linear up to
 * CEIL_T and approaches CEIL_MAX, so the output cannot reach 0 dBFS however many voices are summed. */
#define CEIL_T 0.8f
#define CEIL_MAX 0.99f

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float soft_ceiling(float x)
{
    const float a = fabsf(x);
    if (a <= CEIL_T)
        return x;
    const float k = CEIL_MAX - CEIL_T;
    float z = (a - CEIL_T) / k;
    /* Pade tanh, input clamped to the range where it is accurate. It reaches 1.0 at z = 3. */
    z = z > 3.0f ? 3.0f : z;
    const float z2 = z * z;
    const float t = z * (27.0f + z2) / (27.0f + 9.0f * z2);
    const float y = CEIL_T + k * t;
    return x < 0.0f ? -y : y;
}

float ds_level_gain(float norm)
{
    const float n = clampf(norm, 0.0f, 1.0f);
    return n * n;
}

float ds_pitch_value(float norm)
{
    return (clampf(norm, 0.0f, 1.0f) - 0.5f) * 48.0f;
}

float ds_pitch_norm(float semitones)
{
    return clampf(semitones / 48.0f + 0.5f, 0.0f, 1.0f);
}

void ds_default_params(ds_params *p)
{
    p->attack = DS_DEFAULT_ATTACK;
    p->decay = DS_DEFAULT_DECAY;
    p->sustain = DS_DEFAULT_SUSTAIN;
    p->release = DS_DEFAULT_RELEASE;
    p->level = DS_DEFAULT_LEVEL;
    p->bend_range = DS_DEFAULT_BEND_RANGE;
}

void ds_default_slot(ds_slot_params *p)
{
    p->pitch = DS_DEFAULT_PITCH;
    p->in = DS_DEFAULT_IN;
    p->out = DS_DEFAULT_OUT;
    p->mode = DS_DEFAULT_MODE;
    p->reverse = 0;
    p->gain = DS_DEFAULT_GAIN;
}

static void update_rates(ds_t *c)
{
    const ds_params *t = &c->target;
    c->att_inc = 1.0f / (t->attack * c->sr);
    /* exp(-6.9) is -60 dB: the time constants are set so that decay and release are times to fall 60 dB. */
    c->dec_c = expf(-6.9f / (t->decay * c->sr));
    c->fast_c = expf(-6.9f / (FAST_FADE_SECONDS * c->sr));
}

void ds_set_params(ds_t *c, const ds_params *in)
{
    ds_params *t = &c->target;
    t->attack = clampf(in->attack, 0.0005f, 10.0f);
    t->decay = clampf(in->decay, 0.005f, 30.0f);
    t->sustain = clampf(in->sustain, 0.0f, 1.0f);
    t->release = clampf(in->release, 0.005f, 30.0f);
    t->level = clampf(in->level, 0.0f, 1.0f);
    t->bend_range = clampf(in->bend_range, 0.0f, 24.0f);
    update_rates(c);
}

void ds_set_slot_params(ds_t *c, int slot, const ds_slot_params *in)
{
    if (slot < 0 || slot >= DS_SLOTS)
        return;
    ds_slot_params *s = &c->slot_params[slot];
    s->pitch = clampf(in->pitch, -24.0f, 24.0f);
    s->in = clampf(in->in, 0.0f, 1.0f);
    s->out = clampf(in->out, 0.0f, 1.0f);
    s->mode = in->mode < DS_MODE_GATE ? DS_MODE_GATE : (in->mode > DS_MODE_LOOP ? DS_MODE_LOOP : in->mode);
    s->reverse = in->reverse != 0;
    s->gain = clampf(in->gain, 0.0f, 2.0f);
}

void ds_reset(ds_t *c)
{
    memset(c->voice, 0, sizeof c->voice);
    c->pedal = 0;
    c->serial = 0;
    c->bend_s = c->bend_norm;   /* the wheel keeps its position across a reset */
    c->bend_mul = pow(2.0, (double)(c->bend_s * c->target.bend_range) / 12.0);
    c->s_level = ds_level_gain(c->target.level);
    c->level_cur = c->s_level;
    c->level_d = 0.0f;
    c->tick_left = 0;
    c->act_peak = 0.0f;
    c->act_notes = 0;
    c->act_hits = 0;
    c->act_last_slot = -1;
}

void ds_init(ds_t *c, float sample_rate)
{
    memset(c, 0, sizeof *c);
    c->sr = sample_rate > 8000.0f ? sample_rate : 44100.0f;
    ds_default_params(&c->target);
    for (int i = 0; i < DS_SLOTS; i++)
        ds_default_slot(&c->slot_params[i]);
    update_rates(c);
    ds_reset(c);
}

void ds_set_sample_rate(ds_t *c, float sample_rate)
{
    c->sr = sample_rate > 8000.0f ? sample_rate : 44100.0f;
    update_rates(c);
    ds_reset(c);
}

/* Four-point Catmull-Rom interpolation. Indexes outside the sample repeat its first and last frame. */
static inline float tap(const float *x, int frames, int i)
{
    return x[i < 0 ? 0 : (i >= frames ? frames - 1 : i)];
}

static inline float hermite(const float *x, int frames, double p)
{
    const int i = (int)floor(p);
    const float f = (float)(p - (double)i);
    const float y0 = tap(x, frames, i - 1), y1 = tap(x, frames, i), y2 = tap(x, frames, i + 1), y3 = tap(x, frames, i + 2);
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

/* Position in the source for t source frames into the played region. */
static inline double source_pos(const ds_voice *v, double t)
{
    return v->reverse ? (v->b - 1.0 - t) : (v->a + t);
}

/* Add n frames of one voice into l and r. */
static void voice_run(ds_t *c, ds_voice *v, float *l, float *r, int n)
{
    if (v->state == V_TAIL) {
        for (int i = 0; i < n && v->tail_i < v->tail_n; i++, v->tail_i++) {
            l[i] += v->tail_l[v->tail_i];
            r[i] += v->tail_r[v->tail_i];
        }
        if (v->tail_i >= v->tail_n)
            v->state = V_IDLE;
        return;
    }

    const float sus = c->target.sustain;
    const int taper = (int)(END_TAPER_SECONDS * c->sr) > 8 ? (int)(END_TAPER_SECONDS * c->sr) : 8;
    const double bm = c->bend_mul;
    const int stereo = v->r != v->l;

    for (int i = 0; i < n; i++) {
        switch (v->state) {
        case V_ATTACK:
            v->env += v->att_inc;
            if (v->env >= 1.0f) {
                v->env = 1.0f;
                v->state = V_DECAY;
            }
            break;
        case V_DECAY:
            v->env = sus + (v->env - sus) * c->dec_c;
            if (sus < KILL_LEVEL && v->env < KILL_LEVEL)
                v->state = V_IDLE;
            break;
        case V_RELEASE:
            v->env *= v->rel_c;
            if (v->env < KILL_LEVEL)
                v->state = V_IDLE;
            break;
        case V_FAST:
            v->env *= c->fast_c;
            if (v->env < KILL_LEVEL)
                v->state = V_IDLE;
            break;
        default:
            break;
        }
        if (v->state == V_IDLE)
            return;

        const double inc = v->base_inc * bm;
        if (v->loop) {
            while (v->t >= v->len)
                v->t -= v->period;
        } else if (v->t >= v->len) {
            v->state = V_IDLE;
            return;
        }

        const double p1 = source_pos(v, v->t);
        float sl = hermite(v->l, v->frames, p1);
        float sr_ = stereo ? hermite(v->r, v->frames, p1) : sl;

        float w = 1.0f;
        if (v->loop) {
            /* In the last xf frames of the loop, fade to the material one period earlier. Wrapping then lands on
             * the sample that the crossfade has just reached, so the seam has no step. */
            if (v->xf > 0.0 && v->t > v->len - v->xf) {
                const float x = (float)((v->t - (v->len - v->xf)) / v->xf);
                const double p2 = source_pos(v, v->t - v->period);
                const float l2 = hermite(v->l, v->frames, p2);
                const float r2 = stereo ? hermite(v->r, v->frames, p2) : l2;
                sl += x * (l2 - sl);
                sr_ += x * (r2 - sr_);
            }
        } else {
            const double rem = (v->len - v->t) / inc;   /* output frames left before the region ends */
            if (rem < (double)taper) {
                const float x = (float)(rem / (double)taper);
                w = x * x * (3.0f - 2.0f * x);
            }
        }

        const float g = v->env * v->gain * w;
        l[i] += sl * g;
        r[i] += sr_ * g;
        v->t += inc;
    }
}

static int is_playing(const ds_voice *v)
{
    return v->state == V_ATTACK || v->state == V_DECAY || v->state == V_RELEASE;
}

static void start_release(ds_t *c, ds_voice *v)
{
    if (!is_playing(v) || v->state == V_RELEASE)
        return;
    v->state = V_RELEASE;
    v->rel_c = expf(-6.9f / (c->target.release * c->sr));
}

static void start_fast(ds_voice *v)
{
    if (v->state == V_ATTACK || v->state == V_DECAY || v->state == V_RELEASE)
        v->state = V_FAST;
}

/* A voice about to lose its audio: render its fade-out now, into its own buffer. */
static void make_tail(ds_t *c, ds_voice *v)
{
    int n = (int)(0.0028f * c->sr) + 2;
    if (n > DS_TAIL)
        n = DS_TAIL;
    memset(v->tail_l, 0, (size_t)n * sizeof(float));
    memset(v->tail_r, 0, (size_t)n * sizeof(float));
    start_fast(v);
    voice_run(c, v, v->tail_l, v->tail_r, n);
    v->tail_n = n;
    v->tail_i = 0;
    v->l = v->r = NULL;
    v->state = V_TAIL;
}

void ds_set_sample(ds_t *c, int slot, const float *l, const float *r, int frames, float rate)
{
    if (slot < 0 || slot >= DS_SLOTS)
        return;
    ds_sample *s = &c->sample[slot];
    if (!l || frames <= 0) {
        l = r = NULL;
        frames = 0;
    }
    if (!r)
        r = l;
    if (s->l == l && s->r == r && s->frames == frames && s->rate == rate)
        return;
    for (int i = 0; i < DS_VOICES; i++) {
        ds_voice *v = &c->voice[i];
        if (v->slot == slot && (is_playing(v) || v->state == V_FAST))
            make_tail(c, v);
    }
    s->l = l;
    s->r = r;
    s->frames = frames;
    s->rate = rate > 1000.0f ? rate : 44100.0f;
}

static int playing_count(const ds_t *c)
{
    int n = 0;
    for (int i = 0; i < DS_VOICES; i++)
        n += is_playing(&c->voice[i]);
    return n;
}

static ds_voice *alloc_voice(ds_t *c)
{
    for (int i = 0; i < DS_VOICES; i++)
        if (c->voice[i].state == V_IDLE)
            return &c->voice[i];
    /* Every struct is busy fading. Reuse the one with the least left to say; this is the only hard cut and it needs
     * more than eight steals inside a couple of milliseconds. */
    ds_voice *best = &c->voice[0];
    float best_e = 1e9f;
    for (int i = 0; i < DS_VOICES; i++) {
        const ds_voice *v = &c->voice[i];
        const float e = v->state == V_TAIL ? (float)(v->tail_n - v->tail_i) * 1e-6f : v->env;
        if (e < best_e) {
            best_e = e;
            best = &c->voice[i];
        }
    }
    return best;
}

void ds_note_on(ds_t *c, int note, int velocity)
{
    if (velocity <= 0) {
        ds_note_off(c, note);
        return;
    }
    const int slot = note - DS_BASE_NOTE;
    if (slot < 0 || slot >= DS_SLOTS)
        return;
    const ds_sample *smp = &c->sample[slot];
    if (!smp->l || smp->frames <= 0)
        return;

    /* A slot is monophonic: striking it again fades the old hit out quickly and starts the new one. */
    for (int i = 0; i < DS_VOICES; i++)
        if (c->voice[i].slot == slot)
            start_fast(&c->voice[i]);

    if (playing_count(c) >= DS_MAX_PLAYING) {
        /* Steal the quietest voice that is already releasing, else the oldest one. */
        ds_voice *victim = NULL;
        for (int i = 0; i < DS_VOICES; i++) {
            ds_voice *v = &c->voice[i];
            if (v->state == V_RELEASE && (!victim || v->env < victim->env))
                victim = v;
        }
        if (!victim)
            for (int i = 0; i < DS_VOICES; i++) {
                ds_voice *v = &c->voice[i];
                if (is_playing(v) && (!victim || v->age < victim->age))
                    victim = v;
            }
        if (victim)
            start_fast(victim);
    }

    ds_voice *v = alloc_voice(c);
    const ds_slot_params *sp = &c->slot_params[slot];
    memset(v, 0, sizeof *v);
    v->slot = slot;
    v->note = note;
    v->held = 1;
    v->age = ++c->serial;
    v->l = smp->l;
    v->r = smp->r;
    v->frames = smp->frames;
    v->reverse = sp->reverse;
    v->loop = sp->mode == DS_MODE_LOOP;
    v->oneshot = sp->mode == DS_MODE_ONESHOT;

    float lo = sp->in, hi = sp->out;
    if (lo > hi) {
        const float tmp = lo;
        lo = hi;
        hi = tmp;
    }
    v->a = (double)lo * (double)smp->frames;
    v->b = (double)hi * (double)smp->frames;
    if (v->b - v->a < 1.0) {
        /* A region shorter than one frame still plays that frame. */
        if (v->a > (double)(smp->frames - 1))
            v->a = (double)(smp->frames - 1);
        v->b = v->a + 1.0;
    }
    v->len = v->b - v->a;
    if (v->loop) {
        v->xf = (double)LOOP_XFADE_SECONDS * (double)smp->rate;
        if (v->xf > 0.25 * v->len)
            v->xf = 0.25 * v->len;
        if (v->len - v->xf < 1.0)
            v->xf = 0.0;
        v->period = v->len - v->xf;
    }
    v->base_inc = ((double)smp->rate / (double)c->sr) * pow(2.0, (double)sp->pitch / 12.0);
    v->gain = (0.25f + 0.75f * ((float)velocity / 127.0f)) * sp->gain;
    v->att_inc = c->att_inc;
    v->env = 0.0f;
    v->state = V_ATTACK;

    c->act_notes++;
    c->act_hits |= 1u << slot;
    c->act_last_slot = slot;
}

void ds_note_off(ds_t *c, int note)
{
    for (int i = 0; i < DS_VOICES; i++) {
        ds_voice *v = &c->voice[i];
        if (!is_playing(v) || v->note != note || !v->held)
            continue;
        v->held = 0;
        if (v->oneshot)
            continue;   /* a one-shot plays to its end whatever the key does */
        if (c->pedal)
            v->sustained = 1;
        else
            start_release(c, v);
    }
}

void ds_pitch_bend(ds_t *c, float norm)
{
    c->bend_norm = clampf(norm, -1.0f, 1.0f);
}

void ds_sustain_pedal(ds_t *c, int down)
{
    down = down != 0;
    if (!down && c->pedal) {
        for (int i = 0; i < DS_VOICES; i++) {
            ds_voice *v = &c->voice[i];
            if (v->sustained) {
                v->sustained = 0;
                start_release(c, v);
            }
        }
    }
    c->pedal = down;
}

void ds_all_off(ds_t *c)
{
    c->pedal = 0;
    for (int i = 0; i < DS_VOICES; i++) {
        ds_voice *v = &c->voice[i];
        v->held = 0;
        v->sustained = 0;
        start_release(c, v);
    }
}

static void control_tick(ds_t *c)
{
    const float k = 1.0f - expf(-(float)CTRL / (0.005f * c->sr));
    c->bend_s += k * (c->bend_norm - c->bend_s);
    if (fabsf(c->bend_s - c->bend_norm) < 1e-6f)
        c->bend_s = c->bend_norm;
    c->bend_mul = pow(2.0, (double)(c->bend_s * c->target.bend_range) / 12.0);

    c->s_level += k * (ds_level_gain(c->target.level) - c->s_level);
    c->level_d = (c->s_level - c->level_cur) / (float)CTRL;
}

void ds_render(ds_t *c, float *l, float *r, int frames)
{
    memset(l, 0, (size_t)frames * sizeof(float));
    memset(r, 0, (size_t)frames * sizeof(float));
    int done = 0;
    while (done < frames) {
        if (c->tick_left <= 0) {
            control_tick(c);
            c->tick_left = CTRL;
        }
        const int n = frames - done < c->tick_left ? frames - done : c->tick_left;
        float *pl = l + done, *pr = r + done;
        for (int i = 0; i < DS_VOICES; i++)
            if (c->voice[i].state != V_IDLE)
                voice_run(c, &c->voice[i], pl, pr, n);
        for (int i = 0; i < n; i++) {
            c->level_cur += c->level_d;
            const float a = soft_ceiling(pl[i] * c->level_cur);
            const float b = soft_ceiling(pr[i] * c->level_cur);
            pl[i] = a;
            pr[i] = b;
            const float m = fabsf(a) > fabsf(b) ? fabsf(a) : fabsf(b);
            if (m > c->act_peak)
                c->act_peak = m;
        }
        c->tick_left -= n;
        done += n;
    }
}

int ds_active_voices(const ds_t *c)
{
    int n = 0;
    for (int i = 0; i < DS_VOICES; i++)
        n += c->voice[i].state != V_IDLE;
    return n;
}

void ds_take_activity(ds_t *c, ds_activity *out)
{
    out->peak = c->act_peak;
    out->voices = ds_active_voices(c);
    out->notes = c->act_notes;
    out->hits = c->act_hits;
    out->last_slot = c->act_last_slot;
    out->playing = 0;
    for (int s = 0; s < DS_SLOTS; s++)
        out->head[s] = -1.0f;
    unsigned newest[DS_SLOTS];
    memset(newest, 0, sizeof newest);
    for (int i = 0; i < DS_VOICES; i++) {
        const ds_voice *v = &c->voice[i];
        if (!is_playing(v))
            continue;
        out->playing |= 1u << v->slot;
        if (v->age >= newest[v->slot] && v->frames > 0) {
            newest[v->slot] = v->age;
            const double p = source_pos(v, v->t) / (double)v->frames;
            out->head[v->slot] = (float)(p < 0.0 ? 0.0 : (p > 1.0 ? 1.0 : p));
        }
    }
    c->act_peak = 0.0f;
    c->act_hits = 0;
}
