#include "digital_core.h"

#include <math.h>
#include <string.h>

#define TWO_PI_F 6.28318530717958647692f
#define PI_F 3.14159265358979323846f

/* The control rate. Frequencies, gains and the knob-driven amounts are recomputed every CTRL samples and
 * ramped linearly between ticks where a step would be heard. */
#define CTRL 16

/* Voice states. */
enum { V_IDLE = 0, V_ATTACK = 1, V_DECAY = 2, V_RELEASE = 3 };

/* Phase starts half a cycle in, which is a zero crossing of the sine. */
#define PHASE_START 0x80000000u
#define TURN 4294967296.0

/* The mod wheel adds this much to FEEDBACK at the top. */
#define MOD_FEEDBACK 0.5f

/* Output stage. The voice sum goes into a soft ceiling that is linear up to CEIL_T and reaches CEIL_MAX,
 * so the summed signal cannot pass CEIL_MAX before the level control. One voice peaks at about 0.5. */
#define CEIL_T 0.7f
#define CEIL_MAX 0.97f

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

/* Linear-interpolated sine from a table. x is in cycles and may be any sign. */
static inline float sine_cycles(const float *tab, float x)
{
    const float t = x * (float)DIGITAL_SINE_SIZE;
    int k = (int)t;
    if (t < (float)k)
        k--;
    const float fr = t - (float)k;
    k &= DIGITAL_SINE_SIZE - 1;
    const float a = tab[k];
    return a + fr * (tab[k + 1] - a);
}

/* Same table, read from a 32-bit phase. */
static inline float sine_phase(const float *tab, unsigned ph)
{
    const unsigned k = ph >> (32 - DIGITAL_SINE_BITS);
    const float fr = (float)(ph & ((1u << (32 - DIGITAL_SINE_BITS)) - 1u)) * (1.0f / (float)(1u << (32 - DIGITAL_SINE_BITS)));
    const float a = tab[k];
    return a + fr * (tab[k + 1] - a);
}

int digital_octave_step(float ratio_norm)
{
    const int s = (int)floorf(clampf(ratio_norm, 0.0f, 1.0f) * 8.0f) - 3;
    return s > 4 ? 4 : s;
}

float digital_osc2_ratio(float ratio_norm, float detune)
{
    return exp2f((float)digital_octave_step(ratio_norm)) + clampf(detune, -1.0f, 1.0f) * (1.0f / 16.0f);
}

float digital_osc2_level(float ratio_norm)
{
    return (32767.0f - clampf(ratio_norm, 0.0f, 1.0f) * 32767.0f) * (1.0f / 32768.0f);
}

int digital_sync_active(float detune)
{
    return clampf(detune, -1.0f, 1.0f) * 32767.0f >= 16.0f;
}

float digital_crunch_depth(float crunch_norm)
{
    const float n = clampf(crunch_norm, 0.0f, 1.0f);
    return n * n;
}

float digital_feedback_radians(float feedback_norm)
{
    return PI_F * clampf(feedback_norm, 0.0f, 1.0f);
}

float digital_level_gain(float norm)
{
    const float n = clampf(norm, 0.0f, 1.0f);
    return n * n;
}

void digital_default_params(digital_params *p)
{
    p->feedback = DIGITAL_DEFAULT_FEEDBACK;
    p->ratio = DIGITAL_DEFAULT_RATIO;
    p->detune = DIGITAL_DEFAULT_DETUNE;
    p->crunch = DIGITAL_DEFAULT_CRUNCH;
    p->attack = DIGITAL_DEFAULT_ATTACK;
    p->decay = DIGITAL_DEFAULT_DECAY;
    p->sustain = DIGITAL_DEFAULT_SUSTAIN;
    p->release = DIGITAL_DEFAULT_RELEASE;
    p->level = DIGITAL_DEFAULT_LEVEL;
    p->bend_range = DIGITAL_DEFAULT_BEND_RANGE;
}

