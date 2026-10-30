/* Phase as an MPC OS instrument: glue between the shared DSP core and the mpc-vst-plugins wrapper.
 * The wrapper hands parameters over as key/value strings, MIDI as raw bytes, and wants audio as interleaved
 * int16 stereo at 44.1 kHz. With SAMPLE_ACCURATE it calls render() with any 1..128 frames, so a note starts at
 * its place inside the block. phase_render() takes any frame count and gives the same output for any split. */
#include "engine.h"
#include "phase_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Every parameter here is 0..1. The time controls are exponential so the whole range is usable from a knob:
 *   attack  1 ms to 5 s     decay and release  5 ms to 10 s
 * The plugin uses its own skewed ranges, so a plugin preset does not carry over by value. */
#define ATTACK_MIN 0.001f
#define ATTACK_MAX 5.0f
#define TIME_MIN 0.005f
#define TIME_MAX 10.0f

typedef struct {
    float a, b, r, w, attack, decay, sustain, release, level;
} knobs_t;

typedef struct {
    phase_t core;
    knobs_t k;
} inst_t;

static float exp_time(float norm, float lo, float hi)
{
    norm = norm < 0.0f ? 0.0f : (norm > 1.0f ? 1.0f : norm);
    return lo * powf(hi / lo, norm);
}

static float exp_time_inverse(float seconds, float lo, float hi)
{
    return logf(seconds / lo) / logf(hi / lo);
}

static void push_params(inst_t *p)
{
    phase_params q;
    phase_default_params(&q); /* bend range stays at its default */
    q.a = p->k.a;
    q.b = p->k.b;
    q.r = p->k.r;
    q.w = p->k.w;
    q.attack = exp_time(p->k.attack, ATTACK_MIN, ATTACK_MAX);
    q.decay = exp_time(p->k.decay, TIME_MIN, TIME_MAX);
    q.sustain = p->k.sustain;
    q.release = exp_time(p->k.release, TIME_MIN, TIME_MAX);
    q.level = p->k.level;
    phase_set_params(&p->core, &q);
}

static void *eng_create(const char *data_dir)
{
    (void)data_dir;
    inst_t *p = (inst_t *)calloc(1, sizeof *p);
    if (!p)
        return NULL;
    phase_init(&p->core, 44100.0f);
    p->k.a = PHASE_DEFAULT_A;
    p->k.b = PHASE_DEFAULT_B;
    p->k.r = PHASE_DEFAULT_R;
    p->k.w = PHASE_DEFAULT_W;
    p->k.attack = exp_time_inverse(PHASE_DEFAULT_ATTACK, ATTACK_MIN, ATTACK_MAX);
    p->k.decay = exp_time_inverse(PHASE_DEFAULT_DECAY, TIME_MIN, TIME_MAX);
    p->k.sustain = PHASE_DEFAULT_SUSTAIN;
    p->k.release = exp_time_inverse(PHASE_DEFAULT_RELEASE, TIME_MIN, TIME_MAX);
    p->k.level = PHASE_DEFAULT_LEVEL;
    push_params(p);
    phase_reset(&p->core);
    return p;
}

static void eng_destroy(void *inst)
{
    free(inst);
}

static void eng_midi(void *inst, const uint8_t *msg, int len)
{
    inst_t *p = (inst_t *)inst;
    if (len < 1)
        return;
    const int status = msg[0] & 0xF0;
    const int d1 = len > 1 ? msg[1] & 0x7F : 0;
    const int d2 = len > 2 ? msg[2] & 0x7F : 0;
    switch (status) {
    case 0x90:
        if (len > 2 && d2 > 0)
            phase_note_on(&p->core, d1, (float)d2 / 127.0f);
        else
            phase_note_off(&p->core, d1);
        break;
    case 0x80:
        phase_note_off(&p->core, d1);
        break;
    case 0xE0:
        phase_pitch_bend(&p->core, (float)(((d2 << 7) | d1) - 8192) / 8192.0f);
        break;
    case 0xB0:
        if (d1 == 1)
            phase_mod_wheel(&p->core, (float)d2 / 127.0f);
        else if (d1 == 64)
            phase_sustain(&p->core, d2 >= 64);
        else if (d1 == 120 || d1 == 123)
            phase_all_off(&p->core);
        break;
    default:
        break;
    }
}

