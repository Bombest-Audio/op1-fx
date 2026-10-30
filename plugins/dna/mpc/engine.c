/* DNA as an MPC OS instrument: glue between the shared DSP core and the mpc-vst-plugins wrapper.
 * The wrapper hands MIDI over as raw bytes, parameters as key/value strings, and takes audio as interleaved int16 stereo
 * in blocks of 1 to 128 frames (SAMPLE_ACCURATE) at 44.1 kHz. The three envelope time knobs travel as 0 to 1 and are
 * mapped to seconds with dna_time_seconds(), the same mapping the desktop plugin uses. */
#include "engine.h"
#include "op1fx_act.h"
#include "dna_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    dna_t core;
    dna_params params;
    float attack_n, decay_n, release_n; /* the time knobs as the host sent them, 0 to 1 */
} inst_t;

static void apply(inst_t *p)
{
    p->params.attack = dna_time_seconds(p->attack_n);
    p->params.decay = dna_time_seconds(p->decay_n);
    p->params.release = dna_time_seconds(p->release_n);
    dna_set_params(&p->core, &p->params);
}

static void *eng_create(const char *data_dir)
{
    (void)data_dir;
    inst_t *p = (inst_t *)calloc(1, sizeof *p);
    if (!p)
        return NULL;
    dna_init(&p->core, 44100.0f);
    dna_default_params(&p->params);
    p->attack_n = dna_time_norm(p->params.attack);
    p->decay_n = dna_time_norm(p->params.decay);
    p->release_n = dna_time_norm(p->params.release);
    apply(p);
    dna_reset(&p->core);
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
        dna_note_on(&p->core, d1, d2); /* velocity 0 is a note off inside the core */
        break;
    case 0x80:
        dna_note_off(&p->core, d1);
        break;
    case 0xe0:
        dna_pitch_bend(&p->core, (float)(((d2 << 7) | d1) - 8192) / 8192.0f);
        break;
    case 0xb0:
        if (d1 == 1)
            dna_mod_wheel(&p->core, (float)d2 / 127.0f);
        else if (d1 == 64)
            dna_sustain_pedal(&p->core, d2 >= 64);
        else if (d1 == 120 || d1 == 123)
            dna_all_off(&p->core);
        break;
    default:
        break;
    }
}

static void eng_set_param(void *inst, const char *key, const char *val)
{
    inst_t *p = (inst_t *)inst;
    const float v = (float)atof(val);
    dna_params *q = &p->params;
    if (!strcmp(key, "mutate")) q->mutate = v;
    else if (!strcmp(key, "bits")) q->bits = v;
    else if (!strcmp(key, "sub")) q->sub = v;
    else if (!strcmp(key, "grit")) q->grit = v;
    else if (!strcmp(key, "attack")) p->attack_n = v;
    else if (!strcmp(key, "decay")) p->decay_n = v;
    else if (!strcmp(key, "sustain")) q->sustain = v;
    else if (!strcmp(key, "release")) p->release_n = v;
    else if (!strcmp(key, "level")) q->level = v;
    else if (!strcmp(key, "bend")) q->bend_range = (float)(int)(v + 0.5f);
    else
        return;
    apply(p);
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
    const dna_params *q = &p->params;
    size_t n = (size_t)buf_len;
    if (!strcmp(key, "mutate")) return snprintf(buf, n, "%g", (double)q->mutate);
    if (!strcmp(key, "bits")) return snprintf(buf, n, "%g", (double)q->bits);
    if (!strcmp(key, "sub")) return snprintf(buf, n, "%g", (double)q->sub);
    if (!strcmp(key, "grit")) return snprintf(buf, n, "%g", (double)q->grit);
    if (!strcmp(key, "attack")) return snprintf(buf, n, "%g", (double)p->attack_n);
    if (!strcmp(key, "decay")) return snprintf(buf, n, "%g", (double)p->decay_n);
    if (!strcmp(key, "sustain")) return snprintf(buf, n, "%g", (double)q->sustain);
    if (!strcmp(key, "release")) return snprintf(buf, n, "%g", (double)p->release_n);
    if (!strcmp(key, "level")) return snprintf(buf, n, "%g", (double)q->level);
    if (!strcmp(key, "bend")) return snprintf(buf, n, "%d", (int)(q->bend_range + 0.5f));
    /* readable value text for the MPC screen */
    if (!strcmp(key, "mutate_display")) {
        const float c = q->mutate * 100.0f;
        return snprintf(buf, n, "%d", (int)(c < 0.0f ? c - 0.5f : c + 0.5f));
    }
    if (!strcmp(key, "bits_display")) return snprintf(buf, n, "%d", (int)(dna_bits_value(q->bits) + 0.5f));
    if (!strcmp(key, "sub_display")) return snprintf(buf, n, "%d", (int)(q->sub * 100.0f + 0.5f));
    if (!strcmp(key, "grit_display")) return snprintf(buf, n, "%d", (int)(q->grit * 100.0f + 0.5f));
    if (!strcmp(key, "attack_display")) return fmt_time(buf, buf_len, q->attack);
    if (!strcmp(key, "decay_display")) return fmt_time(buf, buf_len, q->decay);
    if (!strcmp(key, "release_display")) return fmt_time(buf, buf_len, q->release);
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
        dna_render(&p->core, l, r, n);
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
    return op1_act_wrap(&ENGINE_BASE, "mutate");
}
