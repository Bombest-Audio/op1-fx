/* Phase DSP core. See phase_core.h and docs/DSP.md. */
#include "phase_core.h"

#include <math.h>
#include <string.h>

#define TWO_PI 6.28318530717958647692f
#define VOICE_GAIN 0.5f      /* one centred voice peaks at about -9 dBFS before the master level */
#define BRIGHT_DIV 40.0f     /* full bend up to sr / BRIGHT_DIV Hz (1.1 kHz at 44.1 kHz), tapering above that */
#define WINDOW_DIV 30.0f     /* full window ramp up to sr / WINDOW_DIV Hz, tapering above that */
#define KNEE 0.6f            /* output is linear below this and rounds off towards 1.0 above it */
#define FADE_SECONDS 0.003f  /* a stolen voice fades out over this long */
#define END_LEVEL 1e-4f      /* an envelope below this (-80 dB) ends the voice */

enum { ST_OFF = 0, ST_ATTACK = 1, ST_SUSTAIN = 2, ST_RELEASE = 3, ST_FADE = 4 };

static float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

void phase_default_params(phase_params *p)
{
    p->a = PHASE_DEFAULT_A;
    p->b = PHASE_DEFAULT_B;
    p->r = PHASE_DEFAULT_R;
    p->w = PHASE_DEFAULT_W;
    p->attack = PHASE_DEFAULT_ATTACK;
    p->decay = PHASE_DEFAULT_DECAY;
    p->sustain = PHASE_DEFAULT_SUSTAIN;
    p->release = PHASE_DEFAULT_RELEASE;
    p->level = PHASE_DEFAULT_LEVEL;
    p->bend_range = PHASE_DEFAULT_BEND_RANGE;
}

float phase_note_hz(int note)
{
    return 440.0f * exp2f(((float)note - 69.0f) * (1.0f / 12.0f));
}

/* Half-cycle phase warp. u runs 0..1 through one half cycle and the result does too. At exponent 1 it is the
 * identity. Higher exponents hold the half back and then rush it through its end, which moves the peak later and adds
 * harmonics. The blend with sin^2(pi u) keeps the slope at both ends equal to 1, so the warped phase has no kink where
 * one half cycle meets the next. (The price: it is monotonic only up to exponent 2.1; beyond that it can dip back
 * by under 0.001 of a half cycle, which is inaudible.) That keeps the spectrum falling fast enough to limit aliasing without oversampling. */
float phase_warp(float u, float gamma)
{
    const float s = sinf(3.14159265358979f * u);
    return u + (powf(u, gamma) - u) * s * s;
}

/* The oscillator. The cycle is split in two halves. Each half runs the phase of a sine through the warp above: the
 * positive half with exponent 2^(3a), the negative half with 2^(3b). At 0 the cycle is a plain sine. `bright` (0..1)
 * scales both exponents and `nmax` limits the resonant ratio range, which is how the voices stay clear of aliasing at
 * high pitch. */
static float shape_limited(float ph, float a, float b, float r, float w, float bright, float nmax)
{
    float psi;
    if (ph < 0.5f)
        psi = (a > 0.0f) ? 0.5f * phase_warp(2.0f * ph, exp2f(3.0f * a * bright)) : ph;
    else
        psi = (b > 0.0f) ? 0.5f + 0.5f * phase_warp(2.0f * ph - 1.0f, exp2f(3.0f * b * bright)) : ph;

    float s = sinf(TWO_PI * psi);

    /* Resonance: a sine at n times the cycle rate, under a window that is 0 with zero slope at both ends of the
     * cycle (a smooth onset over the first quarter, then a quadratic fall). It runs on the unwarped phase, so the
     * bends do not push its frequency up. The base sine is 0 at ph = 0 too, so the output stays continuous when r
     * changes, and the slope is continuous at every wrap of the phase. */
    const float amt = r * 10.0f > 1.0f ? 1.0f : r * 10.0f;
    if (amt > 0.0f) {
        const float t = ph * 4.0f > 1.0f ? 1.0f : ph * 4.0f;
        const float fall = 1.0f - ph;
        const float k = amt * 1.6f * fall * fall * t * t * (3.0f - 2.0f * t);
        if (k > 1e-6f) {
            const float span = (nmax > 16.0f ? 16.0f : nmax) - 1.0f;
            const float n = 1.0f + r * (span > 0.0f ? span : 0.0f);
            s = (1.0f - k) * s + k * sinf(TWO_PI * n * ph);
        }
    }

    /* Window: a falling ramp over the whole cycle, makes the wave sawtooth-like. The gain brings the loss of level
     * back up part of the way. */
    return (1.0f - w * ph) * s * (1.0f / (1.0f - 0.35f * w));
}