static float one_pole_coef(float tau_seconds, float sr, float samples)
{
    return 1.0f - expf(-samples / (tau_seconds * sr));
}

void digital_set_params(digital_t *d, const digital_params *in)
{
    digital_params *t = &d->target;
    t->feedback = clampf(in->feedback, 0.0f, 1.0f);
    t->ratio = clampf(in->ratio, 0.0f, 1.0f);
    t->detune = clampf(in->detune, -1.0f, 1.0f);
    t->crunch = clampf(in->crunch, 0.0f, 1.0f);
    t->attack = clampf(in->attack, 0.0005f, 10.0f);
    t->decay = clampf(in->decay, 0.005f, 30.0f);
    t->sustain = clampf(in->sustain, 0.0f, 1.0f);
    t->release = clampf(in->release, 0.005f, 30.0f);
    t->level = clampf(in->level, 0.0f, 1.0f);
    t->bend_range = clampf(in->bend_range, 0.0f, 24.0f);

    d->att_inc = 1.0f / (t->attack * d->sr);
    /* exp(-6.9) is -60 dB: the time constants are set so that decay and release are times to fall 60 dB. */
    d->dec_c = expf(-6.9f / (t->decay * d->sr));
    d->rel_c = expf(-6.9f / (t->release * d->sr));
}

void digital_reset(digital_t *d)
{
    memset(d->voice, 0, sizeof d->voice);
    for (int i = 0; i < DIGITAL_VOICES; i++) {
        /* spread the voices across the stereo field, constant power, +-0.25 */
        const float pan = ((float)((i * 5) % DIGITAL_VOICES) - 3.5f) / 3.5f * 0.25f;
        const float ang = (pan + 1.0f) * 0.25f * TWO_PI_F * 0.5f;
        d->voice[i].pan_l = cosf(ang);
        d->voice[i].pan_r = sinf(ang);
    }
    const digital_params *t = &d->target;
    d->s_fb = t->feedback;
    d->s_ratio = t->ratio;
    d->s_detune = t->detune;
    d->s_crunch = t->crunch;
    d->s_level = t->level;
    d->bend_norm = 0.0f;
    d->s_bend = 0.0f;
    d->mod_wheel = 0.0f;
    d->s_mod = 0.0f;
    d->pedal = 0;
    d->lr2 = log2f(digital_osc2_ratio(t->ratio, t->detune));
    d->sync_on = digital_sync_active(t->detune);
    d->fb_cur = digital_feedback_radians(d->s_fb) * (1.0f / PI_F);
    d->fb_d = 0.0f;
    d->lev2_cur = digital_osc2_level(d->s_ratio);
    d->lev2_d = 0.0f;
    d->crunch_cur = digital_crunch_depth(d->s_crunch);
    d->crunch_d = 0.0f;
    d->level_cur = digital_level_gain(d->s_level);
    d->level_d = 0.0f;
    d->tick_left = 0;
    d->tick_pos = 0;
    d->act_peak = 0.0f;
}

void digital_init(digital_t *d, float sample_rate)
{
    memset(d, 0, sizeof *d);
    d->sr = clampf(sample_rate, 8000.0f, 384000.0f);
    for (int i = 0; i <= DIGITAL_SINE_SIZE; i++)
        d->sine[i] = (float)sin((double)i * (6.283185307179586 / (double)DIGITAL_SINE_SIZE));
    d->c_smooth = one_pole_coef(0.005f, d->sr, (float)CTRL);
    d->c_slew = one_pole_coef(0.002f, d->sr, (float)CTRL);
    d->c_glide = one_pole_coef(0.008f, d->sr, (float)CTRL);
    digital_default_params(&d->target);
    digital_set_params(d, &d->target);
    digital_reset(d);
}

/* ---- notes ---- */

