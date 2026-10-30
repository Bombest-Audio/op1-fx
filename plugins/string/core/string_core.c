#include "string_core.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f

/* Controls are recomputed every OPST_CTRL samples (at any sample rate). Block size never changes the audio. */
#define OPST_CTRL 32
/* Three first-order all-pass stages in each loop. With a = 0 they are three samples of plain delay. */
#define OPST_STAGES 3
/* Highest tuned pitch is sr / 8: the loop needs a few samples of delay for its filters. */
#define OPST_MAX_PERIOD_DIV 8.0f
#define OPST_MIN_HZ 20.0f
#define OPST_MIN_D 3.0f

/* Loudness of a pluck at full velocity: RMS of the excitation burst for a loop low-pass at 8x the pitch. The burst is
 * scaled by sqrt(cutoff ratio / 8), capped, so the ringing fundamental keeps about the same level as DAMP moves
 * (a brighter burst spreads its energy over more partials) without a bright pluck clipping on its first millisecond. */
#define OPST_EXC_RMS 0.2f
#define OPST_EXC_SCALE_MAX 1.6f

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Linear up to 0.8, then a tanh knee that stays under 0.999 whatever comes in. */
static inline float soft_ceiling(float x)
{
    const float t0 = 0.8f;
    float a = fabsf(x);
    if (a <= t0)
        return x;
    float z = (a - t0) * 5.0f;
    z = z > 3.0f ? 3.0f : z;
    float z2 = z * z;
    float t = z * (27.0f + z2) / (27.0f + 9.0f * z2);
    float y = t0 + 0.199f * t;
    return x < 0.0f ? -y : y;
}

float opst_decay_seconds(float norm)
{
    return 0.08f * powf(300.0f, clampf(norm, 0.0f, 1.0f));
}

float opst_pick_position(float norm)
{
    return 0.02f + 0.48f * clampf(norm, 0.0f, 1.0f);
}

/* DAMP sets the loop low-pass cutoff as a multiple of the note's frequency, 2.5x (a few partials ring) up to 100x
 * (nearly everything rings). Following the pitch keeps the tone the same from note to note. Below about 2.5x the
 * low-pass would cost the fundamental more than the DECAY time allows, and the loop gain cannot make that up. */
static float damp_to_ratio(float damp)
{
    return 2.5f * powf(40.0f, clampf(damp, 0.0f, 1.0f));
}

/* All-pass coefficient from STIFF. Negative, so higher partials travel faster and land sharp of the harmonics. */
static float stiff_to_a(float stiff)
{
    return -0.85f * powf(clampf(stiff, 0.0f, 1.0f), 1.5f);
}

/* Phase delay in samples at w (radians per sample) of y = (1 - b) x + b y1. */
static float lp_phase_delay(float b, float w)
{
    return atan2f(b * sinf(w), 1.0f - b * cosf(w)) / w;
}

/* Phase delay in samples at w of one stage of (a + z^-1) / (1 + a z^-1). */
static float ap_phase_delay(float a, float w)
{
    return 1.0f - 2.0f * atan2f(a * sinf(w), 1.0f + a * cosf(w)) / w;
}

static inline float frand(opst_t *s)
{
    unsigned x = s->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->rng = x;
    return (float)(x >> 8) * (1.0f / 8388608.0f) - 1.0f;
}

/* Tune the loop for the voice's note, bend and vibrato. Sets the delay target, all-pass coefficient and loop gain. */
static void voice_control(opst_t *s, opst_voice *v)
{
    const float semis = (float)v->note + s->s_bend + s->vib - 69.0f;
    float f = 440.0f * exp2f(semis * (1.0f / 12.0f));
    f = clampf(f, OPST_MIN_HZ, s->sr / OPST_MAX_PERIOD_DIV);
    v->freq = f;

    const float w = 2.0f * PI_F * f / s->sr;
    const float total = s->sr / f;
    float b = expf(-w * damp_to_ratio(s->s_damp));
    b = clampf(b, 0.0f, 0.95f);
    v->b = b;
    const float pd_lp = lp_phase_delay(b, w);
    float a = s->a_ap;
    float d = total - pd_lp - (float)OPST_STAGES * ap_phase_delay(a, w);
    /* A strong STIFF on a high note would need a negative delay. Back the stiffness off until the loop fits. */
    for (int k = 0; k < 8 && d < OPST_MIN_D && a != 0.0f; k++) {
        a = (k == 7) ? 0.0f : a * 0.5f;
        d = total - pd_lp - (float)OPST_STAGES * ap_phase_delay(a, w);
    }
    v->a = a;
    v->d_tgt = clampf(d, OPST_MIN_D, (float)(OPST_BUF - 8));

    /* DECAY is the ring time of the fundamental: undo the low-pass's loss at the fundamental so only the upper
     * partials die faster than that. The low-pass never has gain above 1, so the loop stays stable. */
    const float t60 = opst_decay_seconds(s->s_decay);
    const float h0 = (1.0f - b) / sqrtf(1.0f - 2.0f * b * cosf(w) + b * b);
    v->g = clampf(expf(-6.907755f / (f * t60)) / h0, 0.0f, 0.99995f);

    const float sr = s->sr;
    const opst_params *p = &s->target;
    v->att_inc = 1.0f / ((p->env_attack > 0.001f ? p->env_attack : 0.001f) * sr);
    v->dec_coef = expf(-4.6f / ((p->env_decay > 0.002f ? p->env_decay : 0.002f) * sr));
    v->rel_coef = expf(-6.9f / ((p->env_release > 0.003f ? p->env_release : 0.003f) * sr));
    v->sus = p->env_sustain;
}

