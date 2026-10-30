/* Cluster as an MPC OS instrument: glue between the shared DSP core and the mpc-vst-plugins wrapper.
 * The wrapper hands parameters over as key/value strings, MIDI as raw bytes, and asks for audio as
 * interleaved int16 stereo at 44.1 kHz. With SAMPLE_ACCURATE it calls render() with 1 to 128 frames,
 * so a note starts on its own sample. */
#include "engine.h"
#include "op1fx_act.h"
#include "cluster_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    cluster_t core;
    cluster_params params;
} inst_t;

static void *eng_create(const char *data_dir)
{
    (void)data_dir;
    inst_t *p = (inst_t *)calloc(1, sizeof *p);
    if (!p)
        return NULL;
    cluster_init(&p->core, 44100.0f);
    p->params.oscs = CLUSTER_DEFAULT_OSCS;
    p->params.feedback = CLUSTER_DEFAULT_FEEDBACK;
    p->params.spread = CLUSTER_DEFAULT_SPREAD;
    p->params.wander = CLUSTER_DEFAULT_WANDER;
    p->params.attack = CLUSTER_DEFAULT_ATTACK;
    p->params.decay = CLUSTER_DEFAULT_DECAY;
    p->params.sustain = CLUSTER_DEFAULT_SUSTAIN;
    p->params.release = CLUSTER_DEFAULT_RELEASE;
    p->params.level = CLUSTER_DEFAULT_LEVEL;
    p->params.bend_range = CLUSTER_DEFAULT_BEND;
    cluster_set_params(&p->core, &p->params);
    return p;
}

static void eng_destroy(void *inst)
{
    free(inst);
}

static void eng_midi(void *inst, const uint8_t *msg, int len)
{
    inst_t *p = (inst_t *)inst;
    if (len >= 1)
        cluster_midi(&p->core, msg, len);
}

static void eng_set_param(void *inst, const char *key, const char *val)
{
    inst_t *p = (inst_t *)inst;
    const float v = (float)atof(val);
    if (!strcmp(key, "oscs"))
        p->params.oscs = (int)(v + 0.5f);
    else if (!strcmp(key, "feedback"))
        p->params.feedback = v;
    else if (!strcmp(key, "spread"))
        p->params.spread = v;
    else if (!strcmp(key, "wander"))
        p->params.wander = v;
    else if (!strcmp(key, "attack"))
        p->params.attack = v;
    else if (!strcmp(key, "decay"))
        p->params.decay = v;
    else if (!strcmp(key, "sustain"))
        p->params.sustain = v;
    else if (!strcmp(key, "release"))
        p->params.release = v;
    else if (!strcmp(key, "level"))
        p->params.level = v;
    else if (!strcmp(key, "bend"))
        p->params.bend_range = v;
    else
        return;
    cluster_set_params(&p->core, &p->params);
}

static int time_text(float norm, char *buf, int n)
{
    const float s = cluster_env_time(norm);
    return s < 1.0f ? snprintf(buf, (size_t)n, "%dms", (int)(s * 1000.0f + 0.5f))
                    : snprintf(buf, (size_t)n, "%.1fs", (double)s);
}

static int get_param(void *inst, const char *key, char *buf, int n)
{
    const inst_t *p = (const inst_t *)inst;
    if (!strcmp(key, "oscs"))
        return snprintf(buf, (size_t)n, "%d", p->params.oscs);
    if (!strcmp(key, "feedback"))
        return snprintf(buf, (size_t)n, "%g", (double)p->params.feedback);
    if (!strcmp(key, "spread"))
        return snprintf(buf, (size_t)n, "%g", (double)p->params.spread);
    if (!strcmp(key, "wander"))
        return snprintf(buf, (size_t)n, "%g", (double)p->params.wander);
    if (!strcmp(key, "attack"))
        return snprintf(buf, (size_t)n, "%g", (double)p->params.attack);
    if (!strcmp(key, "decay"))
        return snprintf(buf, (size_t)n, "%g", (double)p->params.decay);
    if (!strcmp(key, "sustain"))
        return snprintf(buf, (size_t)n, "%g", (double)p->params.sustain);
    if (!strcmp(key, "release"))
        return snprintf(buf, (size_t)n, "%g", (double)p->params.release);
    if (!strcmp(key, "level"))
        return snprintf(buf, (size_t)n, "%g", (double)p->params.level);
    if (!strcmp(key, "bend"))
        return snprintf(buf, (size_t)n, "%d", (int)(p->params.bend_range + 0.5f));
    /* readable value text for the MPC screen */
    if (!strcmp(key, "attack_display"))
        return time_text(p->params.attack, buf, n);
    if (!strcmp(key, "decay_display"))
        return time_text(p->params.decay, buf, n);
    if (!strcmp(key, "release_display"))
        return time_text(p->params.release, buf, n);
    if (!strcmp(key, "feedback_display"))
        return snprintf(buf, (size_t)n, "%d", (int)(p->params.feedback * 100.0f + 0.5f));
    if (!strcmp(key, "spread_display"))
        return snprintf(buf, (size_t)n, "%d", (int)(p->params.spread * 100.0f + 0.5f));
    if (!strcmp(key, "wander_display"))
        return snprintf(buf, (size_t)n, "%.1f", (double)(p->params.wander * 100.0f));
    return 0;
}

static int eng_get_param(void *inst, const char *key, char *buf, int buf_len)
{
    return get_param(inst, key, buf, buf_len);
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
        cluster_render(&p->core, l, r, n);
        for (int i = 0; i < n; i++) {
            out_lr[2 * (done + i)] = to_i16(l[i]);
            out_lr[2 * (done + i) + 1] = to_i16(r[i]);
        }
        done += n;
    }
}

static const mpc_engine_t ENGINE_BASE = {
    eng_create, eng_destroy, eng_midi, eng_set_param, eng_get_param, eng_render, NULL, NULL,
};

const mpc_engine_t *mpc_engine(void)
{
    return op1_act_wrap(&ENGINE_BASE, "oscs");
}
