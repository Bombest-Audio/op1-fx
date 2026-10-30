#include "cluster_core.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f
#define TWO_POW_32 4294967296.0f

/* Voice stages. */
enum { VS_IDLE = 0, VS_ATTACK, VS_DECAY, VS_RELEASE, VS_STEAL, VS_STEAL_DONE };

/* The engine updates its slow controls on a fixed tick of 128 samples at 44.1 kHz (about 2.9 ms), the same block
 * size the OP-1 code works in. The tick length in samples scales with the sample rate so the times in seconds hold. */
#define TICK_SECONDS (128.0f / 44100.0f)

/* Per-tick one-pole coefficient for the oscillator gates and the level scale. About 0.6 s to settle. */
#define GATE_COEF 0.005f
/* The feedback drift integrates this much times the cube of the drift direction per tick. */
#define DRIFT_RATE 0.01276f
/* Detune at note on is a random fraction up to this size of the played frequency. */
#define INIT_DETUNE 0.015f
/* Detune is kept inside this fraction of the played frequency. */
#define MAX_DETUNE 0.25f
/* Highest oscillator frequency as a fraction of the sample rate, before detune. Keeps the phase step under half a cycle. */
#define MAX_CYCLES 0.38f
/* Ceiling on the sum of voices: linear to SAT_T, then a tanh knee that reaches 1.0 with matching slope. */
#define SAT_T 0.75f
/* Gain of one voice at full velocity before the pan law and the level control. */
#define VOICE_GAIN 0.55f
#define STEAL_SECONDS 0.003f

static inline float clampf(float v, float lo, float hi)
{
    if (!(v >= lo))
        return lo;
    return v > hi ? hi : v;
}

static inline float soft_ceiling(float x)
{
    float a = fabsf(x);
    if (a <= SAT_T)
        return x;
    float k = 1.0f - SAT_T;
    float z = (a - SAT_T) / k;
    z = z > 3.0f ? 3.0f : z;
    float z2 = z * z;
    float t = z * (27.0f + z2) / (27.0f + 9.0f * z2);
    float yv = SAT_T + k * t;
    return x < 0.0f ? -yv : yv;
}

static inline float flush(float v)
{
    return fabsf(v) < 1e-20f ? 0.0f : v;
}

static inline float frand(cluster_t *c) /* -1..1 */
{
    c->rng = c->rng * 1664525u + 1013904223u;
    return (float)(c->rng >> 8) * (1.0f / 8388608.0f) - 1.0f;
}

float cluster_env_time(float norm)
{
    return 0.001f * powf(5000.0f, clampf(norm, 0.0f, 1.0f));
}

float cluster_env_norm(float seconds)
{
    return clampf(logf(seconds * 1000.0f) / logf(5000.0f), 0.0f, 1.0f);
}

void cluster_feedback_path(float knob, float *a, float *b)
{
    const float u = 0.625f * clampf(knob, 0.0f, 1.0f);
    if (u < 0.125f) {
        *a = 8.0f * u;
        *b = -1.0f;
    } else if (u < 0.375f) {
        *a = 1.0f;
        *b = 8.0f * (u - 0.25f);
    } else if (u < 0.5f) {
        *a = 1.0f - 8.0f * (u - 0.375f);
        *b = 1.0f;
    } else {
        *a = 0.0f;
        *b = 1.0f - 8.0f * (u - 0.5f);
    }
}

float cluster_spread_tau(float spread)
{
    return 0.02f * powf(1500.0f, clampf(spread, 0.0f, 1.0f));
}

/* Fixed stereo position of each oscillator, -1 left to +1 right. Oscillator 0 is the centered, in-tune one. */
static const float kPan[CLUSTER_OSCS] = {0.0f, -0.55f, 0.55f, -0.25f, 0.25f, -0.8f, 0.8f, 0.0f};

