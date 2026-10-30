/* Cwo as an MPC OS effect: glue between the shared DSP core and the mpc-vst-plugins wrapper.
 * The wrapper hands parameters over as key/value strings and audio as interleaved int16 stereo
 * in blocks of up to 128 frames at 44.1 kHz. */
#include "engine.h"
#include "cwo_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    cwo_t core; /* about 1 MB, mostly delay memory */
    cwo_params params;
} inst_t;

static void *eng_create(const char *data_dir)
{
    (void)data_dir;
    inst_t *p = (inst_t *)calloc(1, sizeof *p);
    if (!p)
        return NULL;
    cwo_init(&p->core, 44100.0f);
    p->params.freq = CWO_DEFAULT_FREQ;
    p->params.delay = CWO_DEFAULT_DELAY;
    p->params.fdback = CWO_DEFAULT_FDBACK;
    p->params.sideband = CWO_DEFAULT_SIDEBAND;
    cwo_set_params(&p->core, &p->params);
    cwo_reset(&p->core);
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
    else if (!strcmp(key, "delay"))
        p->params.delay = v;
    else if (!strcmp(key, "fdback"))
        p->params.fdback = v;
    else if (!strcmp(key, "sideband"))
        p->params.sideband = v;
    else
        return;
    cwo_set_params(&p->core, &p->params);
}

static int eng_get_param(void *inst, const char *key, char *buf, int buf_len)
{
    inst_t *p = (inst_t *)inst;
    if (!strcmp(key, "freq"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.freq);
    if (!strcmp(key, "delay"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.delay);
    if (!strcmp(key, "fdback"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.fdback);
    if (!strcmp(key, "sideband"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.sideband);
    /* readable value text for the MPC screen */
    if (!strcmp(key, "freq_display")) {
        const float hz = cwo_shift_hz(p->params.freq);
        if (hz >= 1000.0f)
            return snprintf(buf, (size_t)buf_len, "%.1fk", (double)(hz / 1000.0f));
        return hz < 10.0f ? snprintf(buf, (size_t)buf_len, "%.1f", (double)hz)
                          : snprintf(buf, (size_t)buf_len, "%d", (int)(hz + 0.5f));
    }
    if (!strcmp(key, "delay_display"))
        return snprintf(buf, (size_t)buf_len, "%d", (int)(cwo_delay_ms(p->params.delay) + 0.5f));
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
        cwo_process(&p->core, l, r, n);
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
