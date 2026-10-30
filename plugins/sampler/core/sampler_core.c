#include "sampler_core.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979323846f
#define MIN_REGION 8       /* smallest played region in source frames */
#define ONESHOT_FADE 32    /* source frames faded at the end of a one-shot so it does not end on a step */
#define SOFT_KNEE 0.75f    /* output is left alone below this level and rounded off above it */
#define VOICE_GAIN 0.7f    /* one full-scale voice at full velocity and level peaks here, before the soft limiter */
#define VIB_HZ 5.5f
#define VIB_SEMIS 0.5f     /* vibrato depth at full mod wheel, +- semitones */

static float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

static int clampi(int x, int lo, int hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

float sampler_level_gain(float norm)
{
    norm = clampf(norm, 0.0f, 1.0f);
    return norm * norm;
}

float sampler_tune_from_norm(float norm)
{
    return (clampf(norm, 0.0f, 1.0f) - 0.5f) * (2.0f * SAMPLER_TUNE_RANGE);
}

float sampler_norm_from_tune(float semitones)
{
    return clampf(semitones, -SAMPLER_TUNE_RANGE, SAMPLER_TUNE_RANGE) / (2.0f * SAMPLER_TUNE_RANGE) + 0.5f;
}

void sampler_default_params(sampler_params *p)
{
    p->start = SAMPLER_DEFAULT_START;
    p->end = SAMPLER_DEFAULT_END;
    p->tune = SAMPLER_DEFAULT_TUNE;
    p->loop = SAMPLER_DEFAULT_LOOP;
    p->attack = SAMPLER_DEFAULT_ATTACK;
    p->decay = SAMPLER_DEFAULT_DECAY;
    p->sustain = SAMPLER_DEFAULT_SUSTAIN;
    p->release = SAMPLER_DEFAULT_RELEASE;
    p->level = SAMPLER_DEFAULT_LEVEL;
    p->root = SAMPLER_DEFAULT_ROOT;
    p->bend_range = SAMPLER_DEFAULT_BEND_RANGE;
}

/* One-pole smoothing coefficient per control tick for a 5 ms time constant. */
static float smooth_coeff(float sr)
{
    return 1.0f - expf(-(float)SAMPLER_TICK / (0.005f * sr));
}

static float time_coeff(float seconds, float sr)
{
    const float n = seconds * sr;
    return n < 1.0f ? 0.0f : expf(-6.907755f / n); /* -60 dB over the given time */
}

static void update_envelope_constants(sampler_t *s)
{
    const sampler_params *p = &s->target;
    const float att = p->attack < 0.0002f ? 0.0002f : p->attack;
    s->att_inc = 1.0f / (att * s->sr);
    s->dec_c = time_coeff(p->decay, s->sr);
    s->rel_c = time_coeff(p->release, s->sr);
}

void sampler_set_params(sampler_t *s, const sampler_params *p)
{
    s->target = *p;
    s->target.start = clampf(p->start, 0.0f, 1.0f);
    s->target.end = clampf(p->end, 0.0f, 1.0f);
    s->target.tune = clampf(p->tune, -SAMPLER_TUNE_RANGE, SAMPLER_TUNE_RANGE);
    s->target.loop = clampf(p->loop, 0.0f, 1.0f);
    s->target.sustain = clampf(p->sustain, 0.0f, 1.0f);
    s->target.level = clampf(p->level, 0.0f, 1.0f);
    s->target.root = clampf(p->root, 0.0f, 127.0f);
    s->target.bend_range = clampf(p->bend_range, 0.0f, 24.0f);
    if (!(s->target.attack >= 0.0f)) s->target.attack = 0.0f; /* also catches NaN */
    if (!(s->target.decay >= 0.0f)) s->target.decay = 0.0f;
    if (!(s->target.release >= 0.0f)) s->target.release = 0.0f;
    update_envelope_constants(s);
}

void sampler_init(sampler_t *s, float sample_rate)
{
    memset(s, 0, sizeof *s);
    if (!(sample_rate >= 8000.0f)) sample_rate = 8000.0f;
    s->sr = sample_rate;
    s->c_smooth = smooth_coeff(sample_rate);
    s->resid_c = expf(-1.0f / (0.002f * sample_rate));
    sampler_default_params(&s->target);
    update_envelope_constants(s);
    sampler_reset(s);
}

void sampler_reset(sampler_t *s)
{
    memset(s->voice, 0, sizeof s->voice);
    s->s_semi = s->target.tune;
    s->s_level = sampler_level_gain(s->target.level);
    s->pm_cur = exp2f(s->s_semi * (1.0f / 12.0f));
    s->pm_d = 0.0f;
    s->level_cur = s->s_level;
    s->level_d = 0.0f;
    s->tick_left = 0;
    s->vib_phase = 0.0f;
    s->resid = 0.0f;
    s->bend_norm = 0.0f;
    s->mod_wheel = 0.0f;
    s->pedal = 0;
    s->act_peak = 0.0f;
    s->act_head = -1.0f;
}

/* Cut every voice. What they were putting out is kept as a remainder that dies away over about 2 ms. */
static void cut_all(sampler_t *s)
{
    for (int i = 0; i < SAMPLER_VOICES; i++) {
        if (s->voice[i].state != 0) s->resid += s->voice[i].last;
        memset(&s->voice[i], 0, sizeof s->voice[i]);
    }
}

void sampler_set_sample(sampler_t *s, const sampler_sample *smp)
{
    cut_all(s);
    if (smp && smp->data && smp->frames >= 2 && smp->rate > 0.0f)
        s->sample = *smp;
    else
        memset(&s->sample, 0, sizeof s->sample);
}

/* ---- notes ---- */

static int pick_voice(sampler_t *s, int note)
{
    /* Same note already sounding: take that voice back, so a retrigger never doubles up. */
    for (int i = 0; i < SAMPLER_VOICES; i++)
        if (s->voice[i].state != 0 && s->voice[i].note == note) return i;
    for (int i = 0; i < SAMPLER_VOICES; i++)
        if (s->voice[i].state == 0) return i;
    /* Everything is busy: the quietest releasing voice goes first, otherwise the oldest. */
    int best = -1;
    float best_env = 2.0f;
    for (int i = 0; i < SAMPLER_VOICES; i++)
        if (s->voice[i].state == 3 && s->voice[i].env < best_env) {
            best = i;
            best_env = s->voice[i].env;
        }
    if (best >= 0) return best;
    best = 0;
    for (int i = 1; i < SAMPLER_VOICES; i++)
        if (s->voice[i].age < s->voice[best].age) best = i;
    return best;
}

void sampler_note_on(sampler_t *s, int note, int velocity)
{
    if (velocity <= 0) {
        sampler_note_off(s, note);
        return;
    }
    note = clampi(note, 0, 127);
    if (s->sample.frames < 2) { /* nothing to play, but the activity counter still moves */
        s->serial++;
        return;
    }
    const int vi = pick_voice(s, note);
    sampler_voice *v = &s->voice[vi];
    if (v->state != 0) s->resid += v->last; /* stolen or retriggered: its output carries on as a remainder */

    const sampler_params *p = &s->target;
    const int frames = s->sample.frames;
    const int min_region = frames < MIN_REGION ? frames : MIN_REGION;
    int start = clampi((int)(p->start * (float)frames), 0, frames - 1);
    int end = clampi((int)(p->end * (float)frames), 0, frames);
    if (end < start + min_region) end = start + min_region;
    if (end > frames) {
        end = frames;
        start = frames - min_region;
    }
    const int region = end - start;

    memset(v, 0, sizeof *v);
    v->state = 1;
    v->note = note;
    v->held = 1;
    v->age = ++s->serial;
    v->vel = (float)clampi(velocity, 1, 127) / 127.0f;
    v->env = 0.0f;
    v->start = start;
    v->end = end;
    v->pos = (double)start;
    v->base_inc = pow(2.0, ((double)note - (double)p->root) / 12.0) * (double)s->sample.rate / (double)s->sr;
    v->loop = p->loop > 0.0f && region >= 2 * MIN_REGION;
    v->xf = v->loop ? clampi((int)(p->loop * 0.5f * (float)region), 1, region / 2 - 1) : 0;
    v->fade = region / 2 < ONESHOT_FADE ? region / 2 : ONESHOT_FADE;
}

static void begin_release(sampler_voice *v)
{
    if (v->state != 0) v->state = 3;
    v->held = 0;
    v->sustained = 0;
}

void sampler_note_off(sampler_t *s, int note)
{
    for (int i = 0; i < SAMPLER_VOICES; i++) {
        sampler_voice *v = &s->voice[i];
        if (v->state == 0 || v->state == 3 || !v->held || v->note != note) continue;
        if (s->pedal) {
            v->held = 0;
            v->sustained = 1;
        } else {
            begin_release(v);
        }
    }
}

void sampler_pitch_bend(sampler_t *s, float norm)
{
    s->bend_norm = clampf(norm, -1.0f, 1.0f);
}

void sampler_mod_wheel(sampler_t *s, float norm)
{
    s->mod_wheel = clampf(norm, 0.0f, 1.0f);
}

void sampler_sustain_pedal(sampler_t *s, int down)
{
    s->pedal = down != 0;
    if (!s->pedal)
        for (int i = 0; i < SAMPLER_VOICES; i++)
            if (s->voice[i].sustained) begin_release(&s->voice[i]);
}

void sampler_all_off(sampler_t *s)
{
    s->pedal = 0;
    for (int i = 0; i < SAMPLER_VOICES; i++) begin_release(&s->voice[i]);
}

/* ---- rendering ---- */

static inline float at(const float *d, int n, int i)
{
    return (i < 0 || i >= n) ? 0.0f : d[i];
}

/* Four-point cubic (Catmull-Rom) interpolation. pos must be >= 0. */
static inline float read_hermite(const float *d, int n, double pos)
{
    const int i = (int)pos;
    const float t = (float)(pos - (double)i);
    const float a = at(d, n, i - 1), b = at(d, n, i), c = at(d, n, i + 1), e = at(d, n, i + 2);
    const float c1 = 0.5f * (c - a);
    const float c2 = a - 2.5f * b + 2.0f * c - 0.5f * e;
    const float c3 = 0.5f * (e - a) + 1.5f * (b - c);
    return ((c3 * t + c2) * t + c1) * t + b;
}

static inline float soft_limit(float x)
{
    const float a = fabsf(x);
    if (a <= SOFT_KNEE) return x;
    const float room = 1.0f - SOFT_KNEE;
    const float y = SOFT_KNEE + room * tanhf((a - SOFT_KNEE) / room);
    return x < 0.0f ? -y : y;
}

static void control_tick(sampler_t *s)
{
    const sampler_params *p = &s->target;
    s->vib_phase += VIB_HZ * (float)SAMPLER_TICK / s->sr;
    if (s->vib_phase >= 1.0f) s->vib_phase -= 1.0f;
    const float vib = s->mod_wheel * VIB_SEMIS * sinf(2.0f * PI_F * s->vib_phase);

    const float semi_target = p->tune + s->bend_norm * p->bend_range;
    s->s_semi += (semi_target - s->s_semi) * s->c_smooth;
    s->s_level += (sampler_level_gain(p->level) - s->s_level) * s->c_smooth;

    const float pm_next = exp2f((s->s_semi + vib) * (1.0f / 12.0f));
    s->pm_d = (pm_next - s->pm_cur) / (float)SAMPLER_TICK;
    s->level_d = (s->s_level - s->level_cur) / (float)SAMPLER_TICK;
    s->tick_left = SAMPLER_TICK;
}

/* Render n <= SAMPLER_TICK frames, adding into mix. */
static void render_chunk(sampler_t *s, float *mix, int n)
{
    const float *d = s->sample.data;
    const int total = s->sample.frames;
    for (int vi = 0; vi < SAMPLER_VOICES; vi++) {
        sampler_voice *v = &s->voice[vi];
        if (v->state == 0) continue;
        float pm = s->pm_cur;
        for (int i = 0; i < n; i++) {
            pm += s->pm_d;
            /* envelope */
            if (v->state == 1) {
                v->env += s->att_inc;
                if (v->env >= 1.0f) {
                    v->env = 1.0f;
                    v->state = 2;
                }
            } else if (v->state == 2) {
                v->env = s->target.sustain + (v->env - s->target.sustain) * s->dec_c;
                if (s->target.sustain < 1e-5f && v->env < 1e-5f) v->state = 0;
            } else {
                v->env *= s->rel_c;
                if (v->env < 1e-5f) v->state = 0;
            }
            if (v->state == 0) {
                v->last = 0.0f;
                break;
            }

            /* position and loop */
            float x;
            const double pos = v->pos;
            if (v->loop) {
                x = read_hermite(d, total, pos);
                const int xs = v->end - v->xf;
                if (pos >= (double)xs) {
                    const float w = (float)((pos - (double)xs) / (double)v->xf);
                    const double other = (double)v->start + (pos - (double)xs);
                    x += w * (read_hermite(d, total, other) - x);
                }
            } else {
                x = read_hermite(d, total, pos);
                const double left = (double)v->end - pos;
                if (left < (double)v->fade) x *= (float)(left / (double)v->fade);
            }
            v->pos += v->base_inc * (double)pm;
            if (v->loop) {
                if (v->pos >= (double)v->end) v->pos -= (double)(v->end - v->start - v->xf);
            } else if (v->pos >= (double)v->end) {
                v->state = 0; /* ran off the end of the region; the fade above has already brought it to zero */
            }

            const float y = x * v->env * v->vel * VOICE_GAIN;
            v->last = y;
            mix[i] += y;
            if (v->state == 0) {
                v->last = 0.0f;
                break;
            }
        }
    }
}

void sampler_render(sampler_t *s, float *l, float *r, int frames)
{
    float mix[SAMPLER_TICK];
    int done = 0;
    float peak = s->act_peak;
    while (done < frames) {
        if (s->tick_left <= 0) control_tick(s);
        int n = frames - done;
        if (n > s->tick_left) n = s->tick_left;
        memset(mix, 0, sizeof mix);
        if (s->sample.frames >= 2) render_chunk(s, mix, n);
        float lc = s->level_cur;
        for (int i = 0; i < n; i++) {
            lc += s->level_d;
            s->resid *= s->resid_c;
            if (fabsf(s->resid) < 1e-20f) s->resid = 0.0f; /* denormal guard */
            float y = soft_limit((mix[i] + s->resid) * lc);
            l[done + i] = y;
            r[done + i] = y;
            if (fabsf(y) > peak) peak = fabsf(y);
        }
        s->level_cur = lc;
        /* accumulate one sample at a time, exactly as the voices do, so the result does not depend on the chunking */
        for (int i = 0; i < n; i++) s->pm_cur += s->pm_d;
        s->tick_left -= n;
        done += n;
    }
    s->act_peak = peak;

    /* playhead of the newest sounding voice */
    unsigned newest = 0;
    float head = -1.0f;
    for (int i = 0; i < SAMPLER_VOICES; i++)
        if (s->voice[i].state != 0 && s->voice[i].age >= newest && s->sample.frames > 0) {
            newest = s->voice[i].age;
            head = (float)(s->voice[i].pos / (double)s->sample.frames);
        }
    s->act_head = head;
}

int sampler_active_voices(const sampler_t *s)
{
    int n = 0;
    for (int i = 0; i < SAMPLER_VOICES; i++)
        if (s->voice[i].state != 0) n++;
    return n;
}

void sampler_take_activity(sampler_t *s, sampler_activity *out)
{
    out->peak = s->act_peak;
    out->voices = sampler_active_voices(s);
    out->notes = s->serial;
    out->head = s->act_head;
    s->act_peak = 0.0f;
}

/* ---- built-in samples ---- */

static unsigned lcg(unsigned *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static float noise(unsigned *state)
{
    return (float)((int)(lcg(state) >> 8) - 8388608) / 8388608.0f;
}

int sampler_demo_frames(int kind, float rate)
{
    switch (kind) {
    case 0: return (int)(2.0f * rate);
    case 1: return (int)rate; /* exactly one second, so the whole sample is a whole number of cycles */
    default: return (int)(1.5f * rate);
    }
}

int sampler_demo_root(int kind)
{
    return kind == 2 ? 36 : 60;
}

const char *sampler_demo_name(int kind)
{
    switch (kind) {
    case 0: return "Bar";
    case 1: return "Pad";
    default: return "Pluck";
    }
}

static void normalize(float *out, int frames, float target)
{
    float pk = 0.0f;
    for (int i = 0; i < frames; i++)
        if (fabsf(out[i]) > pk) pk = fabsf(out[i]);
    if (pk > 0.0f)
        for (int i = 0; i < frames; i++) out[i] *= target / pk;
}

void sampler_demo_render(int kind, float rate, float *out, int frames)
{
    unsigned rng = 12345u;
    const double two_pi = 6.283185307179586;
    float lp = 0.0f;
    for (int i = 0; i < frames; i++) {
        const double t = (double)i / (double)rate;
        double y = 0.0;
        if (kind == 0) {
            /* struck bar: inharmonic partials at the ratios of a free bar, each with its own decay */
            static const double ratio[3] = {1.0, 2.756, 5.404};
            static const double amp[3] = {1.0, 0.5, 0.22};
            static const double life[3] = {1.8, 0.6, 0.2};
            for (int k = 0; k < 3; k++)
                y += amp[k] * sin(two_pi * 261.6256 * ratio[k] * t) * exp(-t * 6.9 / life[k]);
            lp += 0.35f * (noise(&rng) - lp); /* soft click, gone in a few ms */
            y += 0.4 * (double)lp * exp(-t * 6.9 / 0.006);
        } else if (kind == 1) {
            /* pad: every partial is a whole number of Hz, so one second repeats exactly */
            static const double hz[5] = {131.0, 262.0, 263.0, 524.0, 786.0};
            static const double amp[5] = {0.4, 1.0, 0.7, 0.5, 0.3};
            for (int k = 0; k < 5; k++) y += amp[k] * sin(two_pi * hz[k] * t);
        } else {
            /* plucked low string: harmonics that die faster the higher they are */
            for (int n = 1; n <= 12; n++)
                y += (1.0 / (double)n) * sin(two_pi * 65.4064 * (double)n * t) * exp(-t * (1.5 + 0.9 * (double)n));
            lp += 0.5f * (noise(&rng) - lp);
            y += 0.3 * (double)lp * exp(-t * 6.9 / 0.01);
        }
        out[i] = (float)y;
    }
    normalize(out, frames, kind == 1 ? 0.7f : 0.8f);
    if (kind != 1) { /* a short fade in and out so the file starts and ends at zero */
        const int fi = (int)(0.001f * rate), fo = (int)(0.02f * rate);
        for (int i = 0; i < fi && i < frames; i++) out[i] *= (float)i / (float)fi;
        for (int i = 0; i < fo && i < frames; i++) out[frames - 1 - i] *= (float)i / (float)fo;
    }
}