static void sanitize(cluster_params *p)
{
    p->oscs = p->oscs < 1 ? 1 : (p->oscs > CLUSTER_OSCS ? CLUSTER_OSCS : p->oscs);
    p->feedback = clampf(p->feedback, 0.0f, 1.0f);
    p->spread = clampf(p->spread, 0.0f, 1.0f);
    p->wander = clampf(p->wander, 0.0f, 1.0f);
    p->attack = clampf(p->attack, 0.0f, 1.0f);
    p->decay = clampf(p->decay, 0.0f, 1.0f);
    p->sustain = clampf(p->sustain, 0.0f, 1.0f);
    p->release = clampf(p->release, 0.0f, 1.0f);
    p->level = clampf(p->level, 0.0f, 1.0f);
    p->bend_range = clampf(p->bend_range, 0.0f, 12.0f);
}

static float level_gain(float level)
{
    return 2.0f * level * level;
}

void cluster_seed(cluster_t *c, uint32_t seed)
{
    c->rng = seed ? seed : 0x2545F491u;
}

void cluster_reset(cluster_t *c)
{
    memset(c->voice, 0, sizeof c->voice);
    for (int v = 0; v < CLUSTER_VOICES; v++)
        c->voice[v].pending_note = -1;
    c->tick_pos = 0;
    c->bend = c->bend_s = 0.0f;
    c->mod = 0.0f;
    c->pedal = 0;
    c->age_counter = 0;
    c->gain_s = level_gain(c->target.level);
    c->out_peak = 0.0f;
    memset(c->osc_act, 0, sizeof c->osc_act);
    memset(c->vl, 0, sizeof c->vl);
    memset(c->vr, 0, sizeof c->vr);
    memset(c->venv, 0, sizeof c->venv);
}

void cluster_init(cluster_t *c, float sample_rate)
{
    memset(c, 0, sizeof *c);
    c->sr = sample_rate >= 8000.0f ? (sample_rate > 384000.0f ? 384000.0f : sample_rate) : 44100.0f;
    int t = (int)(TICK_SECONDS * c->sr + 0.5f);
    c->tick_len = t < 16 ? 16 : (t > CLUSTER_MAX_TICK ? CLUSTER_MAX_TICK : t);
    c->tick_seconds = (float)c->tick_len / c->sr;
    c->target.oscs = CLUSTER_DEFAULT_OSCS;
    c->target.feedback = CLUSTER_DEFAULT_FEEDBACK;
    c->target.spread = CLUSTER_DEFAULT_SPREAD;
    c->target.wander = CLUSTER_DEFAULT_WANDER;
    c->target.attack = CLUSTER_DEFAULT_ATTACK;
    c->target.decay = CLUSTER_DEFAULT_DECAY;
    c->target.sustain = CLUSTER_DEFAULT_SUSTAIN;
    c->target.release = CLUSTER_DEFAULT_RELEASE;
    c->target.level = CLUSTER_DEFAULT_LEVEL;
    c->target.bend_range = CLUSTER_DEFAULT_BEND;
    c->gain_coef = 1.0f - expf(-1.0f / (0.005f * c->sr));
    for (int i = 0; i < CLUSTER_OSCS; i++) {
        const float th = (kPan[i] + 1.0f) * (PI_F * 0.25f); /* constant-power pan */
        c->pan_l[i] = cosf(th);
        c->pan_r[i] = sinf(th);
    }
    for (int i = 0; i <= CLUSTER_SINE_SIZE; i++)
        c->sine[i] = sinf(2.0f * PI_F * (float)i / (float)CLUSTER_SINE_SIZE);
    c->sine[CLUSTER_SINE_SIZE] = c->sine[0];
    cluster_seed(c, 0x2545F491u);
    cluster_reset(c);
}

void cluster_set_params(cluster_t *c, const cluster_params *p)
{
    c->target = *p;
    sanitize(&c->target);
    int idle = 1;
    for (int i = 0; i < CLUSTER_VOICES; i++)
        idle &= c->voice[i].stage == VS_IDLE;
    if (idle)
        c->gain_s = level_gain(c->target.level); /* nothing is sounding, so there is nothing to smooth */
}

