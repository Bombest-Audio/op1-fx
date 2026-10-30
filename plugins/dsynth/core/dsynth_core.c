/* DSynth DSP core. See dsynth_core.h and docs/DSP.md.
 *
 * Signal path per voice:
 *   oscillator 2 (sine to saw, up to three octaves above) x envelope 2 x depth -> phase modulation of
 *   oscillator 1 (sine to saw) x envelope 1 x velocity x amp envelope -> two-stage state-variable filter
 *   -> pan -> sum of voices -> output gain -> soft limiter.
 * Envelope 2 also drives a short pitch drop on oscillator 1 (the drum thump), envelope 1 also opens the filter.
 *
 * Everything that changes with time is either per sample or ramped linearly across a control tick of
 * DSYNTH_TICK samples. The ramp position is derived from the tick position, never accumulated, so the output is
 * bit-identical however a stretch of audio is split into render calls.
 */
#include "dsynth_core.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f
#define TWO_PI_F 6.28318530717959f
#define DECAY_LN 6.907755f      /* ln(1000): 60 dB */
#define SWEEP_SEMITONES 7.0f    /* pitch drop at the start of a note, scaled by envelope 2 to the sixth power */
#define VOICE_DEAD 1e-4f        /* an envelope below this (-80 dB) is silent for good */
#define STEAL_FADE_SECONDS 0.002f
#define SMOOTH_SECONDS 0.005f
#define LIMIT_KNEE 0.6f
#define LIMIT_CEIL 0.98f

static inline float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

/* ------------------------------------------------------------------------------------------ knob mappings */

float dsynth_decay_seconds(float knob)
{
    const float u = clampf(2.0f * knob, 0.0f, 1.0f);
    return 0.015f * powf(2.0f / 0.015f, u);
}

float dsynth_attack_seconds(float knob)
{
    if (knob <= 0.5f)
        return 0.001f;
    return 0.001f * powf(500.0f, clampf(2.0f * (knob - 0.5f), 0.0f, 1.0f));
}

float dsynth_mod_depth(float norm)
{
    const float n = clampf(norm, 0.0f, 1.0f);
    return 9.0f * n * n;
}

float dsynth_osc2_ratio(float norm)
{
    return exp2f(3.0f * clampf(norm, 0.0f, 1.0f));
}

float dsynth_cutoff_ratio(float norm)
{
    return 2.0f * exp2f(5.0f * clampf(norm, 0.0f, 1.0f));
}

float dsynth_level_gain(float norm)
{
    const float n = clampf(norm, 0.0f, 1.0f);
    return 0.625f * n * n;
}

const char *dsynth_filter_name(int mode)
{
    static const char *const names[DSYNTH_FILTER_MODES] = {"OFF", "LP12", "LP24", "BAND", "BAND Q", "HP12", "NOTCH", "LP RES"};
    return names[mode < 0 ? 0 : (mode >= DSYNTH_FILTER_MODES ? DSYNTH_FILTER_MODES - 1 : mode)];
}

/* Filter mix per mode: dry, low-pass 1, band-pass at unity peak, high-pass, low-pass 2 (the second stage runs on the
 * first stage's low-pass output). k_damp is the damping 1/Q shared by both stages. The resonant mode is trimmed so
 * its peak does not jump 15 dB over the others. */
static const float k_mix[DSYNTH_FILTER_MODES][5] = {
    {1.0f, 0.0f, 0.0f, 0.0f, 0.0f},  /* 0 off */
    {0.0f, 1.0f, 0.0f, 0.0f, 0.0f},  /* 1 LP12 */
    {0.0f, 0.0f, 0.0f, 0.0f, 1.0f},  /* 2 LP24 */
    {0.0f, 0.0f, 1.0f, 0.0f, 0.0f},  /* 3 band */
    {0.0f, 0.0f, 1.0f, 0.0f, 0.0f},  /* 4 narrow band */
    {0.0f, 0.0f, 0.0f, 1.0f, 0.0f},  /* 5 HP12 */
    {0.0f, 1.0f, 0.0f, 1.0f, 0.0f},  /* 6 notch */
    {0.0f, 0.45f, 0.0f, 0.0f, 0.0f}, /* 7 resonant LP */
};
static const float k_damp[DSYNTH_FILTER_MODES] = {1.414f, 1.414f, 1.414f, 0.667f, 0.167f, 1.414f, 0.833f, 0.167f};

