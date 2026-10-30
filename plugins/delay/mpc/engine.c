/* Delay as an MPC OS effect: glue between the shared DSP core and the mpc-vst-plugins wrapper.
 * The wrapper hands parameters over as key/value strings and audio as interleaved int16 stereo
 * in blocks of up to 128 frames at 44.1 kHz. */
#include "engine.h"
#include "delay_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MPC_SAMPLE_RATE 44100.0f

typedef struct {
    delay_t core;
    delay_params params;
    float *mem_l, *mem_r; /* delay line memory, owned here because the core never allocates */
} inst_t;

static void *eng_create(const char *data_dir)
{
    (void)data_dir;
    inst_t *p = (inst_t *)calloc(1, sizeof *p);
    if (!p)
        return NULL;
    const size_t frames = (size_t)delay_buffer_frames(MPC_SAMPLE_RATE);
    p->mem_l = (float *)calloc(frames, sizeof(float));
    p->mem_r = (float *)calloc(frames, sizeof(float));
    if (!p->mem_l || !p->mem_r) {
        free(p->mem_l);
        free(p->mem_r);
        free(p);
        return NULL;
    }
    delay_init(&p->core, MPC_SAMPLE_RATE, p->mem_l, p->mem_r);
    p->params.time = DELAY_DEFAULT_TIME;
    p->params.speed = DELAY_DEFAULT_SPEED;
    p->params.fdb = DELAY_DEFAULT_FDB;
    p->params.input = DELAY_DEFAULT_INPUT;
    delay_set_params(&p->core, &p->params);
    delay_reset(&p->core);
    return p;
}

static void eng_destroy(void *inst)
{
    inst_t *p = (inst_t *)inst;
    if (!p)
        return;
    free(p->mem_l);
    free(p->mem_r);
    free(p);
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
    if (!strcmp(key, "time"))
        p->params.time = v;
    else if (!strcmp(key, "speed"))
        p->params.speed = v;
    else if (!strcmp(key, "fdb"))
        p->params.fdb = v;
    else if (!strcmp(key, "input"))
        p->params.input = v;
    else
        return;
    delay_set_params(&p->core, &p->params);
}

static int eng_get_param(void *inst, const char *key, char *buf, int buf_len)
{
    inst_t *p = (inst_t *)inst;
    if (!strcmp(key, "time"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.time);
    if (!strcmp(key, "speed"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.speed);
    if (!strcmp(key, "fdb"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.fdb);
    if (!strcmp(key, "input"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.input);
    /* readable value text: the real delay in milliseconds (loop length divided by tape speed) */
    if (!strcmp(key, "time_display"))
        return snprintf(buf, (size_t)buf_len, "%d ms",
                        (int)(delay_time_seconds(p->params.time, p->params.speed) * 1000.0f + 0.5f));
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
        delay_process(&p->core, l, r, n);
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
