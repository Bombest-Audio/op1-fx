/* Sampler as an MPC OS instrument: glue between the shared DSP core and the mpc-vst-plugins wrapper.
 * The wrapper hands parameters over as key/value strings, MIDI as raw bytes, and wants audio as interleaved int16
 * stereo at 44.1 kHz. With SAMPLE_ACCURATE it calls render() with any 1..128 frames.
 *
 * Samples: the list is the three built-in generated samples, then every .wav in the "samples" folder next to the .so
 * (one level, sorted by name, symlinks skipped). The "sample" parameter is a whole number index into that list. A file
 * is read on a worker thread, never inside set_param or render: set_param only records the wish and wakes the worker,
 * which publishes the decoded buffer under a mutex that render takes with trylock, so audio never waits on disk. */
#define _GNU_SOURCE
#include "engine.h"
#include "op1fx_act.h"
#include "sampler_core.h"
#include "sampler_wav.h"

#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define ATTACK_MIN 0.0005f
#define ATTACK_MAX 5.0f
#define TIME_MIN 0.005f
#define TIME_MAX 10.0f
#define MAX_ENTRIES 32
#define MAX_SAMPLE_SECONDS 30.0f
#define PATH_LEN 640

typedef struct {
    char name[64];
    char path[PATH_LEN]; /* empty for a built-in */
    int builtin;         /* demo kind when path is empty */
    int failed;          /* the last attempt to read it failed */
} entry_t;

typedef struct {
    float start, end, tune, loop, attack, decay, sustain, release, level;
} knobs_t;

typedef struct {
    sampler_t core;
    knobs_t k;
    int root;
    float *cur; /* buffer the core reads; only the render path touches it */

    pthread_mutex_t mu;
    pthread_cond_t cv;
    pthread_t thread;
    int quit;
    char dir[PATH_LEN];
    entry_t list[MAX_ENTRIES];
    int count;
    int wanted;        /* index the user picked, as received */
    int worker_has;    /* index the worker last finished, -1 at start */
    float *pending;    /* decoded buffer waiting for render to pick up */
    int pending_frames, pending_root;
    float pending_rate;
    float *retired;    /* buffer render has dropped, for the worker to free */
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
    sampler_params q;
    sampler_default_params(&q);
    q.start = p->k.start;
    q.end = p->k.end;
    q.tune = sampler_tune_from_norm(p->k.tune);
    q.loop = p->k.loop;
    q.attack = exp_time(p->k.attack, ATTACK_MIN, ATTACK_MAX);
    q.decay = exp_time(p->k.decay, TIME_MIN, TIME_MAX);
    q.sustain = p->k.sustain;
    q.release = exp_time(p->k.release, TIME_MIN, TIME_MAX);
    q.level = p->k.level;
    q.root = (float)p->root;
    sampler_set_params(&p->core, &q);
}

static int cmp_entries(const void *a, const void *b)
{
    return strcasecmp(((const entry_t *)a)->name, ((const entry_t *)b)->name);
}

/* Fills the list: built-ins first, then the folder. Runs on the worker. */
static void scan(inst_t *p)
{
    entry_t files[MAX_ENTRIES];
    int nf = 0;
    DIR *d = p->dir[0] ? opendir(p->dir) : NULL;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL && nf < MAX_ENTRIES - SAMPLER_DEMO_KINDS) {
            const size_t len = strlen(e->d_name);
            if (len < 5 || strcasecmp(e->d_name + len - 4, ".wav") != 0 || e->d_name[0] == '.') continue;
            entry_t *f = &files[nf];
            memset(f, 0, sizeof *f);
            if (snprintf(f->path, sizeof f->path, "%s/%s", p->dir, e->d_name) >= (int)sizeof f->path) continue;
            struct stat st;
            if (lstat(f->path, &st) != 0 || !S_ISREG(st.st_mode)) continue; /* regular files only, no symlinks */
            snprintf(f->name, sizeof f->name, "%.*s", (int)(len - 4 > 63 ? 63 : len - 4), e->d_name);
            nf++;
        }
        closedir(d);
    }
    qsort(files, (size_t)nf, sizeof files[0], cmp_entries);
    pthread_mutex_lock(&p->mu);
    for (int k = 0; k < SAMPLER_DEMO_KINDS; k++) {
        entry_t *b = &p->list[k];
        memset(b, 0, sizeof *b);
        snprintf(b->name, sizeof b->name, "%s", sampler_demo_name(k));
        b->builtin = k;
    }
    for (int i = 0; i < nf; i++) p->list[SAMPLER_DEMO_KINDS + i] = files[i];
    p->count = SAMPLER_DEMO_KINDS + nf;
    pthread_mutex_unlock(&p->mu);
}

