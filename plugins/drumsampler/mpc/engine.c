/* DrumSampler as an MPC OS instrument: glue between the shared DSP core and the mpc-vst-plugins wrapper.
 *
 * Samples: at create() the engine reads every .wav in the data folder (the "samples" folder next to the .so), sorted by
 * name, into memory as mono float. If the folder is missing or holds no usable file it falls back to the generated kit
 * from drumsampler_kit.c, so the plugin always makes sound. All file reading happens in create(); set_param() only
 * swaps pointers, as docs/PORTING.md asks.
 *
 * Parameters: the sound controls (sample, pitch, start, end, mode, reverse, gain) belong to the slot chosen by "slot",
 * the way the OP-1 tweaks the key you last pressed. The envelope and the output level are shared. Slot settings and sample
 * names are saved in the "state" chunk, by sample file name.
 *
 * The wrapper hands parameters over as key/value strings, MIDI as raw bytes, and wants audio as interleaved int16 stereo at
 * 44.1 kHz. With SAMPLE_ACCURATE it calls render() with any 1..128 frames. */
#include "engine.h"
#include "op1fx_act.h"
#include "drumsampler_core.h"
#include "drumsampler_kit.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILES 64
#define NAME_LEN 48
#define MAX_FILE_BYTES (8 * 1024 * 1024)
#define MAX_FRAMES (44100 * 20)

#define ATTACK_MIN 0.0005f
#define ATTACK_MAX 5.0f
#define TIME_MIN 0.005f
#define TIME_MAX 10.0f

typedef struct {
    char name[NAME_LEN];   /* file name without extension, or the kit sound's name */
    float *data;           /* mono, owned */
    int frames;
    float rate;
} sample_t;

typedef struct {
    ds_t core;
    sample_t files[MAX_FILES];
    int nfiles;
    int slot_sample[DS_SLOTS];   /* index into files, or -1 for an empty slot */
    int sel;                     /* selected slot, 0..23 */
    float attack, decay, sustain, release, level;   /* 0..1 knob values */
    unsigned changed;            /* keys to report to the host, bit n = SLOT_KEYS[n] */
} inst_t;

static const char *const SLOT_KEYS[] = {"sample", "pitch", "start", "end", "mode", "reverse", "gain"};
#define NSLOT_KEYS 7

static float exp_time(float norm, float lo, float hi)
{
    norm = norm < 0.0f ? 0.0f : (norm > 1.0f ? 1.0f : norm);
    return lo * powf(hi / lo, norm);
}

static float exp_time_inverse(float seconds, float lo, float hi)
{
    return logf(seconds / lo) / logf(hi / lo);
}

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* ---- WAV reading: PCM 16/24/32 bit and 32 bit float, any channel count, mixed down to mono ---- */

static unsigned rd32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
static unsigned rd16(const unsigned char *p) { return p[0] | (p[1] << 8); }