static inline float sine_lookup(const cluster_t *c, uint32_t ph)
{
    const uint32_t idx = ph >> 20;
    const float frac = (float)(ph & 0xFFFFFu) * (1.0f / 1048576.0f);
    const float a = c->sine[idx];
    return a + frac * (c->sine[idx + 1] - a);
}

static inline float note_freq(int note, float semis)
{
    return 440.0f * exp2f(((float)note - 69.0f + semis) / 12.0f);
}

/* Targets for the next tick for one voice. */
static void voice_targets(cluster_t *c, cluster_voice *v, int prime)
{
    const cluster_params *p = &c->target;
    const int n = p->oscs;
    const float norm_t = powf((float)n, -0.75f);
    float a, b;
    cluster_feedback_path(p->feedback, &a, &b);

    if (prime) {
        v->norm = norm_t;
        v->acc = 0.0f;
        for (int i = 0; i < CLUSTER_OSCS; i++)
            v->g[i] = i < n ? 1.0f : 0.0f;
    } else {
        v->norm += GATE_COEF * (norm_t - v->norm);
        for (int i = 0; i < CLUSTER_OSCS; i++) {
            const float t = i < n ? 1.0f : 0.0f;
            v->g[i] += GATE_COEF * (t - v->g[i]);
            if (t == 0.0f && v->g[i] < 1e-4f)
                v->g[i] = 0.0f;
        }
        v->acc = clampf(v->acc + DRIFT_RATE * b * b * b, -1.0f, 1.0f);
    }

    /* feedback in cycles of phase per unit of the previous output sample: 0.25 at full, 3/4 of that while released */
    const float s = clampf(a + v->acc + 0.5f * c->mod, -1.0f, 1.0f);
    v->fb_next = 0.25f * s * (v->released ? 0.75f : 1.0f);

    /* detune random walk: rnd = rnd * decay + noise * kick, every tick */
    const float decay = expf(-c->tick_seconds / cluster_spread_tau(p->spread));
    const float kick = 0.5f * p->wander * (v->released ? 0.125f : 1.0f);
    v->rnd[0] = 0.0f; /* oscillator 0 stays on the played pitch */
    for (int i = 1; i < CLUSTER_OSCS; i++) {
        if (!prime)
            v->rnd[i] = clampf(flush(v->rnd[i] * decay) + frand(c) * kick, -MAX_DETUNE, MAX_DETUNE);
        v->det_next[i] = v->rnd[i];
    }
    v->det_next[0] = 0.0f;

    for (int i = 0; i < CLUSTER_OSCS; i++)
        v->amp_next[i] = v->g[i] * v->norm;

    if (prime) {
        v->fb_cur = v->fb_next;
        v->fb_step = 0.0f;
        for (int i = 0; i < CLUSTER_OSCS; i++) {
            v->det_cur[i] = v->det_next[i];
            v->det_step[i] = 0.0f;
            v->amp_cur[i] = v->amp_next[i];
            v->amp_step[i] = 0.0f;
        }
    }
}

static void voice_start(cluster_t *c, cluster_voice *v, int note, float vel)
{
    v->stage = VS_ATTACK;
    v->note = note;
    v->held = 1;
    v->sustained = 0;
    v->released = 0;
    v->pending_note = -1;
    v->vel = vel;
    v->env = 0.0f;
    v->age = ++c->age_counter;
    memset(v->phase, 0, sizeof v->phase);
    memset(v->y, 0, sizeof v->y);
    for (int i = 0; i < CLUSTER_OSCS; i++)
        v->rnd[i] = i == 0 ? 0.0f : INIT_DETUNE * frand(c);
    voice_targets(c, v, 1);
}

static void voice_release(cluster_voice *v)
{
    v->held = 0;
    v->sustained = 0;
    if (v->stage == VS_ATTACK || v->stage == VS_DECAY) {
        v->stage = VS_RELEASE;
        v->released = 1;
    }
}

