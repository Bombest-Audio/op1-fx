/* Punch glove activity readout. After every render or process call it compares output peak
 * with a slow level baseline, then publishes one frame number as the parameter "act".
 * The skin shows it with a filmstrip `meter`: 8 steps of lead knob times 8 transient steps, 64 frames
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
    float env;      /* decaying transient strength, 0..1 */
    float baseline; /* slow output level follower, 0..1 */
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

/* Preset and project state. MPC saves a plugin's sound as the VST2 chunk, and the wrapper fills that chunk from
 * get_param("state") and hands it back through set_param("state"). Most engines don't answer "state", so a saved .xpl
 * preset or project used to hold an empty chunk and loading it changed nothing (issue #3). This saves every host
 * parameter in the engine's own value text, one per line:
 *     op1\n<key>=<value>\n...\nstate=<the engine's own state>
 * The last line is only there when the engine answers "state" itself (drumsampler's sample slots), and it runs to the
 * end of the chunk. Loading replays the parameters in params.h order, a key the chunk lacks (one added after the preset
 * was saved) goes back to its default, then the engine's own state is applied last. A chunk without the "op1\n"
 * header (an older drumsampler preset) goes to the engine unchanged.
 * The keys come from the port's generated params.h, which is on the include path only in a port build; an offline
 * engine test without it gets the plain passthrough. */
#if defined(__has_include)
#if __has_include("params.h")
#define OP1_STATE 1
#endif
#endif

#ifdef OP1_STATE
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "params.h"
#pragma GCC diagnostic pop

#define OP1_STATE_HEAD "op1\n"
#define OP1_STATE_INNER "state="

/* Saved: a plain knob, switch or list. Not the engine-driven readout, a momentary button or a step trigger (both
 * act on press), a popup's open flag or a text readout. */
static int op1_state_saved(int i)
{
    const param_t *p = &PARAMS[i];
    return strcmp(p->key, "act") != 0 && !p->momentary && p->step_target < 0 && p->popup_of < 0 && !p->string_display;
}

/* A parameter's default as the engine value text, the same conversion the wrapper's norm_to_str() makes. */
static void op1_state_default(int i, char *buf, int len)
{
    const param_t *p = &PARAMS[i];
    if (p->nopts)
        snprintf(buf, (size_t)len, "%d", (int)lroundf(p->def * (float)(p->nopts - 1)));
    else if (p->int_display)
        snprintf(buf, (size_t)len, "%ld", lroundf(p->min + (p->max - p->min) * p->def));
    else
        snprintf(buf, (size_t)len, "%g", (double)(p->min + (p->max - p->min) * p->def));
}

/* Returns the length written, or 0 when it doesn't fit (the wrapper then saves no chunk rather than a cut-off one). */
static int op1_state_write(void *inner, char *buf, int buf_len)
{
    int n = snprintf(buf, (size_t)buf_len, "%s", OP1_STATE_HEAD);
    char val[64];
    for (int i = 0; i < NPARAMS && n < buf_len; i++) {
        if (!op1_state_saved(i) || op1_act_base->get_param(inner, PARAMS[i].key, val, (int)sizeof val) <= 0)
            continue;
        n += snprintf(buf + n, (size_t)(buf_len - n), "%s=%s\n", PARAMS[i].key, val);
    }
    if (n >= buf_len)
        return 0;
    const int head = n + (int)strlen(OP1_STATE_INNER);
    if (head < buf_len) {
        const int m = op1_act_base->get_param(inner, "state", buf + head, buf_len - head);
        if (m > 0 && head + m < buf_len) {
            memcpy(buf + n, OP1_STATE_INNER, strlen(OP1_STATE_INNER));
            return head + m;
        }
        if (m > 0)
            return 0;
    }
    buf[n] = 0;
    return n;
}