static int load_wav(const char *path, sample_t *out)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 44 || size > MAX_FILE_BYTES) {
        fclose(f);
        return 0;
    }
    unsigned char *buf = (unsigned char *)malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return 0;
    }
    fclose(f);
    int ok = 0;
    if (memcmp(buf, "RIFF", 4) == 0 && memcmp(buf + 8, "WAVE", 4) == 0) {
        unsigned fmt_tag = 0, ch = 0, rate = 0, bits = 0;
        const unsigned char *data = NULL;
        unsigned data_len = 0;
        long pos = 12;
        while (pos + 8 <= size) {
            const unsigned len = rd32(buf + pos + 4);
            const unsigned char *body = buf + pos + 8;
            if (memcmp(buf + pos, "fmt ", 4) == 0 && len >= 16 && pos + 8 + 16 <= size) {
                fmt_tag = rd16(body);
                ch = rd16(body + 2);
                rate = rd32(body + 4);
                bits = rd16(body + 14);
                if (fmt_tag == 0xFFFE && len >= 26 && pos + 8 + 26 <= size)
                    fmt_tag = rd16(body + 24);   /* extensible: the sub-format's first two bytes */
            } else if (memcmp(buf + pos, "data", 4) == 0) {
                data = body;
                data_len = len;
                if ((long)data_len > size - (pos + 8))
                    data_len = (unsigned)(size - (pos + 8));
                break;
            }
            pos += 8 + (long)len + (long)(len & 1);
        }
        const unsigned bytes = bits / 8;
        if (data && ch >= 1 && ch <= 8 && rate >= 8000 && rate <= 192000 && bytes >= 2 && bytes <= 4 &&
            (fmt_tag == 1 || (fmt_tag == 3 && bytes == 4))) {
            long frames = (long)(data_len / (bytes * ch));
            if (frames > MAX_FRAMES)
                frames = MAX_FRAMES;
            float *m = frames > 0 ? (float *)malloc((size_t)frames * sizeof(float)) : NULL;
            if (m) {
                for (long i = 0; i < frames; i++) {
                    float acc = 0.0f;
                    for (unsigned c = 0; c < ch; c++) {
                        const unsigned char *p = data + (i * ch + c) * bytes;
                        float v;
                        if (fmt_tag == 3) {
                            unsigned u = rd32(p);
                            memcpy(&v, &u, 4);
                        } else if (bytes == 2) {
                            v = (float)(short)rd16(p) / 32768.0f;
                        } else if (bytes == 3) {
                            int s = (int)((p[0] << 8) | (p[1] << 16) | ((unsigned)p[2] << 24));
                            v = (float)(s >> 8) / 8388608.0f;
                        } else {
                            v = (float)(int)rd32(p) / 2147483648.0f;
                        }
                        acc += v;
                    }
                    acc /= (float)ch;
                    m[i] = isfinite(acc) ? acc : 0.0f;
                }
                out->data = m;
                out->frames = (int)frames;
                out->rate = (float)rate;
                ok = 1;
            }
        }
    }
    free(buf);
    return ok;
}

static int name_cmp(const void *a, const void *b)
{
    return strcmp(((const sample_t *)a)->name, ((const sample_t *)b)->name);
}

static int has_wav_suffix(const char *n)
{
    const size_t len = strlen(n);
    return len > 4 && (strcmp(n + len - 4, ".wav") == 0 || strcmp(n + len - 4, ".WAV") == 0 || strcmp(n + len - 4, ".Wav") == 0);
}

static void scan_folder(inst_t *p, const char *dir)
{
    if (!dir)
        return;
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && p->nfiles < MAX_FILES) {
        if (e->d_name[0] == '.' || !has_wav_suffix(e->d_name))
            continue;
        const size_t base = strlen(e->d_name) - 4;
        if (base == 0 || base >= NAME_LEN || strchr(e->d_name, ';') || strchr(e->d_name, '|'))
            continue;   /* the state chunk uses ; and | as separators */
        char path[1024];
        if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path)
            continue;
        sample_t s;
        memset(&s, 0, sizeof s);
        if (!load_wav(path, &s))
            continue;
        memcpy(s.name, e->d_name, base);
        s.name[base] = 0;
        p->files[p->nfiles++] = s;
    }
    closedir(d);
    qsort(p->files, (size_t)p->nfiles, sizeof p->files[0], name_cmp);
}

static void use_builtin_kit(inst_t *p)
{
    for (int i = 0; i < DS_KIT_SOUNDS; i++) {
        sample_t *s = &p->files[p->nfiles];
        s->data = (float *)malloc(DS_KIT_MAX_FRAMES * sizeof(float));
        if (!s->data)
            break;
        s->frames = ds_kit_render(i, s->data, DS_KIT_MAX_FRAMES);
        s->rate = (float)DS_KIT_RATE;
        snprintf(s->name, NAME_LEN, "%s", ds_kit_name(i));
        p->nfiles++;
    }
}

/* ---- slot plumbing ---- */

static void bind_sample(inst_t *p, int slot)
{
    const int idx = p->slot_sample[slot];
    if (idx >= 0 && idx < p->nfiles)
        ds_set_sample(&p->core, slot, p->files[idx].data, NULL, p->files[idx].frames, p->files[idx].rate);
    else
        ds_set_sample(&p->core, slot, NULL, NULL, 0, 44100.0f);
}

static void push_params(inst_t *p)
{
    ds_params q;
    ds_default_params(&q);
    q.attack = exp_time(p->attack, ATTACK_MIN, ATTACK_MAX);
    q.decay = exp_time(p->decay, TIME_MIN, TIME_MAX);
    q.sustain = p->sustain;
    q.release = exp_time(p->release, TIME_MIN, TIME_MAX);
    q.level = p->level;
    ds_set_params(&p->core, &q);
}

