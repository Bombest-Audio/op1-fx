/* Voltage DSP core. See voltage_core.h. */
#include "voltage_core.h"

#include <math.h>
#include <string.h>

#define PI_D 3.14159265358979323846
#define CTRL_TICK 16        /* samples per control update */
#define VOICE_GAIN 0.7f     /* per-voice scale before the master limiter */
#define STEAL_FADE_SEC 0.003f
#define BASE_RATE 44100.0f  /* rate the engine constants below are quoted at */

/* ---------------------------------------------------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------------------------------------------------- */

static inline float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

/* One full cycle in 32 bits: 2^32 counts. A 2048-entry table, linear interpolation: error under 1.3e-6. */
static inline float sine_lut(const float *t, uint32_t p)
{
    const uint32_t i = p >> 21;
    const float f = (float)(p & 0x1FFFFFu) * (1.0f / 2097152.0f);
    return t[i] + f * (t[i + 1] - t[i]);
}

/* Phase offset from a feedback amount in -1..1: 1 is half a cycle (pi radians). */
static inline uint32_t fb_offset(float q)
{
    return (uint32_t)(int32_t)(clampf(q, -1.0f, 1.0f) * 2147483520.0f);
}

static inline uint32_t inc_to_u32(float cycles)
{
    return (uint32_t)(clampf(cycles, 0.0f, 0.5f) * 4294967296.0f);
}

/* The soft shaper used on every branch: x - 0.3 x^3. It rounds off the peaks of a sine. */
static inline float soft(float x)
{
    return x - 0.3f * x * x * x;
}

/* Clamp to just under +-1, the range of a 16-bit sample. */
#define FS_MAX 0.999969f

/* Master limiter: linear below 0.6, tanh knee above, tops out at 0.99. */
static inline float limit(float x)
{
    const float a = fabsf(x);
    if (a <= 0.6f)
        return x;
    const float y = 0.6f + 0.39f * tanhf((a - 0.6f) * (1.0f / 0.39f));
    return x < 0.0f ? -y : y;
}

float opvl_note_hz(float note)
{
    return 440.0f * exp2f((note - 69.0f) / 12.0f);
}

/* Cutoff law, quoted at 44.1 kHz. The pole frequency rises exponentially with the knob over its first 48 percent
 * (about 21 Hz to far past the audio band) and a tanh keeps the per-sample pole below 2.5 rad. Above 48 percent the
 * knob only affects the ramp. */
#define FILTER_KNOB_MAX 0.481f
static float filter_w44(float norm)
{
    const float n = clampf(norm, 0.0f, 1.0f);
    const float w = 0.00294f * expf(15.2f * fminf(n, FILTER_KNOB_MAX));
    return 2.5f * tanhf(w * 0.4f);
}

float opvl_filter_hz(float norm)
{
    return filter_w44(norm) * BASE_RATE / (float)(2.0 * PI_D);
}

float opvl_detune_percent(float norm)
{
    const float n = clampf(norm, 0.0f, 1.0f);
    return 50.0f * n * n;
}

void opvl_sine_fill(float *table)
{
    for (int i = 0; i <= OPVL_SINE_SIZE; i++)
        table[i] = (float)sin(2.0 * PI_D * (double)i / (double)OPVL_SINE_SIZE);
}

/* ---------------------------------------------------------------------------------------------------------------
 * The oscillator engine
 *
 * Four sines. Oscillator 0 is the carrier at the played pitch. Oscillator 3 is a twin at the same pitch, 1 sits below
 * it and 2 above it by the DETUNE spread. Oscillators 1, 2 and 3 feed their own previous output back into their
 * phase (FEEDBACK). Branches:
 *   dry   the carrier at half level, added after the low-pass
 *   a     oscillator 1, shaped, at 3/16
 *   b     oscillators 2 and 3 are rectified and shaped, turned into 15-bit integers and XORed. XOR amount scales it
 *         and it is shaped again and mixed at 3/8
 *   ramp  a level that falls by a step proportional to the 3-twin level every sample, halves each time the carrier
 *         passes half a cycle, and enters the low-pass at half level
 * a, b and ramp go through a one-pole low-pass (FILTER), then the carrier is added.
 * ------------------------------------------------------------------------------------------------------------- */