float phase_shape(float ph, float a, float b, float r, float w)
{
    return shape_limited(clampf(ph, 0.0f, 0.9999999f), clampf(a, 0.0f, 1.0f), clampf(b, 0.0f, 1.0f),
                         clampf(r, 0.0f, 1.0f), clampf(w, 0.0f, 1.0f), 1.0f, 16.0f);
}

static void voice_pan(int idx, phase_voice *v)
{
    /* spread the slots a little: -0.3 .. 0.3 of the way to either side */
    const float pos = (float)((idx * 5) % PHASE_VOICES) / (float)(PHASE_VOICES - 1) - 0.5f;
    const float theta = 0.78539816339f * (1.0f + 0.6f * pos);
    v->gl = cosf(theta);
    v->gr = sinf(theta);
}

void phase_init(phase_t *p, float sample_rate)
{
    memset(p, 0, sizeof *p);
    p->sr = clampf(sample_rate, 8000.0f, 384000.0f);
    phase_default_params(&p->target);
    p->smooth_coef = 1.0f - expf(-1.0f / (0.005f * p->sr));
    p->bend_coef = 1.0f - expf(-1.0f / (0.008f * p->sr));
    p->mod_coef = 1.0f - expf(-1.0f / (0.010f * p->sr));
    p->dc_r = 1.0f - TWO_PI * 8.0f / p->sr;
    phase_set_params(p, &p->target);
    phase_reset(p);
}

void phase_reset(phase_t *p)
{
    memset(p->voice, 0, sizeof p->voice);
    memset(p->ghost, 0, sizeof p->ghost);
    for (int i = 0; i < PHASE_VOICES; i++)
        voice_pan(i, &p->voice[i]);
    for (int i = 0; i < PHASE_GHOSTS; i++)
        voice_pan(i, &p->ghost[i]);
    p->s_a = p->target.a;
    p->s_b = p->target.b;
    p->s_r = p->target.r;
    p->s_w = p->target.w;
    p->s_level = p->target.level;
    p->bend = p->bend_s = 0.0f;
    p->mod = p->mod_s = 0.0f;
    p->pedal = 0;
    p->counter = 0;
    p->dc_x[0] = p->dc_x[1] = p->dc_y[0] = p->dc_y[1] = 0.0f;
    p->act_peak = 0.0f;
    p->ui_voices = 0;
}

void phase_set_params(phase_t *p, const phase_params *q)
{
    phase_params *t = &p->target;
    t->a = clampf(q->a, 0.0f, 1.0f);
    t->b = clampf(q->b, 0.0f, 1.0f);
    t->r = clampf(q->r, 0.0f, 1.0f);
    t->w = clampf(q->w, 0.0f, 1.0f);
    t->attack = clampf(q->attack, 0.001f, 5.0f);
    t->decay = clampf(q->decay, 0.005f, 5.0f);
    t->sustain = clampf(q->sustain, 0.0f, 1.0f);
    t->release = clampf(q->release, 0.005f, 8.0f);
    t->level = clampf(q->level, 0.0f, 1.0f);
    t->bend_range = clampf(q->bend_range, 0.0f, 12.0f);

    p->att_inc = 1.0f / (t->attack * p->sr);
    p->dec_c = expf(-4.6f / (t->decay * p->sr));
    p->rel_c = expf(-6.9f / (t->release * p->sr));
    p->fade_c = expf(-9.2f / (FADE_SECONDS * p->sr));
}

/* ---- voice allocation ---- */

static void to_ghost(phase_t *p, const phase_voice *v)
{
    if (v->stage == ST_OFF || v->env < END_LEVEL)
        return;
    int slot = -1;
    unsigned long oldest = ~0ul;
    for (int i = 0; i < PHASE_GHOSTS; i++) {
        if (p->ghost[i].stage == ST_OFF) {
            slot = i;
            break;
        }
        if (p->ghost[i].age < oldest) {
            oldest = p->ghost[i].age;
            slot = i;
        }
    }
    p->ghost[slot] = *v;
    p->ghost[slot].stage = ST_FADE;
}