static void *eng_create(const char *data_dir)
{
    inst_t *p = (inst_t *)calloc(1, sizeof *p);
    if (!p)
        return NULL;
    ds_init(&p->core, 44100.0f);
    scan_folder(p, data_dir);
    if (p->nfiles == 0)
        use_builtin_kit(p);
    for (int s = 0; s < DS_SLOTS; s++) {
        p->slot_sample[s] = s < p->nfiles ? s : -1;
        bind_sample(p, s);
    }
    p->attack = exp_time_inverse(DS_DEFAULT_ATTACK, ATTACK_MIN, ATTACK_MAX);
    p->decay = exp_time_inverse(DS_DEFAULT_DECAY, TIME_MIN, TIME_MAX);
    p->sustain = DS_DEFAULT_SUSTAIN;
    p->release = exp_time_inverse(DS_DEFAULT_RELEASE, TIME_MIN, TIME_MAX);
    p->level = DS_DEFAULT_LEVEL;
    push_params(p);
    ds_reset(&p->core);
    return p;
}

static void eng_destroy(void *inst)
{
    inst_t *p = (inst_t *)inst;
    if (!p)
        return;
    for (int i = 0; i < p->nfiles; i++)
        free(p->files[i].data);
    free(p);
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
            ds_note_on(&p->core, d1, d2);
        else
            ds_note_off(&p->core, d1);
        break;
    case 0x80:
        ds_note_off(&p->core, d1);
        break;
    case 0xE0:
        ds_pitch_bend(&p->core, (float)(((d2 << 7) | d1) - 8192) / 8192.0f);
        break;
    case 0xB0:
        if (d1 == 64)
            ds_sustain_pedal(&p->core, d2 >= 64);
        else if (d1 == 120 || d1 == 123)
            ds_all_off(&p->core);
        break;
    default:
        break;
    }
}

static int round_int(float v) { return (int)(v < 0.0f ? v - 0.5f : v + 0.5f); }

static void eng_set_param(void *inst, const char *key, const char *val);

/* ---- state chunk: "v1;<slot>;<name>|<pitch>|<in>|<out>|<mode>|<rev>|<gain>;... (24 entries)" ---- */

static int write_state(inst_t *p, char *buf, int buf_len)
{
    int n = snprintf(buf, (size_t)buf_len, "v1;%d", p->sel + 1);
    for (int s = 0; s < DS_SLOTS && n > 0 && n < buf_len; s++) {
        const ds_slot_params *sp = &p->core.slot_params[s];
        const int idx = p->slot_sample[s];
        n += snprintf(buf + n, (size_t)(buf_len - n), ";%s|%.3f|%.4f|%.4f|%d|%d|%.4f", idx >= 0 ? p->files[idx].name : "-",
                      (double)sp->pitch, (double)sp->in, (double)sp->out, sp->mode, sp->reverse, (double)sp->gain);
    }
    return n < buf_len ? n : 0;
}

static void read_state(inst_t *p, const char *text)
{
    if (strncmp(text, "v1;", 3) != 0)
        return;
    char *copy = strdup(text);
    if (!copy)
        return;
    char *save = NULL;
    char *tok = strtok_r(copy, ";", &save);   /* "v1" */
    tok = strtok_r(NULL, ";", &save);
    if (tok) {
        const int s = atoi(tok) - 1;
        p->sel = s < 0 ? 0 : (s >= DS_SLOTS ? DS_SLOTS - 1 : s);
    }
    for (int s = 0; s < DS_SLOTS; s++) {
        tok = strtok_r(NULL, ";", &save);
        if (!tok)
            break;
        char name[NAME_LEN] = "";
        float pitch = 0, in = 0, out = 1, gain = 1;
        int mode = DS_MODE_ONESHOT, rev = 0;
        const char *bar = strchr(tok, '|');
        if (!bar || (size_t)(bar - tok) >= NAME_LEN)
            continue;
        memcpy(name, tok, (size_t)(bar - tok));
        name[bar - tok] = 0;
        if (sscanf(bar + 1, "%f|%f|%f|%d|%d|%f", &pitch, &in, &out, &mode, &rev, &gain) != 6)
            continue;
        ds_slot_params sp;
        sp.pitch = pitch;
        sp.in = in;
        sp.out = out;
        sp.mode = mode;
        sp.reverse = rev;
        sp.gain = gain;
        ds_set_slot_params(&p->core, s, &sp);
        int idx = -1;
        if (strcmp(name, "-") != 0)
            for (int i = 0; i < p->nfiles; i++)
                if (strcmp(p->files[i].name, name) == 0) {
                    idx = i;
                    break;
                }
        p->slot_sample[s] = idx;   /* a name that is gone leaves the slot empty rather than playing the wrong sound */
        bind_sample(p, s);
    }
    p->changed = (1u << NSLOT_KEYS) - 1;
    free(copy);
}