void opvl_eng_prepare(opvl_eng_params *ep, float xor_amt, float feedback, float filter, float detune, float sample_rate)
{
    const float rate_ratio = BASE_RATE / sample_rate;
    const float a = 1.0f - clampf(filter, 0.0f, 1.0f);
    const float d = clampf(detune, 0.0f, 1.0f);
    ep->fb = clampf(feedback, 0.0f, 1.0f);
    ep->xor_amt = clampf(xor_amt, 0.0f, 1.0f);
    ep->spread = 0.5f * d * d;
    ep->lp = 1.0f - expf(-filter_w44(filter) * rate_ratio);
    ep->ramp = a * a * (1.0f / 16.0f) * rate_ratio;
    /* A drift of 0.45 Hz per second after note-off, in cycles per sample per sample. */
    ep->drift = (float)((double)BASE_RATE * (double)BASE_RATE / 4294967296.0) / (sample_rate * sample_rate);
    ep->smooth = fminf(1.0f, 1.0f - expf(-(float)(2.0 * PI_D * 11025.0) / sample_rate));
}

void opvl_eng_start(opvl_eng_t *e, const opvl_eng_params *ep)
{
    (void)ep;
    memset(e, 0, sizeof *e);
    /* Settled values for a typical setting: the ramp spends most of its time near -1 and the low-pass follows half of it. */
    e->ramp = -1.0f;
    e->lp = -0.5f;
}

void opvl_eng_run(opvl_eng_t *e, const opvl_eng_params *ep, const float *sine, float inc0, float dinc, float *out, int n)
{
    uint32_t ph0 = e->ph[0], ph1 = e->ph[1], ph2 = e->ph[2], ph3 = e->ph[3];
    float y1p = e->yp[1], y2p = e->yp[2], y3p = e->yp[3];
    float ramp = e->ramp, lp = e->lp, drift = e->drift;
    const int released = e->released;
    const float fb = ep->fb, xa = ep->xor_amt, m = ep->spread, lpc = ep->lp, rstep = ep->ramp, ddrift = ep->drift;
    float inc = inc0;

    for (int i = 0; i < n; i++) {
        const float ic = clampf(inc, 0.0f, 0.5f);
        const uint32_t d0 = inc_to_u32(ic);
        const uint32_t d1 = inc_to_u32(ic * (1.0f - m) + drift);
        const uint32_t d2 = inc_to_u32(ic * (1.0f + m) - drift);

        const float s0 = sine_lut(sine, ph0);
        const float y1 = sine_lut(sine, ph1 + fb_offset(fb * y1p));
        const float y2 = sine_lut(sine, ph2 + fb_offset(fb * y2p));
        const float y3 = sine_lut(sine, ph3 + fb_offset(fb * y3p));
        y1p = y1;
        y2p = y2;
        y3p = y3;

        /* The carrier "wraps" when it passes half a cycle, the point where its sine goes negative. */
        const uint32_t p_old = ph0 ^ 0x80000000u;
        ph0 += d0;
        const int wrap = (ph0 ^ 0x80000000u) < p_old;
        ph1 += d1;
        ph2 += d2;
        ph3 += d0;

        /* Branch a: oscillator 1 shaped. */
        const float a = clampf(1.5f * soft(y1), -FS_MAX, FS_MAX) * 0.125f;

        /* Branch b: XOR of two rectified, shaped sines as 15-bit integers. */
        const float r3 = clampf(1.5f * soft(fabsf(y3)), 0.0f, FS_MAX);
        const float r2 = clampf(1.5f * soft(fabsf(y2)), 0.0f, FS_MAX);
        const int32_t xi = (int32_t)(r3 * 32768.0f) ^ (int32_t)(r2 * 32768.0f);
        const float u = xa * ((float)xi * (1.0f / 32768.0f));
        const float b = clampf(1.5f * soft(u), -FS_MAX, FS_MAX) * 0.25f;

        /* Ramp: halve at the carrier wrap, then fall by a step that follows the pitch-twin level. */
        if (wrap)
            ramp *= 0.5f;
        ramp = fmaxf(ramp - r3 * rstep, -1.0f);

        const float xf = clampf(a + b, -FS_MAX, FS_MAX);
        const float x = clampf(xf + 0.5f * ramp, -1.0f, FS_MAX);
        /* The low-pass is an integrator that clips before it subtracts its feedback, so a hot input rings against +-1. */
        lp = clampf(clampf(lp + lpc * x, -1.0f, FS_MAX) - lpc * lp, -1.0f, FS_MAX);
        out[i] = clampf(0.5f * s0 + lp, -1.0f, FS_MAX);

        if (released)
            drift += ddrift;
        inc += dinc;
    }
    e->ph[0] = ph0;
    e->ph[1] = ph1;
    e->ph[2] = ph2;
    e->ph[3] = ph3;
    e->yp[1] = y1p;
    e->yp[2] = y2p;
    e->yp[3] = y3p;
    e->ramp = ramp;
    e->lp = lp;
    e->drift = drift;
}