static void eng_set_param(void *inst, const char *key, const char *val)
{
    inst_t *p = (inst_t *)inst;
    const float v = (float)atof(val);
    float *dst = NULL;
    if (!strcmp(key, "a")) dst = &p->k.a;
    else if (!strcmp(key, "b")) dst = &p->k.b;
    else if (!strcmp(key, "r")) dst = &p->k.r;
    else if (!strcmp(key, "w")) dst = &p->k.w;
    else if (!strcmp(key, "attack")) dst = &p->k.attack;
    else if (!strcmp(key, "decay")) dst = &p->k.decay;
    else if (!strcmp(key, "sustain")) dst = &p->k.sustain;
    else if (!strcmp(key, "release")) dst = &p->k.release;
    else if (!strcmp(key, "level")) dst = &p->k.level;
    if (!dst)
        return;
    *dst = v;
    push_params(p);
}

static int time_text(char *buf, int buf_len, float seconds)
{
    return seconds < 1.0f ? snprintf(buf, (size_t)buf_len, "%d ms", (int)(seconds * 1000.0f + 0.5f))
                          : snprintf(buf, (size_t)buf_len, "%.2f s", (double)seconds);
}

static int percent_text(char *buf, int buf_len, float v)
{
    return snprintf(buf, (size_t)buf_len, "%d", (int)(v * 100.0f + 0.5f));
}

static int eng_get_param(void *inst, const char *key, char *buf, int buf_len)
{
    inst_t *p = (inst_t *)inst;
    const struct { const char *name; float v; } plain[] = {
        {"a", p->k.a}, {"b", p->k.b}, {"r", p->k.r}, {"w", p->k.w},
        {"attack", p->k.attack}, {"decay", p->k.decay}, {"sustain", p->k.sustain}, {"release", p->k.release},
        {"level", p->k.level},
    };
    for (size_t i = 0; i < sizeof plain / sizeof plain[0]; i++)
        if (!strcmp(key, plain[i].name))
            return snprintf(buf, (size_t)buf_len, "%g", (double)plain[i].v);

    /* readable value text for the MPC screen */
    if (!strcmp(key, "a_display")) return percent_text(buf, buf_len, p->k.a);
    if (!strcmp(key, "b_display")) return percent_text(buf, buf_len, p->k.b);
    if (!strcmp(key, "r_display")) return percent_text(buf, buf_len, p->k.r);
    if (!strcmp(key, "w_display")) return percent_text(buf, buf_len, p->k.w);
    if (!strcmp(key, "sustain_display")) return percent_text(buf, buf_len, p->k.sustain);
    if (!strcmp(key, "level_display")) return percent_text(buf, buf_len, p->k.level);
    if (!strcmp(key, "attack_display"))
        return time_text(buf, buf_len, exp_time(p->k.attack, ATTACK_MIN, ATTACK_MAX));
    if (!strcmp(key, "decay_display"))
        return time_text(buf, buf_len, exp_time(p->k.decay, TIME_MIN, TIME_MAX));
    if (!strcmp(key, "release_display"))
        return time_text(buf, buf_len, exp_time(p->k.release, TIME_MIN, TIME_MAX));
    return 0;
}

static inline int16_t to_i16(float x)
{
    x *= 32768.0f;
    x = x > 32767.0f ? 32767.0f : (x < -32768.0f ? -32768.0f : x);
    return (int16_t)(x < 0.0f ? x - 0.5f : x + 0.5f);
}

static void eng_render(void *inst, int16_t *out_lr, int frames)
{
    inst_t *p = (inst_t *)inst;
    float l[128], r[128];
    int done = 0;
    while (done < frames) {
        const int n = frames - done > 128 ? 128 : frames - done;
        phase_render(&p->core, l, r, n);
        for (int i = 0; i < n; i++) {
            out_lr[2 * (done + i)] = to_i16(l[i]);
            out_lr[2 * (done + i) + 1] = to_i16(r[i]);
        }
        done += n;
    }
}

/* Instrument: no process(). Add it last, the struct is positional. */
static const mpc_engine_t ENGINE = {
    eng_create, eng_destroy, eng_midi, eng_set_param, eng_get_param, eng_render, NULL, NULL,
};

const mpc_engine_t *mpc_engine(void)
{
    return &ENGINE;
}