static void eng_set_param(void *inst, const char *key, const char *val)
{
    inst_t *p = (inst_t *)inst;
    if (!strcmp(key, "state")) {
        read_state(p, val);
        return;
    }
    const float v = (float)atof(val);
    ds_slot_params sp = p->core.slot_params[p->sel];
    int slot_changed = 0;
    if (!strcmp(key, "slot")) {
        const int s = round_int(v) - 1;
        const int ns = s < 0 ? 0 : (s >= DS_SLOTS ? DS_SLOTS - 1 : s);
        if (ns != p->sel) {
            p->sel = ns;
            p->changed = (1u << NSLOT_KEYS) - 1;   /* every slot control now shows another slot's value */
        }
        return;
    } else if (!strcmp(key, "sample")) {
        int idx = round_int(v);
        if (idx >= p->nfiles)
            idx = p->nfiles - 1;
        p->slot_sample[p->sel] = idx;   /* -1 when there are no samples at all */
        bind_sample(p, p->sel);
        return;
    } else if (!strcmp(key, "pitch")) {
        sp.pitch = ds_pitch_value(v);
        slot_changed = 1;
    } else if (!strcmp(key, "start")) {
        sp.in = clamp01(v);
        slot_changed = 1;
    } else if (!strcmp(key, "end")) {
        sp.out = clamp01(v);
        slot_changed = 1;
    } else if (!strcmp(key, "mode")) {
        sp.mode = round_int(v);
        slot_changed = 1;
    } else if (!strcmp(key, "reverse")) {
        sp.reverse = round_int(v) != 0;
        slot_changed = 1;
    } else if (!strcmp(key, "gain")) {
        sp.gain = clamp01(v) * 2.0f;
        slot_changed = 1;
    } else if (!strcmp(key, "attack")) {
        p->attack = v;
    } else if (!strcmp(key, "decay")) {
        p->decay = v;
    } else if (!strcmp(key, "sustain")) {
        p->sustain = v;
    } else if (!strcmp(key, "release")) {
        p->release = v;
    } else if (!strcmp(key, "level")) {
        p->level = v;
    } else {
        return;
    }
    if (slot_changed)
        ds_set_slot_params(&p->core, p->sel, &sp);
    else
        push_params(p);
}

/* The wrapper asks once per block for keys the engine changed itself (here: a slot change moves seven controls). */
int mpc_engine_changed(void *inst, char *key, int len)
{
    inst_t *p = (inst_t *)((op1_act_t *)inst)->inner; /* the wrapper owns the instance pointer the host sees */
    for (int i = 0; i < NSLOT_KEYS; i++)
        if (p->changed & (1u << i)) {
            p->changed &= ~(1u << i);
            snprintf(key, (size_t)len, "%s", SLOT_KEYS[i]);
            return 1;
        }
    return 0;
}

static int time_text(char *buf, int buf_len, float seconds)
{
    return seconds < 1.0f ? snprintf(buf, (size_t)buf_len, "%d ms", (int)(seconds * 1000.0f + 0.5f))
                          : snprintf(buf, (size_t)buf_len, "%.2f s", (double)seconds);
}

