/* Live activity readout for the MPC skins. Wraps an engine table: after every render or process call it measures
 * the block peak and reads the plugin's lead knob, then publishes one frame number as the parameter "act".
 * The skin shows it with a filmstrip `meter`: 8 steps of lead knob times 8 steps of signal level, 64 frames
 * (frame = knob step * 8 + level step). Only the engine writes "act", a host set_param is ignored.
 * It needs "HAS_DISPLAY_REV": 1 in vst.json, the wrapper polls display_rev about every 100 ms. */
#pragma once
#include "engine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OP1_ACT_KNOB_STEPS 8
#define OP1_ACT_LEVEL_STEPS 8
#define OP1_ACT_FRAMES (OP1_ACT_KNOB_STEPS * OP1_ACT_LEVEL_STEPS)

typedef struct {
    void *inner;
    float env;      /* peak follower, 0..1 */
    int frame;      /* 0..OP1_ACT_FRAMES-1 */
    int rev;
} op1_act_t;

static const mpc_engine_t *op1_act_base;
static const char *op1_act_key;

static void *op1_act_create(const char *data_dir)
{
    op1_act_t *a = (op1_act_t *)calloc(1, sizeof *a);
    if (!a)
        return NULL;
    a->inner = op1_act_base->create(data_dir);
    if (!a->inner) {
        free(a);
        return NULL;
    }
    return a;
}

static void op1_act_destroy(void *inst)
{
    op1_act_t *a = (op1_act_t *)inst;
    if (!a)
        return;
    op1_act_base->destroy(a->inner);
    free(a);
}

static void op1_act_midi(void *inst, const uint8_t *msg, int len)
{
    op1_act_base->midi(((op1_act_t *)inst)->inner, msg, len);
}

static void op1_act_set_param(void *inst, const char *key, const char *val)
{
    if (!strcmp(key, "act"))
        return;
    op1_act_base->set_param(((op1_act_t *)inst)->inner, key, val);
}

static int op1_act_get_param(void *inst, const char *key, char *buf, int buf_len)
{
    op1_act_t *a = (op1_act_t *)inst;
    if (!strcmp(key, "act"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)a->frame / (double)(OP1_ACT_FRAMES - 1));
    if (!strcmp(key, "display_rev"))
        return snprintf(buf, (size_t)buf_len, "%d", a->rev);
    return op1_act_base->get_param(a->inner, key, buf, buf_len);
}

/* Peak of the block, a fast attack and a fall of about 90 ms, a square root so quiet playing still shows. */
static void op1_act_feed(op1_act_t *a, const int16_t *out, int frames)
{
    int peak = 0;
    for (int i = 0; i < frames * 2; i++) {
        const int v = out[i] < 0 ? -out[i] : out[i];
        if (v > peak)
            peak = v;
    }
    const float level = (float)peak * (1.0f / 32768.0f);
    const float fall = powf(0.5f, (float)frames / (44100.0f * 0.09f));
    a->env = level > a->env ? level : a->env * fall;

    char buf[32];
    float knob = 0.0f;
    if (op1_act_base->get_param(a->inner, op1_act_key, buf, (int)sizeof buf) > 0)
        knob = (float)atof(buf);
    knob = knob < 0.0f ? 0.0f : (knob > 1.0f ? 1.0f : knob);

    int ks = (int)(knob * (float)(OP1_ACT_KNOB_STEPS - 1) + 0.5f);
    int ls = (int)(sqrtf(a->env) * (float)(OP1_ACT_LEVEL_STEPS - 1) + 0.5f);
    ls = ls > OP1_ACT_LEVEL_STEPS - 1 ? OP1_ACT_LEVEL_STEPS - 1 : ls;
    const int frame = ks * OP1_ACT_LEVEL_STEPS + ls;
    if (frame != a->frame) {
        a->frame = frame;
        a->rev++;
    }
}

static void op1_act_render(void *inst, int16_t *out_lr, int frames)
{
    op1_act_t *a = (op1_act_t *)inst;
    op1_act_base->render(a->inner, out_lr, frames);
    op1_act_feed(a, out_lr, frames);
}

static void op1_act_process(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames)
{
    op1_act_t *a = (op1_act_t *)inst;
    op1_act_base->process(a->inner, in_lr, out_lr, frames);
    op1_act_feed(a, out_lr, frames);
}

/* Returns an engine table that forwards to `base` and adds the "act" and "display_rev" keys.
 * `lead_key` is the parameter key whose position moves the skin's ring. */
static const mpc_engine_t *op1_act_wrap(const mpc_engine_t *base, const char *lead_key)
{
    static mpc_engine_t wrapped;
    op1_act_base = base;
    op1_act_key = lead_key;
    wrapped = *base;
    wrapped.create = op1_act_create;
    wrapped.destroy = op1_act_destroy;
    wrapped.midi = op1_act_midi;
    wrapped.set_param = op1_act_set_param;
    wrapped.get_param = op1_act_get_param;
    /* an effect has a silent render() stub and does its work in process(), so only an instrument is fed from render() */
    if (base->render && !base->process)
        wrapped.render = op1_act_render;
    if (base->process)
        wrapped.process = op1_act_process;
    return &wrapped;
}