/* ---------------------------------------------------------------------------------------------------------------
 * Instrument: voices, envelope, controls
 * ------------------------------------------------------------------------------------------------------------- */

enum { ST_ATTACK = 0, ST_DECAY, ST_RELEASE };

static void voice_reset(opvl_voice *v)
{
    memset(v, 0, sizeof *v);
    v->state = OPVL_IDLE;
}

static void update_env_coefs(opvl_t *s, opvl_voice *v)
{
    const opvl_params *p = &s->target;
    const float sr = s->sr;
    v->att_inc = 1.0f / (fmaxf(p->env_attack, 0.0005f) * sr);
    v->dec_coef = 1.0f - expf(-6.9f / (fmaxf(p->env_decay, 0.002f) * sr));
    v->rel_coef = expf(-6.9f / (fmaxf(p->env_release, 0.002f) * sr));
    v->sus = clampf(p->env_sustain, 0.0f, 1.0f);
}

static float pitch_ratio(const opvl_t *s)
{
    const float vib = s->s_mod * 0.5f * sinf(s->lfo_phase);
    return exp2f((s->s_bend + vib) * (1.0f / 12.0f));
}

static void voice_start(opvl_t *s, opvl_voice *v, int note, int vel)
{
    voice_reset(v);
    v->state = OPVL_ON;
    v->note = note;
    v->age = ++s->age_counter;
    v->vel = (float)clampf((float)vel, 1.0f, 127.0f) * (1.0f / 127.0f);
    v->base_inc = opvl_note_hz((float)note) / s->sr;
    v->inc_cur = clampf(v->base_inc * pitch_ratio(s), 0.0f, 0.5f);
    opvl_eng_start(&v->eng, &s->ep);
    v->smooth = 0.0f;
    v->primed = 0;
    update_env_coefs(s, v);
    v->env = 0.0f;
    v->stage = ST_ATTACK;
    v->fade = 1.0f;
}

static void voice_release(opvl_voice *v)
{
    if (v->state == OPVL_ON) {
        v->state = OPVL_REL;
        v->stage = ST_RELEASE;
        v->eng.released = 1;
        v->pedal_held = 0;
    }
}

void opvl_init(opvl_t *s, float sample_rate)
{
    memset(s, 0, sizeof *s);
    s->sr = sample_rate;
    opvl_sine_fill(s->sine);
    s->target.xor_amt = OPVL_DEFAULT_XOR;
    s->target.feedback = OPVL_DEFAULT_FEEDBACK;
    s->target.filter = OPVL_DEFAULT_FILTER;
    s->target.detune = OPVL_DEFAULT_DETUNE;
    s->target.env_attack = OPVL_DEFAULT_ATTACK;
    s->target.env_decay = OPVL_DEFAULT_ENV_DECAY;
    s->target.env_sustain = OPVL_DEFAULT_SUSTAIN;
    s->target.env_release = OPVL_DEFAULT_RELEASE;
    s->target.level = OPVL_DEFAULT_LEVEL;
    s->ctrl_coef = 1.0f - expf(-(float)CTRL_TICK / (0.008f * sample_rate));
    s->hp_coef = expf(-(float)(2.0 * PI_D * 8.0) / sample_rate);
    s->level_coef = 1.0f - expf(-1.0f / (0.005f * sample_rate));
    opvl_reset(s);
}

void opvl_reset(opvl_t *s)
{
    for (int i = 0; i < OPVL_VOICES; i++)
        voice_reset(&s->voice[i]);
    s->s_xor = s->target.xor_amt;
    s->s_fb = s->target.feedback;
    s->s_filter = s->target.filter;
    s->s_detune = s->target.detune;
    s->s_bend = s->bend_target = 0.0f;
    s->s_mod = s->mod_target = 0.0f;
    s->lfo_phase = 0.0f;
    s->level_cur = s->target.level;
    s->pedal = 0;
    s->out_peak = 0.0f;
    s->ctrl_left = 0;
    opvl_eng_prepare(&s->ep, s->s_xor, s->s_fb, s->s_filter, s->s_detune, s->sr);
}

void opvl_set_params(opvl_t *s, const opvl_params *p)
{
    s->target = *p;
    for (int i = 0; i < OPVL_VOICES; i++)
        if (s->voice[i].state != OPVL_IDLE)
            update_env_coefs(s, &s->voice[i]);
}

