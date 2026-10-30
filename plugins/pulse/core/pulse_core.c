#include "pulse_core.h"

#include <math.h>
#include <string.h>

enum { V_IDLE = 0, V_ATTACK, V_DECAY, V_SUSTAIN, V_RELEASE };

#define LN_1000 6.907755f
#define SHAPE_PW_MIN 0.04f    /* thinnest resting pulse (duty cycle) */
#define VOICE_GAIN 0.14f      /* one voice at full velocity and level; eight at once stay under the soft ceiling's knee at the default level */
#define STEAL_FADE_S 0.0025f  /* fade-out before a stolen voice restarts */
#define CEIL_T 0.70f          /* soft ceiling is linear below this ... */
#define CEIL_K 0.28f          /* ... and approaches CEIL_T + CEIL_K = 0.98 above it */

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float flush(float v)
{
    return fabsf(v) < 1e-20f ? 0.0f : v;
}

float pulse_attack_s(float n)
{
    return 0.0005f * powf(8000.0f, clampf(n, 0.0f, 1.0f));
}

float pulse_decay_s(float n)
{
    return 0.005f * powf(800.0f, clampf(n, 0.0f, 1.0f));
}

float pulse_release_s(float n)
{
    return 0.005f * powf(1200.0f, clampf(n, 0.0f, 1.0f));
}

float pulse_rate_hz(float n)
{
    return 0.2f * exp2f(7.0f * clampf(n, 0.0f, 1.0f));
}

float pulse_detune_cents(float n)
{
    return 100.0f * clampf(n, -1.0f, 1.0f);
}

/* duty cycle at the resting point: SHAPE 0 is 50 percent, SHAPE 1 is SHAPE_PW_MIN */
static float shape_to_pw(float shape)
{
    return SHAPE_PW_MIN + (0.5f - SHAPE_PW_MIN) * (1.0f - clampf(shape, 0.0f, 1.0f));
}

float pulse_duty_pct(float shape)
{
    return 100.0f * shape_to_pw(shape);
}

/* Soft ceiling. Linear up to CEIL_T, then a tanh knee that stays under CEIL_T + CEIL_K. */
static inline float soft_ceiling(float x)
{
    const float a = fabsf(x);
    if (a <= CEIL_T)
        return x;
    float z = (a - CEIL_T) / CEIL_K;
    z = z > 3.0f ? 3.0f : z; /* Pade tanh is exact to 1.0 at z = 3 and is held there */
    const float z2 = z * z;
    const float t = z * (27.0f + z2) / (27.0f + 9.0f * z2);
    const float y = CEIL_T + CEIL_K * t;
    return x < 0.0f ? -y : y;
}

/* Two-sample polynomial band-limited step. t is the phase distance past an edge, dt the phase increment. */
static inline float poly_blep(float t, float dt)
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

/* Band-limited pulse, rising edge at phase 0 and falling edge at phase pw (pw in (0, 0.5]). The mean is removed,
 * and the level is eased down for thin pulses so they do not jump out at a chord: the linear factor is 1 at
 * pw = 0.5 and about 0.72 at pw = 0.04. */
static inline float pulse_osc(float t, float dt, float pw)
{
    float v = t < pw ? 1.0f : -1.0f;
    v += poly_blep(t, dt);
    float t2 = t - pw;
    if (t2 < 0.0f)
        t2 += 1.0f;
    v -= poly_blep(t2, dt);
    const float wide = 1.0f - 2.0f * pw;
    return (v + wide) * (1.0f - 0.304f * wide);
}

static float time_coef(float seconds, float sr)
{
    return expf(-LN_1000 / (seconds * sr));
}

void pulse_init(pulse_t *p, float sample_rate)
{
    memset(p, 0, sizeof *p);
    p->sr = sample_rate > 8000.0f ? sample_rate : 44100.0f;
    p->target.shape = PULSE_DEFAULT_SHAPE;
    p->target.sweep = PULSE_DEFAULT_SWEEP;
    p->target.rate = PULSE_DEFAULT_RATE;
    p->target.detune = PULSE_DEFAULT_DETUNE;
    p->target.attack = PULSE_DEFAULT_ATTACK;
    p->target.decay = PULSE_DEFAULT_DECAY;
    p->target.sustain = PULSE_DEFAULT_SUSTAIN;
    p->target.release = PULSE_DEFAULT_RELEASE;
    p->target.level = PULSE_DEFAULT_LEVEL;
    p->target.bend_range = PULSE_DEFAULT_BEND_RANGE;
    p->smooth_coef = 1.0f - expf(-1.0f / (0.005f * p->sr));
    p->fade_dec = 1.0f / (STEAL_FADE_S * p->sr);
    for (int n = 0; n < 128; n++)
        p->note_base[n] = 440.0f * exp2f((float)(n - 69) / 12.0f) / p->sr;
    pulse_set_params(p, &p->target);
    pulse_reset(p);
}