/* Fill the start of the delay line with a pluck: noise coloured by the hit velocity and by the loop's own low-pass,
 * comb-filtered around the loop by the pick position, mean removed, scaled to a fixed RMS. d is the loop's delay in
 * samples. Colouring the burst with the loop filter means the energy goes into what keeps ringing, so a low or dark
 * note is as loud as a high or bright one. */
static void excite(opst_t *s, opst_voice *v, float d, float vel01)
{
    int m = (int)ceilf(d) + 3;
    if (m > OPST_BUF - 8)
        m = OPST_BUF - 8;
    /* The first 3 samples are never read: the loop reads the last ceil(d) of them. */
    const int base = 3;
    const int len = m - base;
    float *e = v->buf + base;

    const float c = 0.85f * powf(1.0f - vel01, 1.5f);
    const float b = v->b;
    float z1 = 0.0f, z2 = 0.0f;
    for (int i = 0; i < len; i++) {
        z1 = (1.0f - c) * frand(s) + c * z1;
        z2 = (1.0f - b) * z1 + b * z2;
        e[i] = z2;
    }

    /* The loop is circular, so the pick comb e[i] - e[i - P] wraps around the burst. The top P samples are saved in
     * the unused part of the delay line first. */
    const int p = (int)(opst_pick_position(s->target.pick) * d + 0.5f);
    if (p >= 1 && p < len && m + p <= OPST_BUF) {
        float *scr = v->buf + m;
        memcpy(scr, e + (len - p), sizeof(float) * (size_t)p);
        for (int i = len - 1; i >= p; i--)
            e[i] -= e[i - p];
        for (int i = 0; i < p; i++)
            e[i] -= scr[i];
        memset(scr, 0, sizeof(float) * (size_t)p);
    }

    double sum = 0.0;
    for (int i = 0; i < len; i++)
        sum += e[i];
    const float mean = (float)(sum / len);
    double sq = 0.0;
    for (int i = 0; i < len; i++) {
        e[i] -= mean;
        sq += (double)e[i] * e[i];
    }
    const float rms = (float)sqrt(sq / len);
    float spread = sqrtf(damp_to_ratio(s->s_damp) * 0.125f);
    spread = spread > OPST_EXC_SCALE_MAX ? OPST_EXC_SCALE_MAX : spread;
    const float amp = OPST_EXC_RMS * spread * powf(vel01, 1.4f);
    const float k = rms > 1e-9f ? amp / rms : 0.0f;
    for (int i = 0; i < len; i++)
        e[i] *= k;
    v->w = m;
}

static void start_voice(opst_t *s, opst_voice *v, int note, int vel)
{
    memset(v->buf, 0, sizeof v->buf);
    v->note = note;
    v->state = OPST_ON;
    v->pedal_held = 0;
    v->age = ++s->age_counter;
    v->lp_z = 0.0f;
    v->ap_z[0] = v->ap_z[1] = v->ap_z[2] = 0.0f;
    v->quiet = 0;
    v->peak = 0.0f;
    v->env = 0.0f;
    v->stage = 0;
    v->fade = 1.0f;
    v->fade_step = 0.0f;
    v->pend_note = 0;
    v->pend_vel = 0;
    v->d_coef = 1.0f - expf(-1.0f / (0.004f * s->sr));

    voice_control(s, v);
    v->d_cur = v->d_tgt;
    excite(s, v, v->d_tgt, (float)vel * (1.0f / 127.0f));

    /* gentle spread: low notes slightly left, high notes slightly right */
    const float pan = clampf(((float)note - 60.0f) * (0.35f / 60.0f), -0.35f, 0.35f);
    const float ang = (pan + 1.0f) * (PI_F * 0.25f);
    v->pan_l = 1.41421356f * cosf(ang);
    v->pan_r = 1.41421356f * sinf(ang);
}