void opvl_pitch_bend(opvl_t *s, float semitones)
{
    s->bend_target = clampf(semitones, -48.0f, 48.0f);
}

void opvl_mod_wheel(opvl_t *s, float amount)
{
    s->mod_target = clampf(amount, 0.0f, 1.0f);
}

/* Pick a voice for a new note: an idle one, else the same note again, else the quietest released voice, else the
 * oldest. A voice that is already being stolen is only taken last. */
static int pick_voice(const opvl_t *s, int note)
{
    int idle = -1, same = -1, rel = -1, old = -1, stealing = -1;
    for (int i = 0; i < OPVL_VOICES; i++) {
        const opvl_voice *v = &s->voice[i];
        if (v->state == OPVL_IDLE) {
            if (idle < 0)
                idle = i;
            continue;
        }
        if (v->state == OPVL_STEAL) {
            if (stealing < 0 || v->age < s->voice[stealing].age)
                stealing = i;
            continue;
        }
        if (v->note == note && (same < 0 || v->age > s->voice[same].age))
            same = i;
        if (v->state == OPVL_REL && (rel < 0 || v->env < s->voice[rel].env))
            rel = i;
        if (old < 0 || v->age < s->voice[old].age)
            old = i;
    }
    if (idle >= 0)
        return idle;
    if (same >= 0)
        return same;
    if (rel >= 0)
        return rel;
    if (old >= 0)
        return old;
    return stealing;
}

void opvl_note_on(opvl_t *s, int note, int vel)
{
    if (vel <= 0) {
        opvl_note_off(s, note);
        return;
    }
    note = note < 0 ? 0 : (note > 127 ? 127 : note);
    const int i = pick_voice(s, note);
    if (i < 0)
        return;
    opvl_voice *v = &s->voice[i];
    if (v->state == OPVL_IDLE) {
        voice_start(s, v, note, vel);
        return;
    }
    /* Fade the running voice out, then start the new note in its place. */
    v->pend_note = note;
    v->pend_vel = vel;
    if (v->state != OPVL_STEAL) {
        v->state = OPVL_STEAL;
        v->fade_step = 1.0f / (STEAL_FADE_SEC * s->sr);
        v->fade = 1.0f;
    }
}

void opvl_note_off(opvl_t *s, int note)
{
    for (int i = 0; i < OPVL_VOICES; i++) {
        opvl_voice *v = &s->voice[i];
        if (v->state == OPVL_ON && v->note == note) {
            if (s->pedal)
                v->pedal_held = 1;
            else
                voice_release(v);
        } else if (v->state == OPVL_STEAL && v->pend_note == note) {
            /* The pending note ended before it started: drop it. */
            v->pend_note = -1;
        }
    }
}

void opvl_sustain(opvl_t *s, int down)
{
    s->pedal = down ? 1 : 0;
    if (!down)
        for (int i = 0; i < OPVL_VOICES; i++)
            if (s->voice[i].state == OPVL_ON && s->voice[i].pedal_held)
                voice_release(&s->voice[i]);
}

void opvl_all_off(opvl_t *s)
{
    s->pedal = 0;
    for (int i = 0; i < OPVL_VOICES; i++) {
        opvl_voice *v = &s->voice[i];
        if (v->state == OPVL_STEAL)
            v->pend_note = -1;
        else
            voice_release(v);
    }
}

/* Control tick: smooth the knobs, rebuild the engine constants, set each voice's pitch ramp for the next tick. */
static void control_tick(opvl_t *s)
{
    const opvl_params *t = &s->target;
    const float c = s->ctrl_coef;
    s->s_xor += c * (t->xor_amt - s->s_xor);
    s->s_fb += c * (t->feedback - s->s_fb);
    s->s_filter += c * (t->filter - s->s_filter);
    s->s_detune += c * (t->detune - s->s_detune);
    s->s_bend += 0.35f * (s->bend_target - s->s_bend);
    s->s_mod += c * (s->mod_target - s->s_mod);
    s->lfo_phase += (float)(2.0 * PI_D * 5.5) * (float)CTRL_TICK / s->sr;
    if (s->lfo_phase > (float)(2.0 * PI_D))
        s->lfo_phase -= (float)(2.0 * PI_D);
    opvl_eng_prepare(&s->ep, s->s_xor, s->s_fb, s->s_filter, s->s_detune, s->sr);
    s->ctrl_left = CTRL_TICK;
}