/* ------------------------------------------------------------------------------------------ oscillator */

/* Sine from a phase in cycles. Folds to a quarter wave and runs an odd polynomial: error about 4e-6. */
static inline float sin_cycles(float x)
{
    x -= (float)(int)x;
    if (x > 0.5f)
        x -= 1.0f;
    else if (x < -0.5f)
        x += 1.0f;
    if (x > 0.25f)
        x = 0.5f - x;
    else if (x < -0.25f)
        x = -0.5f - x;
    const float z = x * TWO_PI_F, z2 = z * z;
    return z * (1.0f + z2 * (-1.6666667e-1f + z2 * (8.3333333e-3f + z2 * (-1.9841270e-4f + z2 * 2.7557319e-6f))));
}

/* Band-limited step correction, t in [0,1), dt the phase increment. */
static inline float polyblep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

/* Shape morph: sine (0) to square (0.5) to saw (1). t is the phase in [0,1), dt the increment in cycles per sample. */
static inline float osc_shape(float t, float dt, float w)
{
    const float s = sin_cycles(t);
    if (w <= 0.0f)
        return s;
    float t2 = t + 0.5f;
    if (t2 >= 1.0f)
        t2 -= 1.0f;
    const float sq = (t < 0.5f ? 1.0f : -1.0f) + polyblep(t, dt) - polyblep(t2, dt);
    if (w < 0.5f)
        return s + (sq - s) * (2.0f * w);
    const float saw = 2.0f * t - 1.0f - polyblep(t, dt);
    return sq + (saw - sq) * (2.0f * w - 1.0f);
}

/* Transparent below LIMIT_KNEE, then a tanh shoulder with matching slope that never reaches LIMIT_CEIL. */
static inline float soft_limit(float x)
{
    const float a = fabsf(x);
    if (a <= LIMIT_KNEE)
        return x;
    const float room = LIMIT_CEIL - LIMIT_KNEE;
    const float y = LIMIT_KNEE + room * tanhf((a - LIMIT_KNEE) * (1.0f / room));
    return x < 0.0f ? -y : y;
}

/* ------------------------------------------------------------------------------------------ control ticks */

static float master_gain(const dsynth_t *d)
{
    return dsynth_level_gain(d->s_level);
}

/* Rebuild everything derived from the smoothed controls and the targets. */
static void derive(dsynth_t *d)
{
    const dsynth_params *t = &d->target;
    const float sr = d->sr;
    const float mod_eff = d->s_mod + d->s_wheel * (1.0f - d->s_mod);

    d->wave1 = d->s_wave;
    d->wave2 = d->s_wave2;
    d->ratio2 = dsynth_osc2_ratio(d->s_osc2_pitch);
    d->cut_rel = dsynth_cutoff_ratio(d->s_cutoff);
    d->idx_base = dsynth_mod_depth(mod_eff);
    d->bend_mul = exp2f(d->s_bend * t->bend_range * (1.0f / 12.0f));

    d->ainc1 = 1.0f / (dsynth_attack_seconds(t->decay) * sr);
    d->dc1 = expf(-DECAY_LN / (dsynth_decay_seconds(t->decay) * sr));
    d->ainc2 = 1.0f / (dsynth_attack_seconds(t->osc2_decay) * sr);
    d->dc2 = expf(-DECAY_LN / (dsynth_decay_seconds(t->osc2_decay) * sr));

    d->aa_inc = 1.0f / (t->amp_attack * sr);
    d->ad_coef = expf(-DECAY_LN / (t->amp_decay * sr));
    d->ar_coef = expf(-DECAY_LN / (t->amp_release * sr));
    d->a_sus = t->amp_sustain;
    d->fade_step = 1.0f / (STEAL_FADE_SECONDS * sr);
}

static int mode_of(const dsynth_t *d)
{
    const int m = d->target.filter_mode;
    return m < 0 ? 0 : (m >= DSYNTH_FILTER_MODES ? DSYNTH_FILTER_MODES - 1 : m);
}

/* Jump every smoothed control to its target. */
static void snap(dsynth_t *d)
{
    const dsynth_params *t = &d->target;
    d->s_mod = t->mod;
    d->s_wave = t->wave;
    d->s_wave2 = t->osc2_wave;
    d->s_osc2_pitch = t->osc2_pitch;
    d->s_cutoff = t->cutoff;
    d->s_level = t->level;
    d->s_bend = d->bend_norm;
    d->s_wheel = d->wheel;
    const int m = mode_of(d);
    for (int j = 0; j < 5; j++) {
        d->s_w[j] = k_mix[m][j];
        d->w0[j] = d->s_w[j];
        d->dw[j] = 0.0f;
    }
    d->s_k = k_damp[m];
    derive(d);
    d->m0 = master_gain(d);
    d->dm = 0.0f;
}

