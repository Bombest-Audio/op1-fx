/* DBox as an MPC OS instrument: glue between the shared DSP core and the mpc-vst-plugins wrapper.
 * Parameters arrive as key/value strings, MIDI as raw bytes, audio leaves as interleaved int16 stereo at 44.1 kHz.
 * With SAMPLE_ACCURATE the wrapper calls render() with any 1..128 frames. The root note, pitch bend range and
 * the other engine extras stay at their defaults on the MPC. */
#include "engine.h"
#include "dbox_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The MPC knobs are all 0..1 (PITCH -1..1, MODE 0..7). Times are exponential:
 *   attack 0.5 ms to 5 s     env decay and release 5 ms to 10 s */
#define ATTACK_MIN 0.0005f
#define ATTACK_MAX 5.0f
#define TIME_MIN 0.005f
#define TIME_MAX 10.0f

typedef struct {
    float pitch, wave, decay, mix2, wave2, decay2, filter, attack, env_decay, sustain, release, level;
    int mode;
} knobs_t;

typedef struct {
    dbox_t core;
    knobs_t k;
} inst_t;

static float exp_time(float norm, float lo, float hi)
{
    norm = norm < 0.0f ? 0.0f : (norm > 1.0f ? 1.0f : norm);
    return lo * powf(hi / lo, norm);
}

static float exp_time_inverse(float seconds, float lo, float hi)
{
    return logf(seconds / lo) / logf(hi / lo);
}

static void push_params(inst_t *p)
{
    dbox_params q;
    dbox_default_params(&q);
    q.pitch = p->k.pitch;
    q.wave = p->k.wave;
    q.decay = p->k.decay;
    q.mode = p->k.mode;
    q.mix2 = p->k.mix2;
    q.wave2 = p->k.wave2;
    q.decay2 = p->k.decay2;
    q.filter = p->k.filter;
    q.attack = exp_time(p->k.attack, ATTACK_MIN, ATTACK_MAX);
    q.env_decay = exp_time(p->k.env_decay, TIME_MIN, TIME_MAX);
    q.sustain = p->k.sustain;
    q.release = exp_time(p->k.release, TIME_MIN, TIME_MAX);
    q.level = p->k.level;
    dbox_set_params(&p->core, &q);
}

static void *eng_create(const char *data_dir)
{
    (void)data_dir;
    inst_t *p = (inst_t *)calloc(1, sizeof *p);
    if (!p)
        return NULL;
    dbox_init(&p->core, 44100.0f);
    p->k.pitch = DBOX_DEFAULT_PITCH;
    p->k.wave = DBOX_DEFAULT_WAVE;
    p->k.decay = DBOX_DEFAULT_DECAY;
    p->k.mode = DBOX_DEFAULT_MODE;
    p->k.mix2 = DBOX_DEFAULT_MIX2;
    p->k.wave2 = DBOX_DEFAULT_WAVE2;
    p->k.decay2 = DBOX_DEFAULT_DECAY2;
    p->k.filter = DBOX_DEFAULT_FILTER;
    p->k.attack = exp_time_inverse(DBOX_DEFAULT_ATTACK, ATTACK_MIN, ATTACK_MAX);
    p->k.env_decay = exp_time_inverse(DBOX_DEFAULT_ENV_DECAY, TIME_MIN, TIME_MAX);
    p->k.sustain = DBOX_DEFAULT_SUSTAIN;
    p->k.release = exp_time_inverse(DBOX_DEFAULT_RELEASE, TIME_MIN, TIME_MAX);
    p->k.level = DBOX_DEFAULT_LEVEL;
    push_params(p);
    dbox_reset(&p->core);
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
        if (len > 2 && d2 > 0)
            dbox_note_on(&p->core, d1, d2);
        else
            dbox_note_off(&p->core, d1);
        break;
    case 0x80:
        dbox_note_off(&p->core, d1);
        break;
    case 0xE0:
        dbox_pitch_bend(&p->core, (float)(((d2 << 7) | d1) - 8192) / 8192.0f);
        break;
    case 0xB0:
        if (d1 == 1)
            dbox_mod_wheel(&p->core, (float)d2 / 127.0f);
        else if (d1 == 64)
            dbox_sustain_pedal(&p->core, d2 >= 64);
        else if (d1 == 120 || d1 == 123)
            dbox_all_off(&p->core);
        break;
    default:
        break;
    }
}

static void eng_set_param(void *inst, const char *key, const char *val)
{
    inst_t *p = (inst_t *)inst;
    const float v = (float)atof(val);
    float *dst = NULL;
    if (!strcmp(key, "mode")) {
        p->k.mode = (int)(v + 0.5f);
        push_params(p);
        return;
    }
    if (!strcmp(key, "pitch")) dst = &p->k.pitch;
    else if (!strcmp(key, "wave")) dst = &p->k.wave;
    else if (!strcmp(key, "decay")) dst = &p->k.decay;
    else if (!strcmp(key, "mix2")) dst = &p->k.mix2;
    else if (!strcmp(key, "wave2")) dst = &p->k.wave2;
    else if (!strcmp(key, "decay2")) dst = &p->k.decay2;
    else if (!strcmp(key, "filter")) dst = &p->k.filter;
    else if (!strcmp(key, "attack")) dst = &p->k.attack;
    else if (!strcmp(key, "env_decay")) dst = &p->k.env_decay;
    else if (!strcmp(key, "sustain")) dst = &p->k.sustain;
    else if (!strcmp(key, "release")) dst = &p->k.release;
    else if (!strcmp(key, "level")) dst = &p->k.level;
    if (!dst)
        return;
    *dst = v;
    push_params(p);
}