static digital_voice *find_voice(digital_t *d, int note)
{
    digital_voice *quiet = NULL, *oldest = NULL;
    for (int i = 0; i < DIGITAL_VOICES; i++) {
        digital_voice *v = &d->voice[i];
        if (v->state != V_IDLE && v->note == note)
            return v; /* same note again: retrigger the voice it already has */
    }
    for (int i = 0; i < DIGITAL_VOICES; i++)
        if (d->voice[i].state == V_IDLE)
            return &d->voice[i];
    /* Steal. A voice already in release goes first, the quietest of those; otherwise the oldest note. */
    for (int i = 0; i < DIGITAL_VOICES; i++) {
        digital_voice *v = &d->voice[i];
        if (v->state == V_RELEASE && (!quiet || v->env < quiet->env))
            quiet = v;
        if (!oldest || (unsigned)(d->serial - v->age) > (unsigned)(d->serial - oldest->age))
            oldest = v;
    }
    return quiet ? quiet : oldest;
}

void digital_note_on(digital_t *d, int note, int velocity)
{
    if (velocity <= 0) {
        digital_note_off(d, note);
        return;
    }
    note = note < 0 ? 0 : (note > 127 ? 127 : note);
    velocity = velocity > 127 ? 127 : velocity;

    digital_voice *v = find_voice(d, note);
    if (v->state == V_IDLE) {
        /* A fresh voice starts from silence with the phases at their start. A reused one keeps its envelope,
         * phases and filter memory and slides to the new note, so taking over a sounding voice does not step
         * the output. */
        const float pl = v->pan_l, pr = v->pan_r;
        memset(v, 0, sizeof *v);
        v->pan_l = pl;
        v->pan_r = pr;
        v->ph1 = v->ph2 = v->ph3 = PHASE_START;
        v->fresh = 1;
    }
    v->note = note;
    v->vel = (float)velocity * (1.0f / 127.0f);
    v->held = 1;
    v->sustained = 0;
    v->state = V_ATTACK;
    v->age = ++d->serial;
    d->tick_left = 0;
}

void digital_note_off(digital_t *d, int note)
{
    for (int i = 0; i < DIGITAL_VOICES; i++) {
        digital_voice *v = &d->voice[i];
        if (v->state == V_IDLE || v->note != note || !v->held)
            continue;
        v->held = 0;
        if (d->pedal)
            v->sustained = 1;
        else
            v->state = V_RELEASE;
    }
}

void digital_pitch_bend(digital_t *d, float norm)
{
    d->bend_norm = clampf(norm, -1.0f, 1.0f);
}

void digital_mod_wheel(digital_t *d, float norm)
{
    d->mod_wheel = clampf(norm, 0.0f, 1.0f);
}

void digital_sustain_pedal(digital_t *d, int down)
{
    d->pedal = down ? 1 : 0;
    if (d->pedal)
        return;
    for (int i = 0; i < DIGITAL_VOICES; i++) {
        digital_voice *v = &d->voice[i];
        if (v->state != V_IDLE && v->sustained) {
            v->sustained = 0;
            v->state = V_RELEASE;
        }
    }
}

void digital_all_off(digital_t *d)
{
    d->pedal = 0;
    for (int i = 0; i < DIGITAL_VOICES; i++) {
        digital_voice *v = &d->voice[i];
        if (v->state == V_IDLE)
            continue;
        v->held = 0;
        v->sustained = 0;
        v->state = V_RELEASE;
    }
}

int digital_active_voices(const digital_t *d)
{
    int n = 0;
    for (int i = 0; i < DIGITAL_VOICES; i++)
        n += d->voice[i].state != V_IDLE;
    return n;
}

void digital_take_activity(digital_t *d, digital_activity *out)
{
    out->peak = d->act_peak;
    out->voices = digital_active_voices(d);
    out->notes = d->serial;
    d->act_peak = 0.0f;
}

/* ---- control tick ---- */

static unsigned phase_inc(double cycles_per_sample)
{
    /* At most half a cycle per sample, which is Nyquist. */
    if (cycles_per_sample > 0.5)
        cycles_per_sample = 0.5;
    if (cycles_per_sample < 0.0)
        cycles_per_sample = 0.0;
    return (unsigned)(cycles_per_sample * TURN);
}

