/* DSynth as an MPC OS instrument: glue between the shared DSP core and the mpc-vst-plugins wrapper.
 * Parameters arrive as key/value strings. MIDI arrives as raw messages. Audio leaves as interleaved
 * int16 stereo at 44.1 kHz in blocks of 1 to 128 frames (SAMPLE_ACCURATE is set in vst.json). */
#include "engine.h"
#include "op1fx_act.h"
#include "dsynth_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    dsynth_t core;
    dsynth_params params;
    float attack_n, ampdecay_n, release_n; /* 0..1 as the MPC shows them, mapped to seconds below */
} inst_t;

/* The MPC has only 0..1 knobs, so the amp times use the same skew as the plugin: seconds = lo + (hi - lo) * n^(1/0.3). */
static float n_to_seconds(float n, float lo, float hi)
{
    n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    return lo + (hi - lo) * powf(n, 1.0f / 0.3f);
}

static void push_params(inst_t *p)
{
    p->params.amp_attack = n_to_seconds(p->attack_n, 0.0005f, 5.0f);
    p->params.amp_decay = n_to_seconds(p->ampdecay_n, 0.005f, 10.0f);
    p->params.amp_release = n_to_seconds(p->release_n, 0.005f, 10.0f);
    dsynth_set_params(&p->core, &p->params);
}

static void *eng_create(const char *data_dir)
{
    (void)data_dir;
    inst_t *p = (inst_t *)calloc(1, sizeof *p);
    if (!p)
        return NULL;
    dsynth_init(&p->core, 44100.0f);
    dsynth_default_params(&p->params);
    p->attack_n = 0.0631f;
    p->ampdecay_n = 0.5f;
    p->release_n = 0.3345f;
    push_params(p);
    dsynth_reset(&p->core);
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
    const int status = msg[0] & 0xF0;
    const int d1 = len > 1 ? msg[1] & 0x7F : 0;
    const int d2 = len > 2 ? msg[2] & 0x7F : 0;
    switch (status) {
    case 0x90:
        if (d2 > 0)
            dsynth_note_on(&p->core, d1, d2);
        else
            dsynth_note_off(&p->core, d1);
        break;
    case 0x80:
        dsynth_note_off(&p->core, d1);
        break;
    case 0xE0:
        dsynth_pitch_bend(&p->core, (float)(((d2 << 7) | d1) - 8192) / 8192.0f);
        break;
    case 0xB0:
        if (d1 == 1)
            dsynth_mod_wheel(&p->core, (float)d2 / 127.0f);
        else if (d1 == 64)
            dsynth_sustain_pedal(&p->core, d2 >= 64);
        else if (d1 == 120 || d1 == 123)
            dsynth_all_off(&p->core);
        break;
    default:
        break;
    }
}

static void eng_set_param(void *inst, const char *key, const char *val)
{
    inst_t *p = (inst_t *)inst;
    const float v = (float)atof(val);
    if (!strcmp(key, "mod"))
        p->params.mod = v;
    else if (!strcmp(key, "wave"))
        p->params.wave = v;
    else if (!strcmp(key, "decay"))
        p->params.decay = v;
    else if (!strcmp(key, "filter"))
        p->params.filter_mode = (int)(v + 0.5f);
    else if (!strcmp(key, "attack"))
        p->attack_n = v;
    else if (!strcmp(key, "ampdecay"))
        p->ampdecay_n = v;
    else if (!strcmp(key, "sustain"))
        p->params.amp_sustain = v;
    else if (!strcmp(key, "release"))
        p->release_n = v;
    else if (!strcmp(key, "level"))
        p->params.level = v;
    else
        return;
    push_params(p);
}

static int eng_get_param(void *inst, const char *key, char *buf, int buf_len)
{
    inst_t *p = (inst_t *)inst;
    const size_t n = (size_t)buf_len;
    if (!strcmp(key, "mod"))
        return snprintf(buf, n, "%g", (double)p->params.mod);
    if (!strcmp(key, "wave"))
        return snprintf(buf, n, "%g", (double)p->params.wave);
    if (!strcmp(key, "decay"))
        return snprintf(buf, n, "%g", (double)p->params.decay);
    if (!strcmp(key, "filter"))
        return snprintf(buf, n, "%d", p->params.filter_mode);
    if (!strcmp(key, "attack"))
        return snprintf(buf, n, "%g", (double)p->attack_n);
    if (!strcmp(key, "ampdecay"))
        return snprintf(buf, n, "%g", (double)p->ampdecay_n);
    if (!strcmp(key, "sustain"))
        return snprintf(buf, n, "%g", (double)p->params.amp_sustain);
    if (!strcmp(key, "release"))
        return snprintf(buf, n, "%g", (double)p->release_n);
    if (!strcmp(key, "level"))
        return snprintf(buf, n, "%g", (double)p->params.level);
    /* readable value text for the MPC screen */
    if (!strcmp(key, "decay_display")) {
        const float s = dsynth_decay_seconds(p->params.decay);
        return s < 1.0f ? snprintf(buf, n, "%d ms", (int)(s * 1000.0f + 0.5f)) : snprintf(buf, n, "%.2f s", (double)s);
    }
    if (!strcmp(key, "filter_display"))
        return snprintf(buf, n, "%s", dsynth_filter_name(p->params.filter_mode));
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
        dsynth_render(&p->core, l, r, n);
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
    return op1_act_wrap(&ENGINE_BASE, "mod");
}