static int eng_get_param(void *inst, const char *key, char *buf, int buf_len)
{
    inst_t *p = (inst_t *)inst;
    const ds_slot_params *sp = &p->core.slot_params[p->sel];
    if (!strcmp(key, "state"))
        return write_state(p, buf, buf_len);

    if (!strcmp(key, "slot")) return snprintf(buf, (size_t)buf_len, "%d", p->sel + 1);
    if (!strcmp(key, "sample")) return snprintf(buf, (size_t)buf_len, "%d", p->slot_sample[p->sel] < 0 ? 0 : p->slot_sample[p->sel]);
    if (!strcmp(key, "pitch")) return snprintf(buf, (size_t)buf_len, "%g", (double)ds_pitch_norm(sp->pitch));
    if (!strcmp(key, "start")) return snprintf(buf, (size_t)buf_len, "%g", (double)sp->in);
    if (!strcmp(key, "end")) return snprintf(buf, (size_t)buf_len, "%g", (double)sp->out);
    if (!strcmp(key, "mode")) return snprintf(buf, (size_t)buf_len, "%d", sp->mode);
    if (!strcmp(key, "reverse")) return snprintf(buf, (size_t)buf_len, "%d", sp->reverse);
    if (!strcmp(key, "gain")) return snprintf(buf, (size_t)buf_len, "%g", (double)(sp->gain * 0.5f));
    if (!strcmp(key, "attack")) return snprintf(buf, (size_t)buf_len, "%g", (double)p->attack);
    if (!strcmp(key, "decay")) return snprintf(buf, (size_t)buf_len, "%g", (double)p->decay);
    if (!strcmp(key, "sustain")) return snprintf(buf, (size_t)buf_len, "%g", (double)p->sustain);
    if (!strcmp(key, "release")) return snprintf(buf, (size_t)buf_len, "%g", (double)p->release);
    if (!strcmp(key, "level")) return snprintf(buf, (size_t)buf_len, "%g", (double)p->level);

    /* readable value text for the MPC screen */
    if (!strcmp(key, "slot_display")) {
        const int idx = p->slot_sample[p->sel];
        return snprintf(buf, (size_t)buf_len, "%d %.18s", p->sel + 1, idx >= 0 ? p->files[idx].name : "empty");
    }
    if (!strcmp(key, "sample_display")) {
        const int idx = p->slot_sample[p->sel];
        return snprintf(buf, (size_t)buf_len, "%.23s", idx >= 0 ? p->files[idx].name : "none");
    }
    if (!strcmp(key, "pitch_display"))
        return snprintf(buf, (size_t)buf_len, "%+.1f st", (double)sp->pitch);
    if (!strcmp(key, "start_display"))
        return snprintf(buf, (size_t)buf_len, "%d", round_int(sp->in * 100.0f));
    if (!strcmp(key, "end_display"))
        return snprintf(buf, (size_t)buf_len, "%d", round_int(sp->out * 100.0f));
    if (!strcmp(key, "mode_display"))
        return snprintf(buf, (size_t)buf_len, "%s", sp->mode == DS_MODE_GATE ? "GATE" : (sp->mode == DS_MODE_LOOP ? "LOOP" : "ONE-SHOT"));
    if (!strcmp(key, "reverse_display"))
        return snprintf(buf, (size_t)buf_len, "%s", sp->reverse ? "ON" : "OFF");
    if (!strcmp(key, "gain_display"))
        return sp->gain < 0.0001f ? snprintf(buf, (size_t)buf_len, "-inf") : snprintf(buf, (size_t)buf_len, "%.1f dB", 20.0 * log10((double)sp->gain));
    if (!strcmp(key, "sustain_display"))
        return snprintf(buf, (size_t)buf_len, "%d", round_int(p->sustain * 100.0f));
    if (!strcmp(key, "level_display"))
        return snprintf(buf, (size_t)buf_len, "%d", round_int(p->level * 100.0f));
    if (!strcmp(key, "attack_display"))
        return time_text(buf, buf_len, exp_time(p->attack, ATTACK_MIN, ATTACK_MAX));
    if (!strcmp(key, "decay_display"))
        return time_text(buf, buf_len, exp_time(p->decay, TIME_MIN, TIME_MAX));
    if (!strcmp(key, "release_display"))
        return time_text(buf, buf_len, exp_time(p->release, TIME_MIN, TIME_MAX));
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
        ds_render(&p->core, l, r, n);
        for (int i = 0; i < n; i++) {
            out_lr[2 * (done + i)] = to_i16(l[i]);
            out_lr[2 * (done + i) + 1] = to_i16(r[i]);
        }
        done += n;
    }
}

/* Instrument: no process(). Add it last, the struct is positional. */
static const mpc_engine_t ENGINE_BASE = {
    eng_create, eng_destroy, eng_midi, eng_set_param, eng_get_param, eng_render, NULL, NULL,
};

const mpc_engine_t *mpc_engine(void)
{
    return op1_act_wrap(&ENGINE_BASE, "slot");
}