static void tick_begin(digital_t *d)
{
    const digital_params *t = &d->target;
    const float c = d->c_smooth;

    d->s_fb += c * (t->feedback - d->s_fb);
    d->s_ratio += c * (t->ratio - d->s_ratio);
    d->s_detune += c * (t->detune - d->s_detune);
    d->s_crunch += c * (t->crunch - d->s_crunch);
    d->s_level += c * (t->level - d->s_level);
    d->s_bend += c * (d->bend_norm * t->bend_range - d->s_bend);
    d->s_mod += c * (d->mod_wheel - d->s_mod);

    /* The octave step is a staircase in the knob, so the ratio glides to it instead of jumping. */
    d->lr2 += d->c_glide * (log2f(digital_osc2_ratio(d->s_ratio, d->s_detune)) - d->lr2);
    const double ratio2 = exp2((double)d->lr2);
    d->sync_on = digital_sync_active(d->s_detune);

    /* Ramps: each control is a start value plus a per-sample slope, read as start + slope * k at sample k of the
     * tick, so the result does not depend on how a tick is split across render calls. A tick cut short by a note
     * moves its start values on by the samples it did render. */
    const float inv = 1.0f / (float)CTRL;
    const float adv = (float)d->tick_pos;
    d->fb_cur += d->fb_d * adv;
    d->lev2_cur += d->lev2_d * adv;
    d->crunch_cur += d->crunch_d * adv;
    d->level_cur += d->level_d * adv;
    const float fb = clampf(d->s_fb + MOD_FEEDBACK * d->s_mod, 0.0f, 1.0f);
    d->fb_d = (fb - d->fb_cur) * inv;
    d->lev2_d = (digital_osc2_level(d->s_ratio) - d->lev2_cur) * inv;
    d->crunch_d = (digital_crunch_depth(d->s_crunch) - d->crunch_cur) * inv;
    d->level_d = (digital_level_gain(d->s_level) - d->level_cur) * inv;
    d->tick_pos = 0;

    for (int i = 0; i < DIGITAL_VOICES; i++) {
        digital_voice *v = &d->voice[i];
        if (v->state == V_IDLE)
            continue;
        const double f0 = 440.0 * exp2(((double)(v->note - 69) + (double)d->s_bend) * (1.0 / 12.0)) / (double)d->sr;
        v->inc1 = phase_inc(f0);
        v->inc2 = phase_inc(f0 * ratio2);

        if (v->fresh) {
            v->gain_s = v->vel;
            v->gain_cur = v->vel;
            v->fresh = 0;
        } else {
            v->gain_cur += v->gain_d * adv;
            v->gain_s += d->c_slew * (v->vel - v->gain_s);
        }
        v->gain_d = (v->gain_s - v->gain_cur) * inv;
    }
}