void pulse_reset(pulse_t *p)
{
    memset(p->v, 0, sizeof p->v);
    for (int i = 0; i < PULSE_VOICES; i++)
        p->v[i].fade = 1.0f;
    p->clock = 0;
    p->bend_in = 0.0f;
    p->mod_in = 0.0f;
    p->pedal_down = 0;
    p->ctrl_pos = 0;
    p->act_peak = 0.0f;
    p->s_wbase = 1.0f - p->target.shape;
    p->s_sweep = 0.5f * p->target.sweep;
    p->s_rate = p->target.rate;
    p->s_detune = p->target.detune;
    p->s_level = p->target.level;
    p->s_bend = 0.0f;
    p->bend_ratio = 1.0f;
    p->ratio_a = exp2f(-p->s_detune * (50.0f / 1200.0f));
    p->ratio_b = exp2f(p->s_detune * (50.0f / 1200.0f));
    p->lfo_inc = pulse_rate_hz(p->s_rate) / p->sr;
}

void pulse_set_params(pulse_t *p, const pulse_params *in)
{
    pulse_params *t = &p->target;
    t->shape = clampf(in->shape, 0.0f, 1.0f);
    t->sweep = clampf(in->sweep, 0.0f, 1.0f);
    t->rate = clampf(in->rate, 0.0f, 1.0f);
    t->detune = clampf(in->detune, -1.0f, 1.0f);
    t->attack = clampf(in->attack, 0.0f, 1.0f);
    t->decay = clampf(in->decay, 0.0f, 1.0f);
    t->sustain = clampf(in->sustain, 0.0f, 1.0f);
    t->release = clampf(in->release, 0.0f, 1.0f);
    t->level = clampf(in->level, 0.0f, 1.0f);
    t->bend_range = clampf(in->bend_range, 1.0f, 12.0f);
    p->att_inc = 1.0f / (pulse_attack_s(t->attack) * p->sr);
    p->dec_coef = time_coef(pulse_decay_s(t->decay), p->sr);
    p->rel_coef = time_coef(pulse_release_s(t->release), p->sr);
}

static void set_voice_incs(const pulse_t *p, pulse_voice *v)
{
    v->inc_a = clampf(v->base * p->bend_ratio * p->ratio_a, 0.0f, 0.45f);
    v->inc_b = clampf(v->base * p->bend_ratio * p->ratio_b, 0.0f, 0.45f);
}

static void start_voice(pulse_t *p, int idx, int note, float gain, int gate)
{
    pulse_voice *v = &p->v[idx];
    v->state = V_ATTACK;
    v->note = note;
    v->gate = gate;
    v->pedal = 0;
    v->fading = 0;
    v->pending = 0;
    v->gain = gain;
    v->env = 0.0f;
    v->fade = 1.0f;
    v->base = p->note_base[note];
    set_voice_incs(p, v);
    v->ph_a = 0.0f;
    v->ph_b = 0.0f;
    v->ph_lfo = 0.125f * (float)idx;
    v->age = ++p->clock;
}

void pulse_note_on(pulse_t *p, int note, int velocity)
{
    if (velocity <= 0) {
        pulse_note_off(p, note);
        return;
    }
    note = note < 0 ? 0 : (note > 127 ? 127 : note);
    const float gain = 0.15f + 0.85f * (float)(velocity > 127 ? 127 : velocity) / 127.0f;

    int pick = -1;
    /* the same key struck again takes over its own voice */
    for (int i = 0; i < PULSE_VOICES; i++) {
        const pulse_voice *v = &p->v[i];
        if (v->state != V_IDLE && v->note == note && (v->gate || v->pedal) && !v->fading) {
            pick = i;
            break;
        }
    }
    if (pick < 0) {
        for (int i = 0; i < PULSE_VOICES; i++) {
            if (p->v[i].state == V_IDLE && !p->v[i].fading) {
                pick = i;
                break;
            }
        }
    }
    if (pick < 0) {
        /* steal: the quietest releasing voice, else the oldest held one, never one already fading if avoidable */
        float best_env = 2.0f;
        for (int i = 0; i < PULSE_VOICES; i++) {
            const pulse_voice *v = &p->v[i];
            if (v->state == V_RELEASE && !v->fading && v->env < best_env) {
                best_env = v->env;
                pick = i;
            }
        }
    }
    if (pick < 0) {
        unsigned long best_age = (unsigned long)-1;
        for (int i = 0; i < PULSE_VOICES; i++) {
            const pulse_voice *v = &p->v[i];
            if (!v->fading && v->age < best_age) {
                best_age = v->age;
                pick = i;
            }
        }
    }
    if (pick < 0) {
        unsigned long best_age = (unsigned long)-1;
        for (int i = 0; i < PULSE_VOICES; i++) {
            if (p->v[i].age < best_age) {
                best_age = p->v[i].age;
                pick = i;
            }
        }
    }

    pulse_voice *v = &p->v[pick];
    if (v->state == V_IDLE && !v->fading) {
        start_voice(p, pick, note, gain, 1);
    } else {
        v->fading = 1;
        v->pending = 1;
        v->pend_note = note;
        v->pend_gain = gain;
        v->pend_gate = 1;
        v->age = ++p->clock;
    }
}