static void release_voice(opst_voice *v)
{
    if (v->state == OPST_ON) {
        v->state = OPST_REL;
        v->stage = 3;
    }
    v->pedal_held = 0;
}

void opst_init(opst_t *s, float sample_rate)
{
    memset(s, 0, sizeof *s);
    s->sr = sample_rate > 8000.0f ? sample_rate : 44100.0f;
    s->seed = 0x1234abcdu;
    s->target.pick = OPST_DEFAULT_PICK;
    s->target.damp = OPST_DEFAULT_DAMP;
    s->target.stiff = OPST_DEFAULT_STIFF;
    s->target.decay = OPST_DEFAULT_DECAY;
    s->target.env_attack = OPST_DEFAULT_ATTACK;
    s->target.env_decay = OPST_DEFAULT_ENV_DECAY;
    s->target.env_sustain = OPST_DEFAULT_SUSTAIN;
    s->target.env_release = OPST_DEFAULT_RELEASE;
    s->target.level = OPST_DEFAULT_LEVEL;
    s->ctrl_coef = 1.0f - expf(-(float)OPST_CTRL / (0.015f * s->sr));
    s->level_coef = 1.0f - expf(-1.0f / (0.005f * s->sr));
    opst_reset(s);
}

void opst_reset(opst_t *s)
{
    memset(s->voice, 0, sizeof s->voice);
    s->s_damp = s->target.damp;
    s->s_stiff = s->target.stiff;
    s->s_decay = s->target.decay;
    s->s_bend = s->bend_target = 0.0f;
    s->s_mod = s->mod_target = 0.0f;
    s->vib = 0.0f;
    s->a_ap = stiff_to_a(s->s_stiff);
    s->level_cur = s->target.level;
    s->lfo_phase = 0.0f;
    s->ctrl_left = 0;
    s->pedal = 0;
    s->age_counter = 0;
    s->out_peak = 0.0f;
    s->rng = s->seed ? s->seed : 1u;
}

void opst_set_seed(opst_t *s, unsigned seed)
{
    s->seed = seed ? seed : 1u;
    s->rng = s->seed;
}

void opst_set_params(opst_t *s, const opst_params *p)
{
    opst_params *t = &s->target;
    t->pick = clampf(p->pick, 0.0f, 1.0f);
    t->damp = clampf(p->damp, 0.0f, 1.0f);
    t->stiff = clampf(p->stiff, 0.0f, 1.0f);
    t->decay = clampf(p->decay, 0.0f, 1.0f);
    t->env_attack = clampf(p->env_attack, 0.0f, 30.0f);
    t->env_decay = clampf(p->env_decay, 0.0f, 60.0f);
    t->env_sustain = clampf(p->env_sustain, 0.0f, 1.0f);
    t->env_release = clampf(p->env_release, 0.0f, 60.0f);
    t->level = clampf(p->level, 0.0f, 4.0f);
}

static int alloc_voice(opst_t *s)
{
    int best = -1;
    for (int i = 0; i < OPST_VOICES; i++)
        if (s->voice[i].state == OPST_IDLE)
            return i;
    /* quietest voice that is already releasing */
    float quiet = 2.0f;
    for (int i = 0; i < OPST_VOICES; i++)
        if (s->voice[i].state == OPST_REL && s->voice[i].env < quiet) {
            quiet = s->voice[i].env;
            best = i;
        }
    if (best >= 0)
        return best;
    /* oldest held voice */
    unsigned oldest = 0xffffffffu;
    for (int i = 0; i < OPST_VOICES; i++)
        if (s->voice[i].state == OPST_ON && s->voice[i].age < oldest) {
            oldest = s->voice[i].age;
            best = i;
        }
    if (best >= 0)
        return best;
    return 0; /* every voice is already fading out for a pending note: replace the first one's */
}

