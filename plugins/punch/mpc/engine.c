/* Punch as an MPC OS effect: glue between the shared DSP core and the mpc-vst-plugins wrapper.
 * The wrapper hands parameters over as key/value strings and audio as interleaved int16 stereo
 * in blocks of up to 128 frames at 44.1 kHz. */
#include "engine.h"
#include "op1fx_act.h"
#include "punch_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    punch_t core;
    punch_params params;
} inst_t;

static void *eng_create(const char *data_dir)
{
    (void)data_dir;
    inst_t *p = (inst_t *)calloc(1, sizeof *p);
    if (!p)
        return NULL;
    punch_init(&p->core, 44100.0f);
    p->params.freq = PUNCH_DEFAULT_FREQ;
    p->params.punch = PUNCH_DEFAULT_PUNCH;
    p->params.rounds = PUNCH_DEFAULT_ROUNDS;
    p->params.power = PUNCH_DEFAULT_POWER;
    punch_set_params(&p->core, &p->params);
    punch_reset(&p->core);
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
    if (!strcmp(key, "freq"))
        p->params.freq = v;
    else if (!strcmp(key, "punch"))
        p->params.punch = v;
    else if (!strcmp(key, "rounds"))
        p->params.rounds = (int)(v + 0.5f);
    else if (!strcmp(key, "power"))
        p->params.power = v;
    else
        return;
    punch_set_params(&p->core, &p->params);
}

static int eng_get_param(void *inst, const char *key, char *buf, int buf_len)
{
    inst_t *p = (inst_t *)inst;
    if (!strcmp(key, "freq"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.freq);
    if (!strcmp(key, "punch"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.punch);
    if (!strcmp(key, "rounds"))
        return snprintf(buf, (size_t)buf_len, "%d", p->params.rounds);
    if (!strcmp(key, "power"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.power);
    /* readable value text for the MPC screen */
    if (!strcmp(key, "freq_display")) {
        const float hz = punch_freq_hz(p->params.freq);
        return hz >= 1000.0f ? snprintf(buf, (size_t)buf_len, "%.1fk", (double)(hz / 1000.0f))
                             : snprintf(buf, (size_t)buf_len, "%d", (int)(hz + 0.5f));
    }
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
        punch_process(&p->core, l, r, n);
        for (int i = 0; i < n; i++) {
            out_lr[2 * (done + i)] = to_i16(l[i]);
            out_lr[2 * (done + i) + 1] = to_i16(r[i]);
        }
        done += n;
    }
}

static const mpc_engine_t ENGINE_BASE = {
    eng_create, eng_destroy, eng_midi, eng_set_param, eng_get_param, eng_render, eng_process, NULL,
};

const mpc_engine_t *mpc_engine(void)
{
    return op1_act_wrap(&ENGINE_BASE, "freq");
}