void opvl_render(opvl_t *s, float *left, float *right, int frames)
{
    int done = 0;
    int ramp_ready = 0;
    float tmp[CTRL_TICK];

    memset(left, 0, sizeof(float) * (size_t)frames);

    while (done < frames) {
        if (s->ctrl_left <= 0) {
            control_tick(s);
            ramp_ready = 0;
        }
        if (!ramp_ready) {
            /* Glide each voice to the pitch it should have at the end of this tick (bend and vibrato). */
            const float ratio = pitch_ratio(s);
            for (int vi = 0; vi < OPVL_VOICES; vi++) {
                opvl_voice *v = &s->voice[vi];
                if (v->state == OPVL_IDLE)
                    continue;
                const float tgt = clampf(v->base_inc * ratio, 0.0f, 0.5f);
                v->dinc = (tgt - v->inc_cur) / (float)s->ctrl_left;
            }
            ramp_ready = 1;
        }
        int n = frames - done;
        if (n > s->ctrl_left)
            n = s->ctrl_left;

        for (int vi = 0; vi < OPVL_VOICES; vi++) {
            opvl_voice *v = &s->voice[vi];
            if (v->state == OPVL_IDLE)
                continue;
            opvl_eng_run(&v->eng, &s->ep, s->sine, v->inc_cur, v->dinc, tmp, n);
            v->inc_cur += v->dinc * (float)n;
            float *dst = left + done;
            for (int i = 0; i < n; i++) {
                const float x = tmp[i];
                if (!v->primed) {
                    v->smooth = x;
                    v->dc_x = x;
                    v->dc_y = 0.0f;
                    v->primed = 1;
                }
                v->smooth += s->ep.smooth * (x - v->smooth);
                const float y = v->smooth - v->dc_x + s->hp_coef * v->dc_y;
                v->dc_x = v->smooth;
                v->dc_y = y;

                switch (v->stage) {
                case ST_ATTACK:
                    v->env += v->att_inc;
                    if (v->env >= 1.0f) {
                        v->env = 1.0f;
                        v->stage = ST_DECAY;
                    }
                    break;
                case ST_DECAY:
                    v->env += (v->sus - v->env) * v->dec_coef;
                    break;
                default:
                    v->env *= v->rel_coef;
                    break;
                }
                float g = v->env * v->vel * VOICE_GAIN;
                if (v->state == OPVL_STEAL) {
                    v->fade -= v->fade_step;
                    if (v->fade < 0.0f)
                        v->fade = 0.0f;
                    g *= v->fade;
                }
                const float o = y * g;
                const float ao = fabsf(o);
                if (ao > v->peak)
                    v->peak = ao;
                dst[i] += o;

                if (v->state == OPVL_STEAL && v->fade <= 0.0f) {
                    if (v->pend_note >= 0) {
                        const int pn = v->pend_note, pv = v->pend_vel;
                        voice_start(s, v, pn, pv);
                        v->dinc = 0.0f;
                    } else {
                        voice_reset(v);
                    }
                    break; /* the rest of this tick belongs to the new note, which starts on the next tick */
                }
                if (v->stage == ST_RELEASE && v->env < 1.0e-4f) {
                    voice_reset(v);
                    break;
                }
            }
        }

        /* Master level (smoothed), limiter, mono to both channels. */
        for (int i = 0; i < n; i++) {
            s->level_cur += s->level_coef * (s->target.level - s->level_cur);
            const float o = limit(left[done + i] * s->level_cur);
            left[done + i] = o;
            const float ao = fabsf(o);
            if (ao > s->out_peak)
                s->out_peak = ao;
        }
        done += n;
        s->ctrl_left -= n;
    }
    memcpy(right, left, sizeof(float) * (size_t)frames);
}

float opvl_take_level(opvl_t *s)
{
    const float p = s->out_peak;
    s->out_peak = 0.0f;
    return p;
}

int opvl_voice_info(opvl_t *s, int index, int *note, float *level)
{
    if (index < 0 || index >= OPVL_VOICES)
        return 0;
    opvl_voice *v = &s->voice[index];
    if (v->state == OPVL_IDLE)
        return 0;
    if (note)
        *note = v->note;
    if (level)
        *level = v->peak;
    v->peak = 0.0f;
    return 1;
}

int opvl_active_voices(const opvl_t *s)
{
    int n = 0;
    for (int i = 0; i < OPVL_VOICES; i++)
        if (s->voice[i].state != OPVL_IDLE)
            n++;
    return n;
}