static int pick_voice(phase_t *p, int note)
{
    for (int i = 0; i < PHASE_VOICES; i++)
        if (p->voice[i].stage != ST_OFF && p->voice[i].note == note)
            return i;  /* same key again: restart that voice */
    for (int i = 0; i < PHASE_VOICES; i++)
        if (p->voice[i].stage == ST_OFF)
            return i;
    /* steal the quietest voice that is already releasing, else the oldest */
    int best = -1;
    float best_env = 2.0f;
    for (int i = 0; i < PHASE_VOICES; i++)
        if (p->voice[i].stage == ST_RELEASE && p->voice[i].env < best_env) {
            best_env = p->voice[i].env;
            best = i;
        }
    if (best >= 0)
        return best;
    best = 0;
    for (int i = 1; i < PHASE_VOICES; i++)
        if (p->voice[i].age < p->voice[best].age)
            best = i;
    return best;
}

void phase_note_on(phase_t *p, int note, float velocity)
{
    if (note < 0 || note > 127)
        return;
    const int i = pick_voice(p, note);
    phase_voice *v = &p->voice[i];
    to_ghost(p, v);
    v->stage = ST_ATTACK;
    v->note = note;
    v->sustained = 0;
    v->age = ++p->counter;
    v->vel = clampf(velocity, 0.0f, 1.0f);
    v->freq = phase_note_hz(note);
    v->phase = 0.0f;  /* every shape is 0 at phase 0, so a new voice starts from silence */
    v->env = 0.0f;
}

static void release_voice(phase_voice *v)
{
    if (v->stage == ST_ATTACK || v->stage == ST_SUSTAIN)
        v->stage = ST_RELEASE;
}

void phase_note_off(phase_t *p, int note)
{
    for (int i = 0; i < PHASE_VOICES; i++) {
        phase_voice *v = &p->voice[i];
        if (v->note != note || (v->stage != ST_ATTACK && v->stage != ST_SUSTAIN) || v->sustained)
            continue;
        if (p->pedal)
            v->sustained = 1;
        else
            release_voice(v);
    }
}

void phase_pitch_bend(phase_t *p, float bend)
{
    p->bend = clampf(bend, -1.0f, 1.0f);
}

void phase_mod_wheel(phase_t *p, float value)
{
    p->mod = clampf(value, 0.0f, 1.0f);
}

void phase_sustain(phase_t *p, int down)
{
    p->pedal = down ? 1 : 0;
    if (!p->pedal)
        for (int i = 0; i < PHASE_VOICES; i++)
            if (p->voice[i].sustained) {
                p->voice[i].sustained = 0;
                release_voice(&p->voice[i]);
            }
}

void phase_all_off(phase_t *p)
{
    p->pedal = 0;
    for (int i = 0; i < PHASE_VOICES; i++) {
        p->voice[i].sustained = 0;
        release_voice(&p->voice[i]);
    }
}

/* ---- rendering ---- */

/* Add one voice's next n samples into the chunk accumulators. */
static void render_voice(phase_t *p, phase_voice *v, int n, const float *a, const float *b, const float *r,
                         const float *w, const float *mult, float *accl, float *accr)
{
    const float f0 = v->freq;  /* the brightness limit follows the key, not the bend, so output does not depend on chunking */
    /* Keep harmonics and the resonant partial below Nyquist: full bend up to sr / BRIGHT_DIV, tapering above it, and
     * the resonant ratio range shrunk so the partial and its window sidebands sit under about 0.45 sample rates, and the
     * window ramp tapered above sr / WINDOW_DIV. */
    const float bright = clampf(p->sr / (f0 * BRIGHT_DIV), 0.1f, 1.0f);
    const float nmax = 0.45f * p->sr / f0 - 5.0f;
    const float wbright = clampf(p->sr / (f0 * WINDOW_DIV), 0.2f, 1.0f);
    const float inv_sr = 1.0f / p->sr;
    const float sus = p->target.sustain;
    float ph = v->phase, env = v->env;
    int stage = v->stage;

    for (int i = 0; i < n; i++) {
        const float y = shape_limited(ph, a[i], b[i], r[i], w[i] * wbright, bright, nmax) * env * v->vel * VOICE_GAIN;
        accl[i] += y * v->gl;
        accr[i] += y * v->gr;

        float inc = v->freq * mult[i] * inv_sr;
        if (inc > 0.49f)
            inc = 0.49f;
        ph += inc;
        if (ph >= 1.0f)
            ph -= 1.0f;

        switch (stage) {
        case ST_ATTACK:
            env += p->att_inc;
            if (env >= 1.0f) {
                env = 1.0f;
                stage = ST_SUSTAIN;
            }
            break;
        case ST_SUSTAIN:
            env = sus + (env - sus) * p->dec_c;
            break;
        case ST_RELEASE:
            env *= p->rel_c;
            break;
        default:
            env *= p->fade_c;
            break;
        }
        if (stage != ST_ATTACK && env < END_LEVEL && !(stage == ST_SUSTAIN && sus >= END_LEVEL)) {
            stage = ST_OFF;
            env = 0.0f;
            break;
        }
    }
    v->phase = ph;
    v->env = env;
    v->stage = stage;
}