void opst_note_on(opst_t *s, int note, int vel)
{
    if (vel <= 0) {
        opst_note_off(s, note);
        return;
    }
    note = note < 0 ? 0 : (note > 127 ? 127 : note);
    vel = vel > 127 ? 127 : vel;

    for (int i = 0; i < OPST_VOICES; i++)
        if (s->voice[i].state == OPST_ON && s->voice[i].note == note)
            release_voice(&s->voice[i]);

    const int idx = alloc_voice(s);
    opst_voice *v = &s->voice[idx];
    if (v->state == OPST_IDLE) {
        start_voice(s, v, note, vel);
    } else {
        /* fade the old sound out over a couple of milliseconds, then the new note starts in this voice */
        int fade_n = (int)(0.0025f * s->sr);
        if (fade_n < 16)
            fade_n = 16;
        if (v->state != OPST_STEAL) {
            v->state = OPST_STEAL;
            v->fade = 1.0f;
            v->fade_step = 1.0f / (float)fade_n;
        }
        v->pend_note = note;
        v->pend_vel = vel;
    }
}

void opst_note_off(opst_t *s, int note)
{
    for (int i = 0; i < OPST_VOICES; i++) {
        opst_voice *v = &s->voice[i];
        if (v->state == OPST_ON && v->note == note) {
            if (s->pedal)
                v->pedal_held = 1;
            else
                release_voice(v);
        } else if (v->state == OPST_STEAL && v->pend_vel > 0 && v->pend_note == note) {
            v->pend_vel = -1; /* released before it ever started */
        }
    }
}

void opst_pitch_bend(opst_t *s, float semitones)
{
    s->bend_target = clampf(semitones, -48.0f, 48.0f);
}

void opst_mod_wheel(opst_t *s, float amount)
{
    s->mod_target = clampf(amount, 0.0f, 1.0f);
}

void opst_sustain(opst_t *s, int down)
{
    s->pedal = down ? 1 : 0;
    if (!down)
        for (int i = 0; i < OPST_VOICES; i++)
            if (s->voice[i].state == OPST_ON && s->voice[i].pedal_held)
                release_voice(&s->voice[i]);
}

void opst_all_off(opst_t *s)
{
    s->pedal = 0;
    for (int i = 0; i < OPST_VOICES; i++) {
        opst_voice *v = &s->voice[i];
        if (v->state == OPST_ON)
            release_voice(v);
        else if (v->state == OPST_STEAL && v->pend_vel > 0)
            v->pend_vel = -1;
    }
}

/* Global control tick: smooth the knobs, advance the vibrato, retune every sounding voice. */
static void control_update(opst_t *s)
{
    const float c = s->ctrl_coef;
    s->s_damp += c * (s->target.damp - s->s_damp);
    s->s_stiff += c * (s->target.stiff - s->s_stiff);
    s->s_decay += c * (s->target.decay - s->s_decay);
    s->s_bend += c * (s->bend_target - s->s_bend);
    s->s_mod += c * (s->mod_target - s->s_mod);
    s->a_ap = stiff_to_a(s->s_stiff);
    s->lfo_phase += 2.0f * PI_F * 5.2f * (float)OPST_CTRL / s->sr;
    if (s->lfo_phase > 2.0f * PI_F)
        s->lfo_phase -= 2.0f * PI_F;
    s->vib = s->s_mod * 0.35f * sinf(s->lfo_phase);
    for (int i = 0; i < OPST_VOICES; i++)
        if (s->voice[i].state != OPST_IDLE)
            voice_control(s, &s->voice[i]);
}