static float *load_entry(const entry_t *e, int *frames, float *rate, int *root)
{
    if (e->path[0]) return sampler_wav_read(e->path, MAX_SAMPLE_SECONDS, frames, rate, root);
    *rate = 44100.0f;
    *frames = sampler_demo_frames(e->builtin, *rate);
    float *d = (float *)malloc(sizeof(float) * (size_t)*frames);
    if (!d) return NULL;
    sampler_demo_render(e->builtin, *rate, d, *frames);
    *root = sampler_demo_root(e->builtin);
    return d;
}

static void *worker(void *arg)
{
    inst_t *p = (inst_t *)arg;
    scan(p);
    pthread_mutex_lock(&p->mu);
    while (!p->quit) {
        if (p->retired) {
            free(p->retired);
            p->retired = NULL;
        }
        int idx = p->wanted;
        if (idx < 0) idx = 0;
        if (idx >= p->count) idx = p->count - 1;
        if (idx == p->worker_has) {
            pthread_cond_wait(&p->cv, &p->mu);
            continue;
        }
        entry_t e = p->list[idx];
        pthread_mutex_unlock(&p->mu); /* the read happens with the lock released */
        int frames = 0, root = 60;
        float rate = 44100.0f;
        float *d = load_entry(&e, &frames, &rate, &root);
        pthread_mutex_lock(&p->mu);
        p->worker_has = idx;
        if (d) {
            free(p->pending);
            p->pending = d;
            p->pending_frames = frames;
            p->pending_rate = rate;
            p->pending_root = root;
            p->list[idx].failed = 0;
        } else {
            p->list[idx].failed = 1; /* keep playing what is loaded */
        }
    }
    pthread_mutex_unlock(&p->mu);
    return NULL;
}

static void *eng_create(const char *data_dir)
{
    inst_t *p = (inst_t *)calloc(1, sizeof *p);
    if (!p) return NULL;
    sampler_init(&p->core, 44100.0f);
    p->k.start = SAMPLER_DEFAULT_START;
    p->k.end = SAMPLER_DEFAULT_END;
    p->k.tune = sampler_norm_from_tune(SAMPLER_DEFAULT_TUNE);
    p->k.loop = SAMPLER_DEFAULT_LOOP;
    p->k.attack = exp_time_inverse(SAMPLER_DEFAULT_ATTACK, ATTACK_MIN, ATTACK_MAX);
    p->k.decay = exp_time_inverse(SAMPLER_DEFAULT_DECAY, TIME_MIN, TIME_MAX);
    p->k.sustain = SAMPLER_DEFAULT_SUSTAIN;
    p->k.release = exp_time_inverse(SAMPLER_DEFAULT_RELEASE, TIME_MIN, TIME_MAX);
    p->k.level = SAMPLER_DEFAULT_LEVEL;
    p->root = sampler_demo_root(0);
    if (data_dir) snprintf(p->dir, sizeof p->dir, "%s", data_dir);
    pthread_mutex_init(&p->mu, NULL);
    pthread_cond_init(&p->cv, NULL);

    /* The first built-in is ready before create() returns, so the first note after loading already sounds. */
    for (int k = 0; k < SAMPLER_DEMO_KINDS; k++) {
        snprintf(p->list[k].name, sizeof p->list[k].name, "%s", sampler_demo_name(k));
        p->list[k].builtin = k;
    }
    p->count = SAMPLER_DEMO_KINDS;
    int frames = 0, root = 60;
    float rate = 44100.0f;
    p->cur = load_entry(&p->list[0], &frames, &rate, &root);
    if (p->cur) {
        sampler_sample v = {p->cur, frames, rate};
        sampler_set_sample(&p->core, &v);
    }
    p->worker_has = 0;
    push_params(p);
    sampler_reset(&p->core);
    if (pthread_create(&p->thread, NULL, worker, p) != 0) {
        /* no worker: the built-ins stay selectable only through what is already loaded */
        p->quit = 1;
    }
    return p;
}