/* Called at the start of every tick: one-pole smoothing, then ramps from the old value to the new across the tick. */
static void global_tick(dsynth_t *d)
{
    const dsynth_params *t = &d->target;
    const float c = d->c_smooth;
    const float inv = 1.0f / (float)DSYNTH_TICK;
    const float prev_master = master_gain(d);
    float prev_w[5];
    memcpy(prev_w, d->s_w, sizeof prev_w);

    d->s_mod += c * (t->mod - d->s_mod);
    d->s_wave += c * (t->wave - d->s_wave);
    d->s_wave2 += c * (t->osc2_wave - d->s_wave2);
    d->s_osc2_pitch += c * (t->osc2_pitch - d->s_osc2_pitch);
    d->s_cutoff += c * (t->cutoff - d->s_cutoff);
    d->s_level += c * (t->level - d->s_level);
    d->s_bend += c * (d->bend_norm - d->s_bend);
    d->s_wheel += c * (d->wheel - d->s_wheel);
    const int m = mode_of(d);
    for (int j = 0; j < 5; j++)
        d->s_w[j] += c * (k_mix[m][j] - d->s_w[j]);
    d->s_k += c * (k_damp[m] - d->s_k);
    derive(d);

    for (int j = 0; j < 5; j++) {
        d->w0[j] = prev_w[j];
        d->dw[j] = (d->s_w[j] - prev_w[j]) * inv;
    }
    d->m0 = prev_master;
    d->dm = (master_gain(d) - prev_master) * inv;
}

static void voice_kill(dsynth_voice *v)
{
    memset(v, 0, sizeof *v);
}

/* Per-voice values that follow the envelopes and the pitch bend: oscillator increments and filter coefficients.
 * fresh jumps straight to the target (a note just started), otherwise the increments ramp across the next tick. */
static void voice_tick(dsynth_t *d, dsynth_voice *v, int fresh)
{
    const float sr = d->sr;
    const float top = 0.45f * sr;
    const float s2 = v->e2 * v->e2;
    const float s6 = s2 * s2 * s2;
    const float base = v->f0 * d->bend_mul;
    const float f1 = clampf(base * exp2f(SWEEP_SEMITONES * (1.0f / 12.0f) * s6), 1.0f, top);
    const float f2 = clampf(f1 * d->ratio2, 1.0f, top);
    const float t1 = f1 / sr, t2 = f2 / sr;
    if (fresh) {
        v->inc1_0 = t1;
        v->inc2_0 = t2;
        v->dinc1 = v->dinc2 = 0.0f;
    } else {
        v->inc1_0 += v->dinc1 * (float)DSYNTH_TICK;
        v->inc2_0 += v->dinc2 * (float)DSYNTH_TICK;
        v->dinc1 = (t1 - v->inc1_0) * (1.0f / (float)DSYNTH_TICK);
        v->dinc2 = (t2 - v->inc2_0) * (1.0f / (float)DSYNTH_TICK);
    }

    const float fc = clampf(base * d->cut_rel * exp2f(2.0f * v->e1), 20.0f, 0.4f * sr);
    const float g = tanf(PI_F * fc / sr);
    const float k = d->s_k;
    v->a1 = 1.0f / (1.0f + g * (g + k));
    v->a2 = g * v->a1;
    v->a3 = g * v->a2;
}

static void voice_start(dsynth_t *d, dsynth_voice *v, int note, int vel, int held)
{
    memset(v, 0, sizeof *v);
    v->alive = 1;
    v->note = note;
    v->held = held;
    v->age = ++d->serial;
    v->fade = 1.0f;
    v->vel = (float)vel * (1.0f / 127.0f);
    v->f0 = 440.0f * exp2f((float)(note - 69) * (1.0f / 12.0f));
    v->st1 = v->st2 = 1;
    v->ast = 1;
    const float pan = clampf((float)(note - 60) * (1.0f / 60.0f), -1.0f, 1.0f) * 0.3f;
    v->pan_l = 1.0f - 0.5f * pan;
    v->pan_r = 1.0f + 0.5f * pan;
    voice_tick(d, v, 1);
    if (!held) {
        if (d->pedal)
            v->sustained = 1;
        else
            v->ast = 3;
    }
}