static void op1_state_read(void *inner, const char *text)
{
    char *copy = strdup(text + strlen(OP1_STATE_HEAD));
    if (!copy)
        return;
    const char *vals[NPARAMS > 0 ? NPARAMS : 1] = {0};
    const char *own = NULL;
    char *line = copy;
    while (line && *line) {
        if (!strncmp(line, OP1_STATE_INNER, strlen(OP1_STATE_INNER))) {
            own = line + strlen(OP1_STATE_INNER);   /* runs to the end: the engine's state may hold newlines */
            break;
        }
        char *next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = 0;
            for (int i = 0; i < NPARAMS; i++)
                if (op1_state_saved(i) && !strcmp(PARAMS[i].key, line)) {
                    vals[i] = eq + 1;
                    break;
                }
        }
        line = next;
    }
    char def[64];
    for (int i = 0; i < NPARAMS; i++) {
        if (!op1_state_saved(i))
            continue;
        if (!vals[i]) {
            op1_state_default(i, def, (int)sizeof def);
            vals[i] = def;
        }
        op1_act_base->set_param(inner, PARAMS[i].key, vals[i]);
    }
    if (own && *own)
        op1_act_base->set_param(inner, "state", own);
    free(copy);
}
#endif

static void op1_act_set_param(void *inst, const char *key, const char *val)
{
    if (!strcmp(key, "act"))
        return;
#ifdef OP1_STATE
    if (!strcmp(key, "state") && !strncmp(val, OP1_STATE_HEAD, strlen(OP1_STATE_HEAD))) {
        op1_state_read(((op1_act_t *)inst)->inner, val);
        return;
    }
#endif
    op1_act_base->set_param(((op1_act_t *)inst)->inner, key, val);
}

static int op1_act_get_param(void *inst, const char *key, char *buf, int buf_len)
{
    op1_act_t *a = (op1_act_t *)inst;
    if (!strcmp(key, "act"))
        return snprintf(buf, (size_t)buf_len, "%g", (double)a->frame / (double)(OP1_ACT_FRAMES - 1));
    if (!strcmp(key, "display_rev"))
        return snprintf(buf, (size_t)buf_len, "%d", a->rev);
#ifdef OP1_STATE
    if (!strcmp(key, "state"))
        return op1_state_write(a->inner, buf, buf_len);
#endif
    return op1_act_base->get_param(a->inner, key, buf, buf_len);
}

/* A rise above the 80 ms baseline drives the glove. The 180 ms fall keeps it visible
 * across the wrapper's roughly 100 ms display poll. Sustained level returns to guard. */
static void op1_act_feed(op1_act_t *a, const int16_t *out, int frames)
{
    int peak = 0;
    for (int i = 0; i < frames * 2; i++) {
        const int v = out[i] < 0 ? -out[i] : out[i];
        if (v > peak)
            peak = v;
    }
    const float level = (float)peak * (1.0f / 32768.0f);
    const float onset = fmaxf(0.0f, level - a->baseline);
    const float baseline_mix = 1.0f - powf(0.5f, (float)frames / (44100.0f * 0.08f));
    a->baseline += (level - a->baseline) * baseline_mix;
    const float fall = powf(0.5f, (float)frames / (44100.0f * 0.18f));
    a->env = onset > a->env * fall ? onset : a->env * fall;

    char buf[32];
    float knob = 0.0f;
    if (op1_act_base->get_param(a->inner, op1_act_key, buf, (int)sizeof buf) > 0)
        knob = (float)atof(buf);
    knob = knob < 0.0f ? 0.0f : (knob > 1.0f ? 1.0f : knob);

    int ks = (int)(knob * (float)(OP1_ACT_KNOB_STEPS - 1) + 0.5f);
    const float visible = a->env < 0.01f ? 0.0f : fminf(1.0f, 4.0f * a->env);
    int ls = (int)(sqrtf(visible) * (float)(OP1_ACT_LEVEL_STEPS - 1) + 0.5f);
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