static void eng_destroy(void *inst)
{
    inst_t *p = (inst_t *)inst;
    pthread_mutex_lock(&p->mu);
    const int joinable = !p->quit;
    p->quit = 1;
    pthread_cond_broadcast(&p->cv);
    pthread_mutex_unlock(&p->mu);
    if (joinable) pthread_join(p->thread, NULL);
    free(p->pending);
    free(p->retired);
    free(p->cur);
    pthread_mutex_destroy(&p->mu);
    pthread_cond_destroy(&p->cv);
    free(p);
}

static void eng_midi(void *inst, const uint8_t *msg, int len)
{
    inst_t *p = (inst_t *)inst;
    if (len < 1) return;
    const int status = msg[0] & 0xF0;
    const int d1 = len > 1 ? msg[1] & 0x7F : 0;
    const int d2 = len > 2 ? msg[2] & 0x7F : 0;
    switch (status) {
    case 0x90:
        if (len > 2 && d2 > 0) sampler_note_on(&p->core, d1, d2);
        else sampler_note_off(&p->core, d1);
        break;
    case 0x80:
        sampler_note_off(&p->core, d1);
        break;
    case 0xE0:
        sampler_pitch_bend(&p->core, (float)(((d2 << 7) | d1) - 8192) / 8192.0f);
        break;
    case 0xB0:
        if (d1 == 1) sampler_mod_wheel(&p->core, (float)d2 / 127.0f);
        else if (d1 == 64) sampler_sustain_pedal(&p->core, d2 >= 64);
        else if (d1 == 120 || d1 == 123) sampler_all_off(&p->core);
        break;
    default:
        break;
    }
}

static void eng_set_param(void *inst, const char *key, const char *val)
{
    inst_t *p = (inst_t *)inst;
    const float v = (float)atof(val);
    if (!strcmp(key, "sample")) {
        int idx = (int)lroundf(v);
        if (idx < 0) idx = 0;
        pthread_mutex_lock(&p->mu); /* held only for a few instructions, never across a file read */
        p->wanted = idx;
        pthread_cond_signal(&p->cv);
        pthread_mutex_unlock(&p->mu);
        return;
    }
    float *dst = NULL;
    if (!strcmp(key, "start")) dst = &p->k.start;
    else if (!strcmp(key, "end")) dst = &p->k.end;
    else if (!strcmp(key, "tune")) dst = &p->k.tune;
    else if (!strcmp(key, "loop")) dst = &p->k.loop;
    else if (!strcmp(key, "attack")) dst = &p->k.attack;
    else if (!strcmp(key, "decay")) dst = &p->k.decay;
    else if (!strcmp(key, "sustain")) dst = &p->k.sustain;
    else if (!strcmp(key, "release")) dst = &p->k.release;
    else if (!strcmp(key, "level")) dst = &p->k.level;
    if (!dst) return;
    *dst = v;
    push_params(p);
}

static int time_text(char *buf, int buf_len, float seconds)
{
    return seconds < 1.0f ? snprintf(buf, (size_t)buf_len, "%d ms", (int)(seconds * 1000.0f + 0.5f))
                          : snprintf(buf, (size_t)buf_len, "%.2f s", (double)seconds);
}