static void voice_render(dsynth_t *d, dsynth_voice *v, float *lacc, float *racc, int n)
{
    const int pos0 = d->tick_pos;
    for (int i = 0; i < n; i++) {
        const float fp = (float)(pos0 + i);
        float fg = 1.0f;
        if (v->stealing) {
            v->fade -= v->fade_step;
            if (v->fade <= 0.0f)
                voice_start(d, v, v->pend_note, v->pend_vel, v->pend_held);
            else
                fg = v->fade;
        }

        /* envelopes. A voice is not retired while it is being stolen: its fade ends it. */
        if (v->st1 == 1) {
            v->e1 += d->ainc1;
            if (v->e1 >= 1.0f) {
                v->e1 = 1.0f;
                v->st1 = 2;
            }
        } else {
            v->e1 *= d->dc1;
            if (v->e1 < VOICE_DEAD && !v->stealing) {
                voice_kill(v);
                return;
            }
        }
        if (v->st2 == 1) {
            v->e2 += d->ainc2;
            if (v->e2 >= 1.0f) {
                v->e2 = 1.0f;
                v->st2 = 2;
            }
        } else {
            v->e2 *= d->dc2;
            if (v->e2 < 1e-6f)
                v->e2 = 0.0f;
        }
        if (v->ast == 1) {
            v->ae += d->aa_inc;
            if (v->ae >= 1.0f) {
                v->ae = 1.0f;
                v->ast = 2;
            }
        } else if (v->ast == 2) {
            if (d->a_sus < 1.0f)
                v->ae = d->a_sus + (v->ae - d->a_sus) * d->ad_coef;
            if (v->ae < VOICE_DEAD && d->a_sus < VOICE_DEAD && !v->stealing) {
                voice_kill(v);
                return;
            }
        } else {
            v->ae *= d->ar_coef;
            if (v->ae < VOICE_DEAD && !v->stealing) {
                voice_kill(v);
                return;
            }
        }

        /* oscillators: 2 phase-modulates 1 */
        const float inc1 = v->inc1_0 + v->dinc1 * fp;
        const float inc2 = v->inc2_0 + v->dinc2 * fp;
        v->p2 += inc2;
        if (v->p2 >= 1.0f)
            v->p2 -= 1.0f;
        v->p1 += inc1;
        if (v->p1 >= 1.0f)
            v->p1 -= 1.0f;
        float ph = v->p1;
        const float depth = d->idx_base * (0.5f + 0.5f * v->vel) * v->e2;
        if (depth > 1e-4f)
            ph += depth * (1.0f / TWO_PI_F) * osc_shape(v->p2, inc2, d->wave2);
        ph -= (float)(int)ph;
        if (ph < 0.0f)
            ph += 1.0f;
        if (ph >= 1.0f)
            ph = 0.0f;
        const float x = osc_shape(ph, inc1, d->wave1) * v->e1 * v->vel * v->ae;

        /* filter: two state-variable stages (trapezoidal), the second fed by the first stage's low-pass */
        const float k = d->s_k;
        const float v3a = x - v->f1_ic2;
        const float v1a = v->a1 * v->f1_ic1 + v->a2 * v3a;
        const float v2a = v->f1_ic2 + v->a2 * v->f1_ic1 + v->a3 * v3a;
        v->f1_ic1 = 2.0f * v1a - v->f1_ic1;
        v->f1_ic2 = 2.0f * v2a - v->f1_ic2;
        const float hp = x - k * v1a - v2a;
        const float v3b = v2a - v->f2_ic2;
        const float v1b = v->a1 * v->f2_ic1 + v->a2 * v3b;
        const float v2b = v->f2_ic2 + v->a2 * v->f2_ic1 + v->a3 * v3b;
        v->f2_ic1 = 2.0f * v1b - v->f2_ic1;
        v->f2_ic2 = 2.0f * v2b - v->f2_ic2;
        const float y = ((d->w0[0] + d->dw[0] * fp) * x + (d->w0[1] + d->dw[1] * fp) * v2a +
                         (d->w0[2] + d->dw[2] * fp) * (k * v1a) + (d->w0[3] + d->dw[3] * fp) * hp +
                         (d->w0[4] + d->dw[4] * fp) * v2b) * fg;
        lacc[i] += y * v->pan_l;
        racc[i] += y * v->pan_r;
    }
}