static cluster_voice *alloc_voice(cluster_t *c, int *needs_steal)
{
    *needs_steal = 0;
    for (int i = 0; i < CLUSTER_VOICES; i++)
        if (c->voice[i].stage == VS_IDLE)
            return &c->voice[i];
    *needs_steal = 1;
    cluster_voice *best = NULL;
    for (int i = 0; i < CLUSTER_VOICES; i++) {
        cluster_voice *v = &c->voice[i];
        if (v->stage == VS_RELEASE && (!best || best->stage != VS_RELEASE || v->env < best->env))
            best = v;
    }
    if (best)
        return best;
    for (int i = 0; i < CLUSTER_VOICES; i++) {
        cluster_voice *v = &c->voice[i];
        if (v->stage == VS_STEAL || v->stage == VS_STEAL_DONE)
            continue;
        if (!best || v->age < best->age)
            best = v;
    }
    if (best)
        return best;
    return &c->voice[0]; /* all voices are already fading out for a stolen note: reuse the first */
}

void cluster_note_on(cluster_t *c, int note, int velocity)
{
    if (velocity <= 0) {
        cluster_note_off(c, note);
        return;
    }
    if (note < 0 || note > 127)
        return;
    const float vel = (float)(velocity > 127 ? 127 : velocity) * (1.0f / 127.0f);
    /* a note that is already sounding is released first, the new one takes another voice */
    for (int i = 0; i < CLUSTER_VOICES; i++) {
        cluster_voice *o = &c->voice[i];
        if (o->note == note && (o->stage == VS_ATTACK || o->stage == VS_DECAY))
            voice_release(o);
        if (o->note == note && (o->stage == VS_STEAL || o->stage == VS_STEAL_DONE))
            o->pending_note = -1; /* a queued retrigger of the same note is replaced below */
    }
    int steal;
    cluster_voice *v = alloc_voice(c, &steal);
    if (!steal) {
        voice_start(c, v, note, vel);
        return;
    }
    v->held = 0;
    v->sustained = 0;
    v->pending_note = note;
    v->pending_vel = vel;
    if (v->stage == VS_STEAL || v->stage == VS_STEAL_DONE)
        return; /* already fading out, only the queued note changed */
    v->stage = VS_STEAL;
    v->steal_dec = v->env / (STEAL_SECONDS * c->sr);
    if (v->steal_dec <= 0.0f)
        v->stage = VS_STEAL_DONE;
}

void cluster_note_off(cluster_t *c, int note)
{
    /* release the newest held voice for this note */
    cluster_voice *pick = NULL;
    for (int i = 0; i < CLUSTER_VOICES; i++) {
        cluster_voice *v = &c->voice[i];
        if (v->pending_note == note)
            v->pending_note = -1; /* the key came up before a stolen voice finished fading: the note never starts */
        if (v->note == note && v->held && (v->stage == VS_ATTACK || v->stage == VS_DECAY))
            if (!pick || v->age > pick->age)
                pick = v;
    }
    if (!pick)
        return;
    if (c->pedal) {
        pick->held = 0;
        pick->sustained = 1;
    } else {
        voice_release(pick);
    }
}

void cluster_pitch_bend(cluster_t *c, float value)
{
    c->bend = clampf(value, -1.0f, 1.0f);
}

void cluster_mod_wheel(cluster_t *c, float value)
{
    c->mod = clampf(value, 0.0f, 1.0f);
}

void cluster_sustain(cluster_t *c, int down)
{
    c->pedal = down != 0;
    if (!c->pedal)
        for (int i = 0; i < CLUSTER_VOICES; i++)
            if (c->voice[i].sustained)
                voice_release(&c->voice[i]);
}

void cluster_all_off(cluster_t *c)
{
    c->pedal = 0;
    for (int i = 0; i < CLUSTER_VOICES; i++) {
        cluster_voice *v = &c->voice[i];
        v->pending_note = -1;
        voice_release(v);
    }
}