void pulse_note_off(pulse_t *p, int note)
{
    for (int i = 0; i < PULSE_VOICES; i++) {
        pulse_voice *v = &p->v[i];
        if (v->pending && v->pend_note == note) {
            if (p->pedal_down)
                continue;
            v->pend_gate = 0;
        }
        if (v->state != V_IDLE && v->note == note && v->gate && !v->fading) {
            if (p->pedal_down)
                v->pedal = 1;
            else
                v->gate = 0;
        }
    }
}

void pulse_pitch_bend(pulse_t *p, float value)
{
    p->bend_in = clampf(value, -1.0f, 1.0f);
}

void pulse_mod_wheel(pulse_t *p, float value)
{
    p->mod_in = clampf(value, 0.0f, 1.0f);
}

void pulse_sustain_pedal(pulse_t *p, int down)
{
    p->pedal_down = down ? 1 : 0;
    if (!down) {
        for (int i = 0; i < PULSE_VOICES; i++) {
            if (p->v[i].pedal) {
                p->v[i].pedal = 0;
                p->v[i].gate = 0;
            }
        }
    }
}

void pulse_all_off(pulse_t *p)
{
    p->pedal_down = 0;
    for (int i = 0; i < PULSE_VOICES; i++) {
        pulse_voice *v = &p->v[i];
        v->gate = 0;
        v->pedal = 0;
        if (v->pending)
            v->pend_gate = 0;
    }
}

void pulse_all_sound_off(pulse_t *p)
{
    p->pedal_down = 0;
    for (int i = 0; i < PULSE_VOICES; i++) {
        pulse_voice *v = &p->v[i];
        v->gate = 0;
        v->pedal = 0;
        v->pending = 0;
        if (v->state != V_IDLE)
            v->fading = 1;
    }
}

float pulse_take_activity(pulse_t *p)
{
    const float a = p->act_peak;
    p->act_peak = 0.0f;
    return a;
}

int pulse_active_voices(const pulse_t *p)
{
    int n = 0;
    for (int i = 0; i < PULSE_VOICES; i++)
        n += (p->v[i].state != V_IDLE || p->v[i].fading) ? 1 : 0;
    return n;
}

/* Step the smoothers one sample and fill the per-sample arrays for a chunk. */
static void run_smoothers(pulse_t *p, int n, float *wb, float *sw, float *lv)
{
    const float c = p->smooth_coef;
    const pulse_params *t = &p->target;
    for (int k = 0; k < n; k++) {
        const float sweep_t = 0.5f * (t->sweep + (1.0f - t->sweep) * p->mod_in);
        p->s_wbase += c * ((1.0f - t->shape) - p->s_wbase);
        p->s_sweep += c * (sweep_t - p->s_sweep);
        p->s_rate += c * (t->rate - p->s_rate);
        p->s_detune += c * (t->detune - p->s_detune);
        p->s_level += c * (t->level - p->s_level);
        p->s_bend += c * (p->bend_in * t->bend_range - p->s_bend);
        wb[k] = p->s_wbase;
        sw[k] = p->s_sweep;
        lv[k] = p->s_level;
    }
    p->s_wbase = flush(p->s_wbase);
    p->s_sweep = flush(p->s_sweep);
    p->s_detune = flush(p->s_detune);
    p->s_level = flush(p->s_level);
    p->s_bend = flush(p->s_bend);
}

/* Control-rate update: pitch ratios and LFO speed from the smoothed controls. */
static void control_update(pulse_t *p)
{
    p->bend_ratio = exp2f(p->s_bend * (1.0f / 12.0f));
    p->ratio_a = exp2f(-p->s_detune * (50.0f / 1200.0f));
    p->ratio_b = exp2f(p->s_detune * (50.0f / 1200.0f));
    p->lfo_inc = pulse_rate_hz(p->s_rate) / p->sr;
    for (int i = 0; i < PULSE_VOICES; i++)
        if (p->v[i].state != V_IDLE)
            set_voice_incs(p, &p->v[i]);
}