static void voice_render(opst_t *s, opst_voice *v, float *out_l, float *out_r, int n)
{
    const unsigned mask = OPST_BUF - 1;
    const int silence_samples = (int)(0.1f * s->sr);

    int i = 0;
    while (i < n && v->state != OPST_IDLE) {
        int limit = n;
        if (v->state == OPST_STEAL) {
            int k = (int)ceilf(v->fade / v->fade_step);
            if (k < 1)
                k = 1;
            if (k < n - i)
                limit = i + k;
        }

        float *buf = v->buf;
        unsigned w = (unsigned)v->w;
        float d_cur = v->d_cur;
        const float d_tgt = v->d_tgt, d_coef = v->d_coef;
        float lp = v->lp_z, z0 = v->ap_z[0], z1 = v->ap_z[1], z2 = v->ap_z[2];
        const float a = v->a, g = v->g, b = v->b;
        float env = v->env;
        int stage = v->stage;
        float fade = v->fade;
        int quiet = v->quiet;
        float peak = v->peak;
        const int stealing = v->state == OPST_STEAL;
        int done = 0;

        for (; i < limit; i++) {
            d_cur += d_coef * (d_tgt - d_cur);
            const int di = (int)d_cur;
            const float x = d_cur - (float)di;
            const float xm1 = x - 1.0f, xm2 = x - 2.0f, xp1 = x + 1.0f;
            /* 4-point Lagrange read at delays di-1 .. di+2 */
            const float c0 = -x * xm1 * xm2 * (1.0f / 6.0f);
            const float c1 = xp1 * xm1 * xm2 * 0.5f;
            const float c2 = -xp1 * x * xm2 * 0.5f;
            const float c3 = xp1 * x * xm1 * (1.0f / 6.0f);
            const unsigned i0 = w - (unsigned)di;
            const float r = c0 * buf[(i0 + 1u) & mask] + c1 * buf[i0 & mask] + c2 * buf[(i0 - 1u) & mask] +
                            c3 * buf[(i0 - 2u) & mask];

            const float y0 = a * r + z0;
            z0 = r - a * y0;
            const float y1 = a * y0 + z1;
            z1 = y0 - a * y1;
            const float y2 = a * y1 + z2;
            z2 = y1 - a * y2;
            lp = (1.0f - b) * y2 + b * lp;
            const float y = g * lp;
            buf[w & mask] = y;
            w = (w + 1u) & mask;

            switch (stage) {
            case 0:
                env += v->att_inc;
                if (env >= 1.0f) {
                    env = 1.0f;
                    stage = 1;
                }
                break;
            case 1:
                env = v->sus + (env - v->sus) * v->dec_coef;
                break;
            default:
                env *= v->rel_coef;
                if (env < 1e-4f)
                    done = 1;
                break;
            }

            const float o = y * env * fade;
            const float ao = fabsf(o);
            if (ao > peak)
                peak = ao;
            out_l[i] += o * v->pan_l;
            out_r[i] += o * v->pan_r;
            if (stealing)
                fade -= v->fade_step;

            quiet = fabsf(y) < 1e-5f ? quiet + 1 : 0;
            if (quiet > silence_samples)
                done = 1;
            if (done) {
                i++;
                break;
            }
        }

        v->w = (int)w;
        v->d_cur = d_cur;
        v->lp_z = lp;
        v->ap_z[0] = z0;
        v->ap_z[1] = z1;
        v->ap_z[2] = z2;
        v->env = env;
        v->stage = stage;
        v->fade = fade;
        v->quiet = quiet;
        v->peak = peak;

        if (stealing && (done || fade <= 0.0f)) {
            /* the old sound is gone (faded out, or it had already died): the pending note takes the voice */
            if (v->pend_vel > 0)
                start_voice(s, v, v->pend_note, v->pend_vel);
            else
                v->state = OPST_IDLE;
        } else if (done) {
            v->state = OPST_IDLE;
        }
    }
}

void opst_render(opst_t *s, float *left, float *right, int frames)
{
    int done = 0;
    while (done < frames) {
        if (s->ctrl_left <= 0) {
            control_update(s);
            s->ctrl_left = OPST_CTRL;
        }
        const int n = frames - done < s->ctrl_left ? frames - done : s->ctrl_left;
        float *l = left + done, *r = right + done;
        memset(l, 0, sizeof(float) * (size_t)n);
        memset(r, 0, sizeof(float) * (size_t)n);
        for (int v = 0; v < OPST_VOICES; v++)
            if (s->voice[v].state != OPST_IDLE)
                voice_render(s, &s->voice[v], l, r, n);

        float pk = s->out_peak;
        for (int i = 0; i < n; i++) {
            s->level_cur += s->level_coef * (s->target.level - s->level_cur);
            l[i] = soft_ceiling(l[i] * s->level_cur);
            r[i] = soft_ceiling(r[i] * s->level_cur);
            const float a = fabsf(l[i]) > fabsf(r[i]) ? fabsf(l[i]) : fabsf(r[i]);
            if (a > pk)
                pk = a;
        }
        s->out_peak = pk;
        s->ctrl_left -= n;
        done += n;
    }
}

float opst_take_level(opst_t *s)
{
    const float p = s->out_peak;
    s->out_peak = 0.0f;
    return p;
}

int opst_voice_info(opst_t *s, int index, int *note, float *level)
{
    if (index < 0 || index >= OPST_VOICES)
        return 0;
    opst_voice *v = &s->voice[index];
    *note = v->note;
    *level = v->peak;
    v->peak = 0.0f;
    return v->state != OPST_IDLE;
}

int opst_active_voices(const opst_t *s)
{
    int n = 0;
    for (int i = 0; i < OPST_VOICES; i++)
        n += s->voice[i].state != OPST_IDLE;
    return n;
}