void cluster_midi(cluster_t *c, const uint8_t *msg, int len)
{
    if (len < 2)
        return;
    const int st = msg[0] & 0xF0;
    const int d1 = msg[1] & 0x7F;
    const int d2 = len > 2 ? (msg[2] & 0x7F) : 0;
    switch (st) {
    case 0x90:
        cluster_note_on(c, d1, d2);
        break;
    case 0x80:
        cluster_note_off(c, d1);
        break;
    case 0xE0:
        cluster_pitch_bend(c, (float)((d2 << 7 | d1) - 8192) / 8192.0f);
        break;
    case 0xB0:
        if (d1 == 1)
            cluster_mod_wheel(c, (float)d2 / 127.0f);
        else if (d1 == 64)
            cluster_sustain(c, d2 >= 64);
        else if (d1 == 120 || d1 == 123)
            cluster_all_off(c);
        else if (d1 == 121) {
            cluster_mod_wheel(c, 0.0f);
            cluster_pitch_bend(c, 0.0f);
            cluster_sustain(c, 0);
        }
        break;
    default:
        break;
    }
}

static void tick(cluster_t *c)
{
    c->bend_s += 0.5f * (c->bend - c->bend_s);
    const float inv = 1.0f / (float)c->tick_len;
    for (int k = 0; k < CLUSTER_VOICES; k++) {
        cluster_voice *v = &c->voice[k];
        if (v->stage == VS_IDLE)
            continue;
        /* the ramp that just ended landed on the "next" values */
        v->fb_cur = v->fb_next;
        for (int i = 0; i < CLUSTER_OSCS; i++) {
            v->det_cur[i] = v->det_next[i];
            v->amp_cur[i] = v->amp_next[i];
        }
        voice_targets(c, v, 0);
        v->fb_step = (v->fb_next - v->fb_cur) * inv;
        for (int i = 0; i < CLUSTER_OSCS; i++) {
            v->det_step[i] = (v->det_next[i] - v->det_cur[i]) * inv;
            v->amp_step[i] = (v->amp_next[i] - v->amp_cur[i]) * inv;
            const float a = v->g[i] * v->env * v->vel;
            if (a > c->osc_act[i])
                c->osc_act[i] = a > 1.0f ? 1.0f : a;
        }
    }
}

/* Amp envelope for one segment into c->venv. */
static void voice_env(cluster_t *c, cluster_voice *v, int n)
{
    const cluster_params *p = &c->target;
    float *e = c->venv;
    const float sr = c->sr;
    const float att_inc = 1.0f / (cluster_env_time(p->attack) * sr);
    const float dco = expf(-4.6f / (cluster_env_time(p->decay) * sr));
    const float rco = expf(-6.9f / (cluster_env_time(p->release) * sr));
    const float sus = p->sustain;
    float env = v->env;
    int j = 0;
    for (; j < n; j++) {
        switch (v->stage) {
        case VS_ATTACK:
            env += att_inc;
            if (env >= 1.0f) {
                env = 1.0f;
                v->stage = VS_DECAY;
            }
            break;
        case VS_DECAY:
            env = sus + flush(env - sus) * dco;
            break;
        case VS_RELEASE:
            env *= rco;
            if (env < 1e-5f) {
                env = 0.0f;
                v->stage = VS_IDLE;
            }
            break;
        case VS_STEAL:
            env -= v->steal_dec;
            if (env <= 0.0f) {
                env = 0.0f;
                v->stage = VS_STEAL_DONE;
            }
            break;
        default:
            env = 0.0f;
            break;
        }
        e[j] = env;
    }
    v->env = env;
}