/* One voice, n samples, added into l and r. See docs/DSP.md for the signal path. */
static void render_voice(digital_t *d, digital_voice *v, float *l, float *r, int n)
{
    const float *tab = d->sine;
    const float sus = d->target.sustain;
    float env = v->env;
    int state = v->state;
    const float k0 = (float)d->tick_pos;
    const float gain0 = v->gain_cur, gd = v->gain_d;
    const float fb0 = d->fb_cur, lev20 = d->lev2_cur, crunch0 = d->crunch_cur;
    const float fbd = d->fb_d, lev2d = d->lev2_d, crunchd = d->crunch_d;
    unsigned ph1 = v->ph1, ph2 = v->ph2, ph3 = v->ph3;
    const unsigned inc1 = v->inc1, inc2 = v->inc2;
    const int sync = d->sync_on;
    float y1 = v->y1, zprev = v->zprev, xprev = v->xprev;
    const float pl = v->pan_l, pr = v->pan_r;

    for (int i = 0; i < n; i++) {
        switch (state) {
        case V_ATTACK:
            env += d->att_inc;
            if (env >= 1.0f) {
                env = 1.0f;
                state = V_DECAY;
            }
            break;
        case V_DECAY:
            /* Settles on the sustain level, and follows it if the knob moves. */
            env = sus + (env - sus) * d->dec_c;
            break;
        default: /* V_RELEASE */
            env *= d->rel_c;
            break;
        }
        if ((state == V_RELEASE || (state == V_DECAY && sus < 1e-4f)) && env < 1e-4f) {
            state = V_IDLE;
            env = 0.0f;
            break;
        }

        const float fk = k0 + (float)i; /* one rounding, whatever the split */
        const float gain = gain0 + gd * fk, fb = fb0 + fbd * fk, lev2 = lev20 + lev2d * fk,
                    crunch = crunch0 + crunchd * fk;

        /* Oscillator 1: a sine that phase-modulates itself with its previous output. FEEDBACK of 1 is pi radians. */
        y1 = sine_cycles(tab, (float)ph1 * (1.0f / (float)TURN) + 0.5f * fb * y1);
        const unsigned ph1_old = ph1;
        ph1 += inc1;

        /* Oscillator 2: a plain sine, added as the sum of its last two samples, then scaled by the RATIO level. */
        const float z = sine_phase(tab, ph2);
        ph2 += inc2;
        if (sync && ph1 < ph1_old)
            ph2 = ph1; /* oscillator 1 wrapped: hard sync */
        const float m2 = clampf(zprev + z, -1.0f, 1.0f) * lev2;
        zprev = z;
        const float mix = clampf(0.5f * y1 + m2, -1.0f, 1.0f);

        /* XOR stage: the mix, as a 16-bit integer, against a rectified sine at oscillator 2's pitch, scaled by
         * CRUNCH. At CRUNCH 0 it passes the mix through, halved. Then a two-sample average. */
        const float s3 = sine_phase(tab, ph3);
        ph3 += inc2;
        int a = (int)lrintf(mix * 32768.0f);
        a = a > 32767 ? 32767 : (a < -32768 ? -32768 : a);
        int b = (int)(fabsf(s3) * crunch * 32768.0f + 0.5f);
        b = b > 32767 ? 32767 : b;
        const int xi = a ^ b;
        const float x = (float)((xi - (xi & 1)) / 2) * (1.0f / 32768.0f);
        const float out = 0.5f * (xprev + x);
        xprev = x;

        const float o = out * env * gain;
        l[i] += o * pl;
        r[i] += o * pr;
    }

    v->env = env;
    v->state = state;
    v->ph1 = ph1;
    v->ph2 = ph2;
    v->ph3 = ph3;
    v->y1 = y1;
    v->zprev = zprev;
    v->xprev = xprev;
}

void digital_render(digital_t *d, float *l, float *r, int frames)
{
    int done = 0;
    while (done < frames) {
        if (d->tick_left <= 0) {
            tick_begin(d);
            d->tick_left = CTRL;
        }
        const int n = frames - done < d->tick_left ? frames - done : d->tick_left;
        float *pl = l + done, *pr = r + done;
        memset(pl, 0, sizeof(float) * (size_t)n);
        memset(pr, 0, sizeof(float) * (size_t)n);

        for (int v = 0; v < DIGITAL_VOICES; v++)
            if (d->voice[v].state != V_IDLE)
                render_voice(d, &d->voice[v], pl, pr, n);

        float peak = d->act_peak;
        for (int i = 0; i < n; i++) {
            const float lev = d->level_cur + d->level_d * ((float)d->tick_pos + (float)i);
            const float a = soft_ceiling(pl[i]) * lev;
            const float b = soft_ceiling(pr[i]) * lev;
            pl[i] = a;
            pr[i] = b;
            const float m = fabsf(a) > fabsf(b) ? fabsf(a) : fabsf(b);
            if (m > peak)
                peak = m;
        }
        d->act_peak = peak;

        d->tick_pos += n;
        d->tick_left -= n;
        done += n;
    }
}
