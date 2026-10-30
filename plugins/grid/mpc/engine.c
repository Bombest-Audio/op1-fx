/* Grid as an MPC OS effect: glue between the shared DSP core and the mpc-vst-plugins wrapper.
 * The wrapper hands parameters over as key/value strings and audio as interleaved int16 stereo
 * in blocks of up to 128 frames at 44.1 kHz. The mesh memory is allocated once per instance. */
#include "engine.h"
#include "op1fx_act.h"
#include "grid_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MPC_RATE 44100.0f

typedef struct {
    grid_t core;
    grid_params params;
    float *mesh;
} inst_t;

static void *eng_create(const char *data_dir)
{
    (void)data_dir;
    inst_t *p = (inst_t *)calloc(1, sizeof *p);
    if (!p)
        return NULL;
    p->mesh = (float *)calloc(grid_memory_floats(MPC_RATE), sizeof(float));
    if (!p->mesh) {
        free(p);
        return NULL;
    }
    grid_init(&p->core, MPC_RATE, p->mesh);
    p->params.x = GRID_DEFAULT_X;
    p->params.y = GRID_DEFAULT_Y;
    p->params.zfb = GRID_DEFAULT_ZFB;
    p->params.mix = GRID_DEFAULT_MIX;
    grid_set_params(&p->core, &p->params);
    grid_reset(&p->core);
    return p;
}

static void eng_destroy(void *inst)
{
    inst_t *p = (inst_t *)inst;
    if (p)
        free(p->mesh);
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
    if (!strcmp(key, "x"))
        p->params.x = v;
    else if (!strcmp(key, "y"))
        p->params.y = v;
    else if (!strcmp(key, "zfb"))
        p->params.zfb = v;
    else if (!strcmp(key, "mix"))
        p->params.mix = v;
    else
        return;
    grid_set_params(&p->core, &p->params);
}

static int eng_get_param(void *inst, const char *key, char *buf, int buf_len)
{
    inst_t *p = (inst_t *)inst;
    if (!strcmp(key, "x"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.x);
    if (!strcmp(key, "y"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.y);
    if (!strcmp(key, "zfb"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.zfb);
    if (!strcmp(key, "mix"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.mix);
    /* readable value text for the MPC screen: X and Y in cells, Z FDB and MIX in percent */
    if (!strcmp(key, "x_display"))
        return snprintf(buf, (size_t)buf_len, "%d", grid_cells_from_norm(p->params.x));
    if (!strcmp(key, "y_display"))
        return snprintf(buf, (size_t)buf_len, "%d", grid_cells_from_norm(p->params.y));
    if (!strcmp(key, "zfb_display"))
        return snprintf(buf, (size_t)buf_len, "%d", (int)(p->params.zfb * 100.0f + 0.5f));
    if (!strcmp(key, "mix_display"))
        return snprintf(buf, (size_t)buf_len, "%d", (int)(p->params.mix * 100.0f + 0.5f));
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
        grid_process(&p->core, l, r, n);
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
    return op1_act_wrap(&ENGINE_BASE, "x");
}