static void voice_render(cluster_t *c, cluster_voice *v, float *outl, float *outr, int n)
{
    voice_env(c, v, n);

    const float cycles = fminf(note_freq(v->note, c->bend_s * c->target.bend_range) / c->sr, MAX_CYCLES);
    float *vl = c->vl, *vr = c->vr;
    memset(vl, 0, sizeof(float) * (size_t)n);
    memset(vr, 0, sizeof(float) * (size_t)n);

    for (int i = 0; i < CLUSTER_OSCS; i++) {
        if (v->amp_cur[i] <= 1e-6f && v->amp_next[i] <= 1e-6f)
            continue;
        const float gl = c->pan_l[i], gr = c->pan_r[i];
        uint32_t ph = v->phase[i];
        float y = v->y[i];
        float det = v->det_cur[i], amp = v->amp_cur[i], fb = v->fb_cur;
        const float dstep = v->det_step[i], astep = v->amp_step[i], fstep = v->fb_step;
        for (int j = 0; j < n; j++) {
            ph += (uint32_t)(int32_t)(cycles * (1.0f + det) * TWO_POW_32);
            const uint32_t m = (uint32_t)(int32_t)(fb * y * TWO_POW_32);
            y = sine_lookup(c, ph + m);
            const float a = y * amp;
            vl[j] += a * gl;
            vr[j] += a * gr;
            det += dstep;
            amp += astep;
            fb += fstep;
        }
        v->phase[i] = ph;
        v->y[i] = y;
        /* the ramps run over a whole tick, a segment may be shorter: carry the position forward */
        v->det_cur[i] = det;
        v->amp_cur[i] = amp;
    }
    /* the same additions the oscillator loops made, so the result does not depend on how the host splits blocks */
    for (int j = 0; j < n; j++)
        v->fb_cur += v->fb_step;

    const float g = v->vel * VOICE_GAIN;
    const float *e = c->venv;
    for (int j = 0; j < n; j++) {
        const float k = e[j] * g;
        outl[j] += vl[j] * k;
        outr[j] += vr[j] * k;
    }

    if (v->stage == VS_STEAL_DONE) {
        if (v->pending_note >= 0)
            voice_start(c, v, v->pending_note, v->pending_vel);
        else
            v->stage = VS_IDLE;
    }
}

void cluster_render(cluster_t *c, float *left, float *right, int frames)
{
    if (frames <= 0)
        return;
    memset(left, 0, sizeof(float) * (size_t)frames);
    memset(right, 0, sizeof(float) * (size_t)frames);

    int done = 0;
    while (done < frames) {
        if (c->tick_pos == 0)
            tick(c);
        int n = c->tick_len - c->tick_pos;
        if (n > frames - done)
            n = frames - done;

        float *l = left + done, *r = right + done;
        for (int k = 0; k < CLUSTER_VOICES; k++)
            if (c->voice[k].stage != VS_IDLE)
                voice_render(c, &c->voice[k], l, r, n);

        const float gain = level_gain(c->target.level);
        float peak = c->out_peak;
        for (int j = 0; j < n; j++) {
            c->gain_s += c->gain_coef * (gain - c->gain_s);
            l[j] = soft_ceiling(l[j] * c->gain_s);
            r[j] = soft_ceiling(r[j] * c->gain_s);
            const float a = fmaxf(fabsf(l[j]), fabsf(r[j]));
            peak = a > peak ? a : peak;
        }
        c->out_peak = peak;

        c->tick_pos += n;
        if (c->tick_pos >= c->tick_len)
            c->tick_pos = 0;
        done += n;
    }
}

float cluster_take_level(cluster_t *c)
{
    const float p = c->out_peak;
    c->out_peak = 0.0f;
    return p;
}

int cluster_take_activity(cluster_t *c, float act[CLUSTER_OSCS])
{
    int voices = 0;
    for (int i = 0; i < CLUSTER_OSCS; i++) {
        act[i] = c->osc_act[i];
        c->osc_act[i] = 0.0f;
    }
    for (int k = 0; k < CLUSTER_VOICES; k++)
        if (c->voice[k].stage != VS_IDLE)
            voices++;
    return voices;
}