/* ------------------------------------------------------------------------------------------ public API */

void dsynth_default_params(dsynth_params *p)
{
    p->mod = DSYNTH_DEFAULT_MOD;
    p->wave = DSYNTH_DEFAULT_WAVE;
    p->decay = DSYNTH_DEFAULT_DECAY;
    p->filter_mode = DSYNTH_DEFAULT_FILTER_MODE;
    p->osc2_pitch = DSYNTH_DEFAULT_OSC2_PITCH;
    p->osc2_wave = DSYNTH_DEFAULT_OSC2_WAVE;
    p->osc2_decay = DSYNTH_DEFAULT_OSC2_DECAY;
    p->cutoff = DSYNTH_DEFAULT_CUTOFF;
    p->amp_attack = DSYNTH_DEFAULT_AMP_ATTACK;
    p->amp_decay = DSYNTH_DEFAULT_AMP_DECAY;
    p->amp_sustain = DSYNTH_DEFAULT_AMP_SUSTAIN;
    p->amp_release = DSYNTH_DEFAULT_AMP_RELEASE;
    p->level = DSYNTH_DEFAULT_LEVEL;
    p->bend_range = DSYNTH_DEFAULT_BEND_RANGE;
}

static void clamp_params(dsynth_params *p)
{
    p->mod = clampf(p->mod, 0.0f, 1.0f);
    p->wave = clampf(p->wave, 0.0f, 1.0f);
    p->decay = clampf(p->decay, 0.0f, 1.0f);
    p->filter_mode = p->filter_mode < 0 ? 0 : (p->filter_mode >= DSYNTH_FILTER_MODES ? DSYNTH_FILTER_MODES - 1 : p->filter_mode);
    p->osc2_pitch = clampf(p->osc2_pitch, 0.0f, 1.0f);
    p->osc2_wave = clampf(p->osc2_wave, 0.0f, 1.0f);
    p->osc2_decay = clampf(p->osc2_decay, 0.0f, 1.0f);
    p->cutoff = clampf(p->cutoff, 0.0f, 1.0f);
    p->amp_attack = clampf(p->amp_attack, 0.0005f, 10.0f);
    p->amp_decay = clampf(p->amp_decay, 0.005f, 20.0f);
    p->amp_sustain = clampf(p->amp_sustain, 0.0f, 1.0f);
    p->amp_release = clampf(p->amp_release, 0.005f, 20.0f);
    p->level = clampf(p->level, 0.0f, 1.0f);
    p->bend_range = clampf(p->bend_range, 0.0f, 24.0f);
}

void dsynth_set_params(dsynth_t *d, const dsynth_params *p)
{
    d->target = *p;
    clamp_params(&d->target);
}

void dsynth_init(dsynth_t *d, float sample_rate)
{
    memset(d, 0, sizeof *d);
    d->sr = clampf(sample_rate, 8000.0f, 384000.0f);
    d->c_smooth = 1.0f - expf(-(float)DSYNTH_TICK / (SMOOTH_SECONDS * d->sr));
    dsynth_default_params(&d->target);
    snap(d);
}

void dsynth_reset(dsynth_t *d)
{
    memset(d->voice, 0, sizeof d->voice);
    d->pedal = 0;
    d->bend_norm = 0.0f;
    d->wheel = 0.0f;
    d->tick_pos = 0;
    d->act_peak = 0.0f;
    snap(d);
}

