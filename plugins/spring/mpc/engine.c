/* Spring as an MPC OS effect: glue between the shared DSP core and the mpc-vst-plugins wrapper.
 * The wrapper hands parameters over as key/value strings and audio as interleaved int16 stereo
 * in blocks of up to 128 frames at 44.1 kHz. */
#include "engine.h"
#include "spring_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    spring_t core;
    spring_params params;
} inst_t;

static void *eng_create(const char *data_dir)
{
    (void)data_dir;
    inst_t *p = (inst_t *)calloc(1, sizeof *p);
    if (!p)
        return NULL;
    spring_init(&p->core, 44100.0f);
    p->params.tone = SPRING_DEFAULT_TONE;
    p->params.turns = SPRING_DEFAULT_TURNS;
    p->params.damp = SPRING_DEFAULT_DAMP;
    p->params.level = SPRING_DEFAULT_LEVEL;
    spring_set_params(&p->core, &p->params);
    spring_reset(&p->core);
    return p;
}

static void eng_destroy(void *inst)
{
    free(inst);
}

static void eng_midi(void *inst, const uint8_t *msg, int len)
{
    (void)inst;
    (void)msg;
    (void)len;
}

static void eng_set_param(void *inst, const char *key, const char *val)
{
    inst_t *p = (inst_t *)inst;
    const float v = (float)atof(val);
    if (!strcmp(key, "tone"))
        p->params.tone = v;
    else if (!strcmp(key, "turns"))
        p->params.turns = v;
    else if (!strcmp(key, "damp"))
        p->params.damp = v;
    else if (!strcmp(key, "level"))
        p->params.level = v;
    else
        return;
    spring_set_params(&p->core, &p->params);
}

static int eng_get_param(void *inst, const char *key, char *buf, int buf_len)
{
    inst_t *p = (inst_t *)inst;
    if (!strcmp(key, "tone"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.tone);
    if (!strcmp(key, "turns"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.turns);
    if (!strcmp(key, "damp"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.damp);
    if (!strcmp(key, "level"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.level);
    /* readable value text for the MPC screen */
    if (!strcmp(key, "tone_display")) {
        const float hz = spring_tone_hz(p->params.tone);
        return hz >= 1000.0f ? snprintf(buf, (size_t)buf_len, "%.1fk", (double)(hz / 1000.0f))
                             : snprintf(buf, (size_t)buf_len, "%d", (int)(hz + 0.5f));
    }
    if (!strcmp(key, "turns_display"))
        return snprintf(buf, (size_t)buf_len, "%d", (int)(p->params.turns * 100.0f + 0.5f));
    if (!strcmp(key, "damp_display"))
        return snprintf(buf, (size_t)buf_len, "%.1fs", (double)spring_rt60(p->params.damp));
    if (!strcmp(key, "level_display"))
        return snprintf(buf, (size_t)buf_len, "%d", (int)(p->params.level * 100.0f + 0.5f));
    return 0;
}

static void eng_render(void *inst, int16_t *out_lr, int frames)
{
    (void)inst;
    memset(out_lr, 0, sizeof(int16_t) * 2u * (size_t)frames);
}

static inline int16_t to_i16(float x)
{
    x *= 32768.0f;
    x = x > 32767.0f ? 32767.0f : (x < -32768.0f ? -32768.0f : x);
    return (int16_t)(x < 0.0f ? x - 0.5f : x + 0.5f);
}

static void eng_process(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames)
{
    inst_t *p = (inst_t *)inst;
    float l[128], r[128];
    int done = 0;
    while (done < frames) {
        const int n = frames - done > 128 ? 128 : frames - done;
        for (int i = 0; i < n; i++) {
            l[i] = (float)in_lr[2 * (done + i)] * (1.0f / 32768.0f);
            r[i] = (float)in_lr[2 * (done + i) + 1] * (1.0f / 32768.0f);
        }
        spring_process(&p->core, l, r, n);
        for (int i = 0; i < n; i++) {
            out_lr[2 * (done + i)] = to_i16(l[i]);
            out_lr[2 * (done + i) + 1] = to_i16(r[i]);
        }
        done += n;
    }
}

static const mpc_engine_t ENGINE = {
    eng_create, eng_destroy, eng_midi, eng_set_param, eng_get_param, eng_render, eng_process, NULL,
};

const mpc_engine_t *mpc_engine(void)
{
    return &ENGINE;
}
