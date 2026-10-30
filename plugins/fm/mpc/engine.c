/* Fm as an MPC OS instrument: glue between the shared DSP core and the mpc-vst-plugins wrapper.
 * The wrapper hands parameters over as key/value strings, MIDI as raw bytes, and wants audio as interleaved
 * int16 stereo at 44.1 kHz. With SAMPLE_ACCURATE it calls render() with any 1..128 frames, so a note starts at
 * its place inside the block. */
#include "engine.h"
#include "fm_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Every parameter here is 0..1. The time controls are exponential so the whole range is usable from a knob:
 *   attack  0.5 ms to 5 s     decay and release  5 ms to 10 s
 * These are not the same curves as the plugin's skewed ranges, so a plugin preset does not carry over by value. */
#define ATTACK_MIN 0.0005f
#define ATTACK_MAX 5.0f
#define TIME_MIN 0.005f
#define TIME_MAX 10.0f

typedef struct {
    float ratio, index, feedback, layer, attack, decay, sustain, release, level;
} knobs_t;

typedef struct {
    fm_t core;
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
    fm_params q;
    fm_default_params(&q); /* the engine extras stay at the OP-1 factory values */
    q.ratio = p->k.ratio;
    q.index = p->k.index;
    q.feedback = p->k.feedback;
    q.layer = p->k.layer;
    q.attack = exp_time(p->k.attack, ATTACK_MIN, ATTACK_MAX);
    q.decay = exp_time(p->k.decay, TIME_MIN, TIME_MAX);
    q.sustain = p->k.sustain;
    q.release = exp_time(p->k.release, TIME_MIN, TIME_MAX);
    q.level = p->k.level;
    fm_set_params(&p->core, &q);
}

static void *eng_create(const char *data_dir)
{
    (void)data_dir;
    inst_t *p = (inst_t *)calloc(1, sizeof *p);
    if (!p)
        return NULL;
    fm_init(&p->core, 44100.0f);
    p->k.ratio = FM_DEFAULT_RATIO;
    p->k.index = FM_DEFAULT_INDEX;
    p->k.feedback = FM_DEFAULT_FEEDBACK;
    p->k.layer = FM_DEFAULT_LAYER;
    p->k.attack = exp_time_inverse(FM_DEFAULT_ATTACK, ATTACK_MIN, ATTACK_MAX);
    p->k.decay = exp_time_inverse(FM_DEFAULT_DECAY, TIME_MIN, TIME_MAX);
    p->k.sustain = FM_DEFAULT_SUSTAIN;
    p->k.release = exp_time_inverse(FM_DEFAULT_RELEASE, TIME_MIN, TIME_MAX);
    p->k.level = FM_DEFAULT_LEVEL;
    push_params(p);
    fm_reset(&p->core);
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
            fm_note_on(&p->core, d1, d2);
        else
            fm_note_off(&p->core, d1);
        break;
    case 0x80:
        fm_note_off(&p->core, d1);
        break;
    case 0xE0:
        fm_pitch_bend(&p->core, (float)(((d2 << 7) | d1) - 8192) / 8192.0f);
        break;
    case 0xB0:
        if (d1 == 1)
            fm_mod_wheel(&p->core, (float)d2 / 127.0f);
        else if (d1 == 64)
            fm_sustain_pedal(&p->core, d2 >= 64);
        else if (d1 == 120 || d1 == 123)
            fm_all_off(&p->core);
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
    if (!strcmp(key, "ratio")) dst = &p->k.ratio;
    else if (!strcmp(key, "index")) dst = &p->k.index;
    else if (!strcmp(key, "feedback")) dst = &p->k.feedback;
    else if (!strcmp(key, "layer")) dst = &p->k.layer;
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

static int eng_get_param(void *inst, const char *key, char *buf, int buf_len)
{
    inst_t *p = (inst_t *)inst;
    const struct { const char *name; float v; } plain[] = {
        {"ratio", p->k.ratio}, {"index", p->k.index}, {"feedback", p->k.feedback}, {"layer", p->k.layer},
        {"attack", p->k.attack}, {"decay", p->k.decay}, {"sustain", p->k.sustain}, {"release", p->k.release},
        {"level", p->k.level},
    };
    for (size_t i = 0; i < sizeof plain / sizeof plain[0]; i++)
        if (!strcmp(key, plain[i].name))
            return snprintf(buf, (size_t)buf_len, "%g", (double)plain[i].v);

    /* readable value text for the MPC screen */
    if (!strcmp(key, "ratio_display"))
        return snprintf(buf, (size_t)buf_len, "%.2f", (double)fm_ratio_value(p->k.ratio));
    if (!strcmp(key, "index_display"))
        return snprintf(buf, (size_t)buf_len, "%d", (int)(p->k.index * 100.0f + 0.5f));
    if (!strcmp(key, "feedback_display"))
        return snprintf(buf, (size_t)buf_len, "%d", (int)(p->k.feedback * 100.0f + 0.5f));
    if (!strcmp(key, "layer_display"))
        return snprintf(buf, (size_t)buf_len, "%d", (int)(p->k.layer * 100.0f + 0.5f));
    if (!strcmp(key, "sustain_display"))
        return snprintf(buf, (size_t)buf_len, "%d", (int)(p->k.sustain * 100.0f + 0.5f));
    if (!strcmp(key, "level_display"))
        return snprintf(buf, (size_t)buf_len, "%d", (int)(p->k.level * 100.0f + 0.5f));
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
        fm_render(&p->core, l, r, n);
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