static void render_voice(pulse_t *p, int idx, int n, const float *wb, const float *sw, float *l, float *r)
{
    pulse_voice *v = &p->v[idx];
    const float sus = p->target.sustain;
    const float c = p->smooth_coef;

    for (int k = 0; k < n; k++) {
        if (v->state == V_IDLE && !v->fading)
            return;

        /* gate: a key that went up sends any stage but release into release */
        if (!v->gate && v->state != V_RELEASE && v->state != V_IDLE)
            v->state = V_RELEASE;

        switch (v->state) {
        case V_ATTACK:
            v->env += p->att_inc;
            if (v->env >= 1.0f) {
                v->env = 1.0f;
                v->state = V_DECAY;
            }
            break;
        case V_DECAY:
            v->env = sus + (v->env - sus) * p->dec_coef;
            if (fabsf(v->env - sus) < 1e-4f) {
                v->env = sus;
                v->state = V_SUSTAIN;
            }
            break;
        case V_SUSTAIN:
            v->env += c * (sus - v->env);
            break;
        case V_RELEASE:
            v->env = flush(v->env * p->rel_coef);
            if (v->env < 1e-4f) {
                v->env = 0.0f;
                v->state = V_IDLE;
            }
            break;
        default:
            break;
        }

        float amp = v->env * v->gain * v->fade * VOICE_GAIN;
        if (v->fading) {
            v->fade -= p->fade_dec;
            if (v->fade <= 0.0f) {
                v->fade = 1.0f;
                v->fading = 0;
                if (v->pending) {
                    start_voice(p, idx, v->pend_note, v->pend_gain, v->pend_gate);
                } else {
                    v->state = V_IDLE;
                    v->env = 0.0f;
                }
            }
        }
        if (v->state == V_IDLE && !v->fading)
            return;

        /* parabolic sine in -1..1; the second oscillator sweeps the opposite way */
        const float x = 2.0f * v->ph_lfo - 1.0f;
        const float lfo = 4.0f * x * (1.0f - fabsf(x));
        float wa = clampf(wb[k] + sw[k] * lfo, 0.0f, 1.0f);
        float wbv = clampf(wb[k] - sw[k] * lfo, 0.0f, 1.0f);
        float lo_a = 1.5f * v->inc_a, lo_b = 1.5f * v->inc_b;
        lo_a = lo_a < SHAPE_PW_MIN ? SHAPE_PW_MIN : (lo_a > 0.5f ? 0.5f : lo_a);
        lo_b = lo_b < SHAPE_PW_MIN ? SHAPE_PW_MIN : (lo_b > 0.5f ? 0.5f : lo_b);
        const float pw_a = clampf(SHAPE_PW_MIN + (0.5f - SHAPE_PW_MIN) * wa, lo_a, 0.5f);
        const float pw_b = clampf(SHAPE_PW_MIN + (0.5f - SHAPE_PW_MIN) * wbv, lo_b, 0.5f);

        const float a = pulse_osc(v->ph_a, v->inc_a, pw_a);
        const float b = pulse_osc(v->ph_b, v->inc_b, pw_b);

        v->ph_a += v->inc_a;
        if (v->ph_a >= 1.0f)
            v->ph_a -= 1.0f;
        v->ph_b += v->inc_b;
        if (v->ph_b >= 1.0f)
            v->ph_b -= 1.0f;
        v->ph_lfo += p->lfo_inc;
        if (v->ph_lfo >= 1.0f)
            v->ph_lfo -= 1.0f;

        /* oscillator A leans left, B leans right; with equal A and B this is a centered mono signal */
        l[k] += amp * (0.75f * a + 0.25f * b);
        r[k] += amp * (0.25f * a + 0.75f * b);
    }
}

void pulse_render(pulse_t *p, float *left, float *right, int frames)
{
    float wb[PULSE_CTRL], sw[PULSE_CTRL], lv[PULSE_CTRL];
    float peak = 0.0f;
    int done = 0;
    while (done < frames) {
        if (p->ctrl_pos == 0)
            control_update(p);
        int n = PULSE_CTRL - p->ctrl_pos;
        if (n > frames - done)
            n = frames - done;
        float *l = left + done, *r = right + done;
        memset(l, 0, sizeof(float) * (size_t)n);
        memset(r, 0, sizeof(float) * (size_t)n);

        run_smoothers(p, n, wb, sw, lv);
        for (int i = 0; i < PULSE_VOICES; i++)
            render_voice(p, i, n, wb, sw, l, r);

        for (int k = 0; k < n; k++) {
            l[k] = soft_ceiling(l[k] * lv[k]);
            r[k] = soft_ceiling(r[k] * lv[k]);
            const float m = fabsf(l[k]) > fabsf(r[k]) ? fabsf(l[k]) : fabsf(r[k]);
            if (m > peak)
                peak = m;
        }

        p->ctrl_pos += n;
        if (p->ctrl_pos >= PULSE_CTRL)
            p->ctrl_pos = 0;
        done += n;
    }
    if (peak > p->act_peak)
        p->act_peak = peak;
}
