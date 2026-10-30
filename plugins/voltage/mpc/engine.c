/* Voltage as an MPC OS instrument: glue between the shared DSP core and the mpc-vst-plugins wrapper.
 * The wrapper hands parameters over as key/value strings, MIDI as raw bytes, and takes audio as interleaved int16
 * stereo in blocks of 1 to 128 frames at 44.1 kHz (SAMPLE_ACCURATE is set in vst.json). */
#include "engine.h"
#include "voltage_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Envelope times are a linear 0..1 control mapped to seconds on an exponential scale, the same as the plugin. */
#define ATT_LO 0.0005f
#define ATT_HI 5.0f
#define LONG_LO 0.005f
#define LONG_HI 10.0f

typedef struct {
    opvl_t core;
    float xor_amt, feedback, filter, detune;
    float attack, env_decay, sustain, release; /* normalized times */
    float level_db;
    float bend_semis;
    int bend_range;
} inst_t;

static float to_seconds(float x, float lo, float hi)
{
    return lo * powf(hi / lo, x);
}

static float to_norm(float s, float lo, float hi)
{
    return logf(s / lo) / logf(hi / lo);
}

static void push_params(inst_t *p)
{
    opvl_params q;
    q.xor_amt = p->xor_amt;
    q.feedback = p->feedback;
    q.filter = p->filter;
    q.detune = p->detune;
    q.env_attack = to_seconds(p->attack, ATT_LO, ATT_HI);
    q.env_decay = to_seconds(p->env_decay, LONG_LO, LONG_HI);
    q.env_sustain = p->sustain;
    q.env_release = to_seconds(p->release, LONG_LO, LONG_HI);
    q.level = p->level_db <= -59.9f ? 0.0f : powf(10.0f, p->level_db / 20.0f);
    opvl_set_params(&p->core, &q);
}

static void *eng_create(const char *data_dir)
{
    (void)data_dir;
    inst_t *p = (inst_t *)calloc(1, sizeof *p);
    if (!p)
        return NULL;
    opvl_init(&p->core, 44100.0f);
    p->xor_amt = OPVL_DEFAULT_XOR;
    p->feedback = OPVL_DEFAULT_FEEDBACK;
    p->filter = OPVL_DEFAULT_FILTER;
    p->detune = OPVL_DEFAULT_DETUNE;
    p->attack = to_norm(OPVL_DEFAULT_ATTACK, ATT_LO, ATT_HI);
    p->env_decay = to_norm(OPVL_DEFAULT_ENV_DECAY, LONG_LO, LONG_HI);
    p->sustain = OPVL_DEFAULT_SUSTAIN;
    p->release = to_norm(OPVL_DEFAULT_RELEASE, LONG_LO, LONG_HI);
    p->level_db = 0.0f;
    p->bend_range = 2;
    push_params(p);
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
    const int st = msg[0] & 0xF0;
    const int d1 = len > 1 ? msg[1] & 0x7F : 0;
    const int d2 = len > 2 ? msg[2] & 0x7F : 0;
    if (st == 0x90 && d2 > 0 && len > 2)
        opvl_note_on(&p->core, d1, d2);
    else if (st == 0x80 || st == 0x90)
        opvl_note_off(&p->core, d1);
    else if (st == 0xE0 && len > 2) {
        const float x = (float)(((d2 << 7) | d1) - 8192) / 8192.0f;
        p->bend_semis = x * (float)p->bend_range;
        opvl_pitch_bend(&p->core, p->bend_semis);
    } else if (st == 0xB0 && len > 2) {
        if (d1 == 1)
            opvl_mod_wheel(&p->core, (float)d2 / 127.0f);
        else if (d1 == 64)
            opvl_sustain(&p->core, d2 >= 64);
        else if (d1 == 120)
            opvl_reset(&p->core);
        else if (d1 == 123)
            opvl_all_off(&p->core);
    }
}