static int time_text(char *buf, int buf_len, float seconds)
{
    return seconds < 1.0f ? snprintf(buf, (size_t)buf_len, "%d ms", (int)(seconds * 1000.0f + 0.5f))
                          : snprintf(buf, (size_t)buf_len, "%.2f s", (double)seconds);
}

static int decay_text(char *buf, int buf_len, float norm)
{
    const float a = dbox_attack_seconds(norm);
    return a > 0.0f ? snprintf(buf, (size_t)buf_len, "atk %d ms", (int)(a * 1000.0f + 0.5f))
                    : snprintf(buf, (size_t)buf_len, "%d ms", (int)(dbox_decay_seconds(norm) * 1000.0f + 0.5f));
}

static int eng_get_param(void *inst, const char *key, char *buf, int buf_len)
{
    inst_t *p = (inst_t *)inst;
    const struct { const char *name; float v; } plain[] = {
        {"pitch", p->k.pitch}, {"wave", p->k.wave}, {"decay", p->k.decay}, {"mix2", p->k.mix2},
        {"wave2", p->k.wave2}, {"decay2", p->k.decay2}, {"filter", p->k.filter}, {"attack", p->k.attack},
        {"env_decay", p->k.env_decay}, {"sustain", p->k.sustain}, {"release", p->k.release}, {"level", p->k.level},
    };
    for (size_t i = 0; i < sizeof plain / sizeof plain[0]; i++)
        if (!strcmp(key, plain[i].name))
            return snprintf(buf, (size_t)buf_len, "%g", (double)plain[i].v);
    if (!strcmp(key, "mode"))
        return snprintf(buf, (size_t)buf_len, "%d", p->k.mode);

    /* readable value text for the MPC screen */
    if (!strcmp(key, "pitch_display"))
        return snprintf(buf, (size_t)buf_len, "%+.1f st", (double)dbox_pitch_semitones(p->k.pitch));
    if (!strcmp(key, "mode_display"))
        return snprintf(buf, (size_t)buf_len, "%s", dbox_mode_name(p->k.mode));
    if (!strcmp(key, "decay_display"))
        return decay_text(buf, buf_len, p->k.decay);
    if (!strcmp(key, "decay2_display"))
        return decay_text(buf, buf_len, p->k.decay2);
    if (!strcmp(key, "filter_display")) {
        const float hz = dbox_filter_hz(p->k.filter);
        return hz >= 1000.0f ? snprintf(buf, (size_t)buf_len, "%.1fk", (double)(hz / 1000.0f))
                             : snprintf(buf, (size_t)buf_len, "%d", (int)(hz + 0.5f));
    }
    if (!strcmp(key, "wave_display"))
        return snprintf(buf, (size_t)buf_len, "%d", (int)(p->k.wave * 100.0f + 0.5f));
    if (!strcmp(key, "wave2_display"))
        return snprintf(buf, (size_t)buf_len, "%d", (int)(p->k.wave2 * 100.0f + 0.5f));
    if (!strcmp(key, "mix2_display"))
        return snprintf(buf, (size_t)buf_len, "%d", (int)(p->k.mix2 * 100.0f + 0.5f));
    if (!strcmp(key, "sustain_display"))
        return snprintf(buf, (size_t)buf_len, "%d", (int)(p->k.sustain * 100.0f + 0.5f));
    if (!strcmp(key, "level_display"))
        return snprintf(buf, (size_t)buf_len, "%d", (int)(p->k.level * 100.0f + 0.5f));
    if (!strcmp(key, "attack_display"))
        return time_text(buf, buf_len, exp_time(p->k.attack, ATTACK_MIN, ATTACK_MAX));
    if (!strcmp(key, "env_decay_display"))
        return time_text(buf, buf_len, exp_time(p->k.env_decay, TIME_MIN, TIME_MAX));
    if (!strcmp(key, "release_display"))
        return time_text(buf, buf_len, exp_time(p->k.release, TIME_MIN, TIME_MAX));
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
        dbox_render(&p->core, l, r, n);
        for (int i = 0; i < n; i++) {
            out_lr[2 * (done + i)] = to_i16(l[i]);
            out_lr[2 * (done + i) + 1] = to_i16(r[i]);
        }
        done += n;
    }
}

/* Instrument: no process(). */
static const mpc_engine_t ENGINE = {
    eng_create, eng_destroy, eng_midi, eng_set_param, eng_get_param, eng_render, NULL, NULL,
};

const mpc_engine_t *mpc_engine(void)
{
    return &ENGINE;
}
