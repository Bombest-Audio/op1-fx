/* Phone as an MPC OS effect: glue between the shared DSP core and the mpc-vst-plugins wrapper.
 * The wrapper hands parameters over as key/value strings and audio as interleaved int16 stereo
 * in blocks of up to 128 frames at 44.1 kHz. */
#include "engine.h"
#include "phone_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    phone_t core; /* about 1.2 MB, so one calloc per instance */
    phone_params params;
} inst_t;

static void *eng_create(const char *data_dir)
{
    (void)data_dir;
    inst_t *p = (inst_t *)calloc(1, sizeof *p);
    if (!p)
        return NULL;
    phone_init(&p->core, 44100.0f);
    p->params.tone = PHONE_DEFAULT_TONE;
    p->params.phonic = PHONE_DEFAULT_PHONIC;
    p->params.baud = PHONE_DEFAULT_BAUD;
    p->params.telematic = PHONE_DEFAULT_TELEMATIC;
    phone_set_params(&p->core, &p->params);
    phone_reset(&p->core);
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
    else if (!strcmp(key, "phonic"))
        p->params.phonic = (int)(v + 0.5f);
    else if (!strcmp(key, "baud"))
        p->params.baud = (int)(v + 0.5f);
    else if (!strcmp(key, "telematic"))
        p->params.telematic = v;
    else
        return;
    phone_set_params(&p->core, &p->params);
}

static int eng_get_param(void *inst, const char *key, char *buf, int buf_len)
{
    inst_t *p = (inst_t *)inst;
    if (!strcmp(key, "tone"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.tone);
    if (!strcmp(key, "phonic"))
        return snprintf(buf, (size_t)buf_len, "%d", p->params.phonic);
    if (!strcmp(key, "baud"))
        return snprintf(buf, (size_t)buf_len, "%d", p->params.baud);
    if (!strcmp(key, "telematic"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)p->params.telematic);
    /* readable value text for the MPC screen: TONE in semitones */
    if (!strcmp(key, "tone_display")) {
        const float st = phone_tone_semitones(p->params.tone);
        return snprintf(buf, (size_t)buf_len, "%s%.1f", st > 0.05f ? "+" : "", (double)st);
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
        phone_process(&p->core, l, r, n);
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