static float soft_clip(float x)
{
    const float ax = fabsf(x);
    if (ax <= KNEE)
        return x;
    const float y = KNEE + (1.0f - KNEE) * tanhf((ax - KNEE) / (1.0f - KNEE));
    return x < 0.0f ? -y : y;
}

void phase_render(phase_t *p, float *left, float *right, int frames)
{
    const phase_params *t = &p->target;
    int done = 0;
    while (done < frames) {
        const int n = frames - done > PHASE_CHUNK ? PHASE_CHUNK : frames - done;
        float a[PHASE_CHUNK], b[PHASE_CHUNK], r[PHASE_CHUNK], w[PHASE_CHUNK], mult[PHASE_CHUNK], lv[PHASE_CHUNK];
        float *L = left + done, *R = right + done;

        for (int i = 0; i < n; i++) {
            /* one-pole smoothing, snapped to the target once it is closer than any audible step */
#define SMOOTH(s, tg, c) \
    do { (s) += ((tg) - (s)) * (c); if (fabsf((tg) - (s)) < 1e-6f) (s) = (tg); } while (0)
            SMOOTH(p->s_a, t->a, p->smooth_coef);
            SMOOTH(p->s_b, t->b, p->smooth_coef);
            SMOOTH(p->s_r, t->r, p->smooth_coef);
            SMOOTH(p->s_w, t->w, p->smooth_coef);
            SMOOTH(p->s_level, t->level, p->smooth_coef);
            SMOOTH(p->bend_s, p->bend, p->bend_coef);
            SMOOTH(p->mod_s, p->mod, p->mod_coef);
#undef SMOOTH
            a[i] = clampf(p->s_a + 0.5f * p->mod_s, 0.0f, 1.0f);
            b[i] = clampf(p->s_b + 0.5f * p->mod_s, 0.0f, 1.0f);
            r[i] = p->s_r;
            w[i] = p->s_w;
            lv[i] = p->s_level;
            mult[i] = p->bend_s == 0.0f ? 1.0f : exp2f(p->bend_s * t->bend_range * (1.0f / 12.0f));
            L[i] = 0.0f;
            R[i] = 0.0f;
        }

        for (int k = 0; k < PHASE_VOICES; k++)
            if (p->voice[k].stage != ST_OFF)
                render_voice(p, &p->voice[k], n, a, b, r, w, mult, L, R);
        for (int k = 0; k < PHASE_GHOSTS; k++)
            if (p->ghost[k].stage != ST_OFF)
                render_voice(p, &p->ghost[k], n, a, b, r, w, mult, L, R);

        float peak = p->act_peak;
        for (int i = 0; i < n; i++) {
            float x[2] = {L[i] * lv[i], R[i] * lv[i]};
            for (int c = 0; c < 2; c++) {
                const float y = x[c] - p->dc_x[c] + p->dc_r * p->dc_y[c];
                p->dc_x[c] = x[c];
                p->dc_y[c] = y;
                x[c] = soft_clip(y);
            }
            L[i] = x[0];
            R[i] = x[1];
            const float m = fmaxf(fabsf(x[0]), fabsf(x[1]));
            if (m > peak)
                peak = m;
        }
        p->act_peak = peak;
        /* flush the DC blocker state so it can never reach denormal range */
        for (int c = 0; c < 2; c++)
            if (fabsf(p->dc_y[c]) < 1e-20f)
                p->dc_y[c] = 0.0f;
        done += n;
    }

    int count = 0;
    for (int k = 0; k < PHASE_VOICES; k++)
        if (p->voice[k].stage != ST_OFF)
            count++;
    p->ui_voices = count;
}

float phase_take_activity(phase_t *p, int *voices)
{
    const float peak = p->act_peak;
    p->act_peak = 0.0f;
    if (voices)
        *voices = p->ui_voices;
    return peak;
}
