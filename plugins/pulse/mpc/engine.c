/* Pulse as an MPC OS instrument: glue between the shared DSP core and the mpc-vst-plugins wrapper.
 * The wrapper hands MIDI over as raw bytes, parameters as key/value strings, and takes audio as interleaved int16 stereo
 * in blocks of 1 to 128 frames (SAMPLE_ACCURATE) at 44.1 kHz. */
#include "engine.h"
#include "op1fx_act.h"
#include "pulse_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    pulse_t core;
    pulse_params params;
} inst_t;

static void *eng_create(const char *data_dir)
{
    (void)data_dir;
    inst_t *p = (inst_t *)calloc(1, sizeof *p);
    if (!p)
        return NULL;
    pulse_init(&p->core, 44100.0f);
    p->params.shape = PULSE_DEFAULT_SHAPE;
    p->params.sweep = PULSE_DEFAULT_SWEEP;
    p->params.rate = PULSE_DEFAULT_RATE;
    p->params.detune = PULSE_DEFAULT_DETUNE;
    p->params.attack = PULSE_DEFAULT_ATTACK;
    p->params.decay = PULSE_DEFAULT_DECAY;
    p->params.sustain = PULSE_DEFAULT_SUSTAIN;
    p->params.release = PULSE_DEFAULT_RELEASE;
    p->params.level = PULSE_DEFAULT_LEVEL;
    p->params.bend_range = PULSE_DEFAULT_BEND_RANGE;
    pulse_set_params(&p->core, &p->params);
    pulse_reset(&p->core);
    return p;
}

static void eng_destroy(void *inst)
{
    free(inst);
}

static void eng_midi(void *inst, const uint8_t *msg, int len)
{
    inst_t *p = (inst_t *)inst;
    if (len < 2)
        return;
    const int st = msg[0] & 0xf0;
    const int d1 = msg[1] & 0x7f;
    const int d2 = len > 2 ? (msg[2] & 0x7f) : 0;
    switch (st) {
    case 0x90:
        pulse_note_on(&p->core, d1, d2); /* velocity 0 is a note off inside the core */
        break;
    case 0x80:
        pulse_note_off(&p->core, d1);
        break;
    case 0xe0:
        pulse_pitch_bend(&p->core, (float)(((d2 << 7) | d1) - 8192) / 8192.0f);
        break;
    case 0xb0:
        if (d1 == 1)
            pulse_mod_wheel(&p->core, (float)d2 / 127.0f);
        else if (d1 == 64)
            pulse_sustain_pedal(&p->core, d2 >= 64);
        else if (d1 == 120)
            pulse_all_sound_off(&p->core);
        else if (d1 == 123)
            pulse_all_off(&p->core);
        break;
    default:
        break;
    }
}

static void eng_set_param(void *inst, const char *key, const char *val)
{
    inst_t *p = (inst_t *)inst;
    const float v = (float)atof(val);
    pulse_params *q = &p->params;
    if (!strcmp(key, "shape")) q->shape = v;
    else if (!strcmp(key, "sweep")) q->sweep = v;
    else if (!strcmp(key, "rate")) q->rate = v;
    else if (!strcmp(key, "detune")) q->detune = v;
    else if (!strcmp(key, "attack")) q->attack = v;
    else if (!strcmp(key, "decay")) q->decay = v;
    else if (!strcmp(key, "sustain")) q->sustain = v;
    else if (!strcmp(key, "release")) q->release = v;
    else if (!strcmp(key, "level")) q->level = v;
    else if (!strcmp(key, "bend")) q->bend_range = (float)(int)(v + 0.5f);
    else
        return;
    pulse_set_params(&p->core, q);
}

static int fmt_time(char *buf, int n, float s)
{
    if (s < 0.01f)
        return snprintf(buf, (size_t)n, "%.1fms", (double)(s * 1000.0f));
    if (s < 1.0f)
        return snprintf(buf, (size_t)n, "%dms", (int)(s * 1000.0f + 0.5f));
    return snprintf(buf, (size_t)n, "%.1fs", (double)s);
}

static int eng_get_param(void *inst, const char *key, char *buf, int buf_len)
{
    inst_t *p = (inst_t *)inst;
    const pulse_params *q = &p->params;
    size_t n = (size_t)buf_len;
    if (!strcmp(key, "shape")) return snprintf(buf, n, "%g", (double)q->shape);
    if (!strcmp(key, "sweep")) return snprintf(buf, n, "%g", (double)q->sweep);
    if (!strcmp(key, "rate")) return snprintf(buf, n, "%g", (double)q->rate);
    if (!strcmp(key, "detune")) return snprintf(buf, n, "%g", (double)q->detune);
    if (!strcmp(key, "attack")) return snprintf(buf, n, "%g", (double)q->attack);
    if (!strcmp(key, "decay")) return snprintf(buf, n, "%g", (double)q->decay);
    if (!strcmp(key, "sustain")) return snprintf(buf, n, "%g", (double)q->sustain);
    if (!strcmp(key, "release")) return snprintf(buf, n, "%g", (double)q->release);
    if (!strcmp(key, "level")) return snprintf(buf, n, "%g", (double)q->level);
    if (!strcmp(key, "bend")) return snprintf(buf, n, "%d", (int)(q->bend_range + 0.5f));
    /* readable value text for the MPC screen */
    if (!strcmp(key, "shape_display")) return snprintf(buf, n, "%d%%", (int)(pulse_duty_pct(q->shape) + 0.5f));
    if (!strcmp(key, "sweep_display")) return snprintf(buf, n, "%d", (int)(q->sweep * 100.0f + 0.5f));
    if (!strcmp(key, "rate_display")) return snprintf(buf, n, "%.1fHz", (double)pulse_rate_hz(q->rate));
    if (!strcmp(key, "detune_display")) {
        const float c = pulse_detune_cents(q->detune);
        return snprintf(buf, n, "%d", (int)(c < 0.0f ? c - 0.5f : c + 0.5f));
    }
    if (!strcmp(key, "attack_display")) return fmt_time(buf, buf_len, pulse_attack_s(q->attack));
    if (!strcmp(key, "decay_display")) return fmt_time(buf, buf_len, pulse_decay_s(q->decay));
    if (!strcmp(key, "release_display")) return fmt_time(buf, buf_len, pulse_release_s(q->release));
    if (!strcmp(key, "sustain_display")) return snprintf(buf, n, "%d", (int)(q->sustain * 100.0f + 0.5f));
    if (!strcmp(key, "level_display")) return snprintf(buf, n, "%d", (int)(q->level * 100.0f + 0.5f));
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
        pulse_render(&p->core, l, r, n);
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
    return op1_act_wrap(&ENGINE_BASE, "shape");
}