static int pick_voice(dsynth_t *d, int note)
{
    for (int i = 0; i < DSYNTH_VOICES; i++)
        if (d->voice[i].alive && d->voice[i].note == note && !d->voice[i].stealing)
            return i; /* the same key again: reuse its voice */
    for (int i = 0; i < DSYNTH_VOICES; i++)
        if (!d->voice[i].alive)
            return i;
    /* All busy: take the quietest voice, preferring released ones and avoiding voices already being stolen. */
    int best = 0;
    float best_score = 1e30f;
    for (int i = 0; i < DSYNTH_VOICES; i++) {
        const dsynth_voice *v = &d->voice[i];
        float score = v->e1 * v->ae * v->vel;
        if (v->held || v->sustained)
            score += 1000.0f;
        if (v->stealing)
            score += 10000.0f;
        if (score < best_score || (score == best_score && v->age < d->voice[best].age)) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

void dsynth_note_on(dsynth_t *d, int note, int velocity)
{
    if (velocity <= 0) {
        dsynth_note_off(d, note);
        return;
    }
    note = note < 0 ? 0 : (note > 127 ? 127 : note);
    velocity = velocity > 127 ? 127 : velocity;
    dsynth_voice *v = &d->voice[pick_voice(d, note)];
    if (!v->alive) {
        voice_start(d, v, note, velocity, 1);
        return;
    }
    /* Busy voice (a steal, or the same key struck again): fade the old sound out over about 2 ms, then start the new
     * note in the same voice. The pending note is kept until the fade ends. */
    if (!v->stealing) {
        v->stealing = 1;
        v->fade = 1.0f;
        v->fade_step = d->fade_step;
    }
    v->pend_note = note;
    v->pend_vel = velocity;
    v->pend_held = 1;
}

void dsynth_note_off(dsynth_t *d, int note)
{
    for (int i = 0; i < DSYNTH_VOICES; i++) {
        dsynth_voice *v = &d->voice[i];
        if (!v->alive)
            continue;
        if (v->stealing) {
            if (v->pend_note == note)
                v->pend_held = 0; /* released before it even started: it will start in its release */
            continue;
        }
        if (v->note != note || !v->held)
            continue;
        v->held = 0;
        if (d->pedal)
            v->sustained = 1;
        else
            v->ast = 3;
    }
}

void dsynth_pitch_bend(dsynth_t *d, float norm)
{
    d->bend_norm = clampf(norm, -1.0f, 1.0f);
}

void dsynth_mod_wheel(dsynth_t *d, float norm)
{
    d->wheel = clampf(norm, 0.0f, 1.0f);
}

void dsynth_sustain_pedal(dsynth_t *d, int down)
{
    d->pedal = down ? 1 : 0;
    if (down)
        return;
    for (int i = 0; i < DSYNTH_VOICES; i++) {
        dsynth_voice *v = &d->voice[i];
        if (v->alive && v->sustained) {
            v->sustained = 0;
            if (!v->stealing)
                v->ast = 3;
        }
    }
}

void dsynth_all_off(dsynth_t *d)
{
    d->pedal = 0;
    for (int i = 0; i < DSYNTH_VOICES; i++) {
        dsynth_voice *v = &d->voice[i];
        if (!v->alive)
            continue;
        v->held = 0;
        v->sustained = 0;
        if (v->stealing)
            v->pend_held = 0;
        else
            v->ast = 3;
    }
}

void dsynth_render(dsynth_t *d, float *l, float *r, int frames)
{
    int done = 0;
    while (done < frames) {
        if (d->tick_pos == 0) {
            global_tick(d);
            for (int i = 0; i < DSYNTH_VOICES; i++)
                if (d->voice[i].alive)
                    voice_tick(d, &d->voice[i], 0);
        }
        int n = DSYNTH_TICK - d->tick_pos;
        if (n > frames - done)
            n = frames - done;
        float *lo = l + done, *ro = r + done;
        for (int i = 0; i < n; i++)
            lo[i] = ro[i] = 0.0f;
        for (int i = 0; i < DSYNTH_VOICES; i++)
            if (d->voice[i].alive)
                voice_render(d, &d->voice[i], lo, ro, n);
        float peak = d->act_peak;
        for (int i = 0; i < n; i++) {
            const float m = d->m0 + d->dm * (float)(d->tick_pos + i);
            const float a = soft_limit(lo[i] * m), b = soft_limit(ro[i] * m);
            lo[i] = a;
            ro[i] = b;
            const float pa = fabsf(a), pb = fabsf(b);
            if (pa > peak)
                peak = pa;
            if (pb > peak)
                peak = pb;
        }
        d->act_peak = peak;
        d->tick_pos += n;
        if (d->tick_pos >= DSYNTH_TICK)
            d->tick_pos = 0;
        done += n;
    }
}

int dsynth_active_voices(const dsynth_t *d)
{
    int n = 0;
    for (int i = 0; i < DSYNTH_VOICES; i++)
        n += d->voice[i].alive ? 1 : 0;
    return n;
}

void dsynth_take_activity(dsynth_t *d, dsynth_activity *out)
{
    out->peak = d->act_peak;
    out->voices = dsynth_active_voices(d);
    out->notes = d->serial;
    d->act_peak = 0.0f;
}