static void eng_set_param(void *inst, const char *key, const char *val)
{
    inst_t *p = (inst_t *)inst;
    const float v = (float)atof(val);
    if (!strcmp(key, "xor"))
        p->xor_amt = v;
    else if (!strcmp(key, "feedback"))
        p->feedback = v;
    else if (!strcmp(key, "filter"))
        p->filter = v;
    else if (!strcmp(key, "detune"))
        p->detune = v;
    else if (!strcmp(key, "attack"))
        p->attack = v;
    else if (!strcmp(key, "env_decay"))
        p->env_decay = v;
    else if (!strcmp(key, "sustain"))
        p->sustain = v;
    else if (!strcmp(key, "release"))
        p->release = v;
    else if (!strcmp(key, "level"))
        p->level_db = v;
    else
        return;
    push_params(p);
}

static int fmt_seconds(char *buf, int n, float s)
{
    return s < 0.995f ? snprintf(buf, (size_t)n, "%dms", (int)(s * 1000.0f + 0.5f))
                      : snprintf(buf, (size_t)n, s < 9.95f ? "%.1fs" : "%.0fs", (double)s);
}

static int get_num(char *buf, int n, float v)
{
    return snprintf(buf, (size_t)n, "%g", (double)v);
}

static int eng_get_param(void *inst, const char *key, char *buf, int n)
{
    inst_t *p = (inst_t *)inst;
    if (!strcmp(key, "xor"))
        return get_num(buf, n, p->xor_amt);
    if (!strcmp(key, "feedback"))
        return get_num(buf, n, p->feedback);
    if (!strcmp(key, "filter"))
        return get_num(buf, n, p->filter);
    if (!strcmp(key, "detune"))
        return get_num(buf, n, p->detune);
    if (!strcmp(key, "attack"))
        return get_num(buf, n, p->attack);
    if (!strcmp(key, "env_decay"))
        return get_num(buf, n, p->env_decay);
    if (!strcmp(key, "sustain"))
        return get_num(buf, n, p->sustain);
    if (!strcmp(key, "release"))
        return get_num(buf, n, p->release);
    if (!strcmp(key, "level"))
        return get_num(buf, n, p->level_db);
    /* readable value text for the MPC screen */
    if (!strcmp(key, "xor_display"))
        return snprintf(buf, (size_t)n, "%d", (int)(p->xor_amt * 100.0f + 0.5f));
    if (!strcmp(key, "feedback_display"))
        return snprintf(buf, (size_t)n, "%d", (int)(p->feedback * 100.0f + 0.5f));
    if (!strcmp(key, "filter_display")) {
        const float hz = opvl_filter_hz(p->filter);
        return hz >= 1000.0f ? snprintf(buf, (size_t)n, "%.1fk", (double)(hz / 1000.0f))
                             : snprintf(buf, (size_t)n, "%d", (int)(hz + 0.5f));
    }
    if (!strcmp(key, "detune_display"))
        return snprintf(buf, (size_t)n, "%.1f", (double)opvl_detune_percent(p->detune));
    if (!strcmp(key, "attack_display"))
        return fmt_seconds(buf, n, to_seconds(p->attack, ATT_LO, ATT_HI));
    if (!strcmp(key, "env_decay_display"))
        return fmt_seconds(buf, n, to_seconds(p->env_decay, LONG_LO, LONG_HI));
    if (!strcmp(key, "sustain_display"))
        return snprintf(buf, (size_t)n, "%d", (int)(p->sustain * 100.0f + 0.5f));
    if (!strcmp(key, "release_display"))
        return fmt_seconds(buf, n, to_seconds(p->release, LONG_LO, LONG_HI));
    if (!strcmp(key, "level_display"))
        return p->level_db <= -59.9f ? snprintf(buf, (size_t)n, "off")
                                     : snprintf(buf, (size_t)n, "%ddB", (int)lroundf(p->level_db));
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
        opvl_render(&p->core, l, r, n);
        for (int i = 0; i < n; i++) {
            out_lr[2 * (done + i)] = to_i16(l[i]);
            out_lr[2 * (done + i) + 1] = to_i16(r[i]);
        }
        done += n;
    }
}

static const mpc_engine_t ENGINE = {
    eng_create, eng_destroy, eng_midi, eng_set_param, eng_get_param, eng_render, NULL, NULL,
};

const mpc_engine_t *mpc_engine(void)
{
    return &ENGINE;
}