static int eng_get_param(void *inst, const char *key, char *buf, int buf_len)
{
    inst_t *p = (inst_t *)inst;
    const struct { const char *name; float v; } plain[] = {
        {"start", p->k.start}, {"end", p->k.end}, {"tune", p->k.tune}, {"loop", p->k.loop},
        {"attack", p->k.attack}, {"decay", p->k.decay}, {"sustain", p->k.sustain}, {"release", p->k.release},
        {"level", p->k.level},
    };
    for (size_t i = 0; i < sizeof plain / sizeof plain[0]; i++)
        if (!strcmp(key, plain[i].name)) return snprintf(buf, (size_t)buf_len, "%g", (double)plain[i].v);

    if (!strcmp(key, "sample")) {
        pthread_mutex_lock(&p->mu);
        const int w = p->wanted; /* raw: an index past the end shows the last entry's name and plays it */
        pthread_mutex_unlock(&p->mu);
        return snprintf(buf, (size_t)buf_len, "%d", w);
    }
    if (!strcmp(key, "sample_display")) {
        pthread_mutex_lock(&p->mu);
        int idx = p->wanted;
        if (idx >= p->count) idx = p->count - 1;
        const int r = snprintf(buf, (size_t)buf_len, "%s%s", p->list[idx].name, p->list[idx].failed ? " (unreadable)" : "");
        pthread_mutex_unlock(&p->mu);
        return r;
    }
    if (!strcmp(key, "start_display")) return snprintf(buf, (size_t)buf_len, "%d", (int)(p->k.start * 100.0f + 0.5f));
    if (!strcmp(key, "end_display")) return snprintf(buf, (size_t)buf_len, "%d", (int)(p->k.end * 100.0f + 0.5f));
    if (!strcmp(key, "tune_display")) return snprintf(buf, (size_t)buf_len, "%+.1f", (double)sampler_tune_from_norm(p->k.tune));
    if (!strcmp(key, "loop_display")) {
        if (p->k.loop <= 0.0f) return snprintf(buf, (size_t)buf_len, "OFF");
        return snprintf(buf, (size_t)buf_len, "%d", (int)(p->k.loop * 100.0f + 0.5f));
    }
    if (!strcmp(key, "sustain_display")) return snprintf(buf, (size_t)buf_len, "%d", (int)(p->k.sustain * 100.0f + 0.5f));
    if (!strcmp(key, "level_display")) return snprintf(buf, (size_t)buf_len, "%d", (int)(p->k.level * 100.0f + 0.5f));
    if (!strcmp(key, "attack_display")) return time_text(buf, buf_len, exp_time(p->k.attack, ATTACK_MIN, ATTACK_MAX));
    if (!strcmp(key, "decay_display")) return time_text(buf, buf_len, exp_time(p->k.decay, TIME_MIN, TIME_MAX));
    if (!strcmp(key, "release_display")) return time_text(buf, buf_len, exp_time(p->k.release, TIME_MIN, TIME_MAX));
    return 0;
}

static inline int16_t to_i16(float x)
{
    x *= 32768.0f;
    x = x > 32767.0f ? 32767.0f : (x < -32768.0f ? -32768.0f : x);
    return (int16_t)(x < 0.0f ? x - 0.5f : x + 0.5f);
}

/* Take a freshly decoded sample if the worker has one. Never waits: if the lock is busy, try again next block. */
static void pick_up_sample(inst_t *p)
{
    if (pthread_mutex_trylock(&p->mu) != 0) return;
    if (p->pending && !p->retired) {
        float *old = p->cur;
        p->cur = p->pending;
        p->pending = NULL;
        p->retired = old; /* freed by the worker, not here */
        p->root = p->pending_root;
        sampler_sample v = {p->cur, p->pending_frames, p->pending_rate};
        sampler_set_sample(&p->core, &v);
        push_params(p);
        pthread_cond_signal(&p->cv); /* wake the worker to free the retired buffer */
    }
    pthread_mutex_unlock(&p->mu);
}

static void eng_render(void *inst, int16_t *out_lr, int frames)
{
    inst_t *p = (inst_t *)inst;
    float l[128], r[128];
    pick_up_sample(p);
    int done = 0;
    while (done < frames) {
        const int n = frames - done > 128 ? 128 : frames - done;
        sampler_render(&p->core, l, r, n);
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
    return op1_act_wrap(&ENGINE_BASE, "start");
}
