/* Behavioral tests for the Sampler core. Build and run with core/tests/run.sh */
#include "../sampler_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PI 3.14159265358979323846

static int failures = 0;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
            failures++;                                   \
        }                                                 \
    } while (0)

/* ---- helpers ---- */

enum { EV_ON, EV_OFF, EV_BEND, EV_PEDAL, EV_ALLOFF, EV_MOD };
typedef struct {
    int frame;
    int type;
    int a;
    float b;
} event;

static void apply(sampler_t *f, const event *e)
{
    switch (e->type) {
    case EV_ON: sampler_note_on(f, e->a, (int)e->b); break;
    case EV_OFF: sampler_note_off(f, e->a); break;
    case EV_BEND: sampler_pitch_bend(f, e->b); break;
    case EV_PEDAL: sampler_sustain_pedal(f, e->a); break;
    case EV_ALLOFF: sampler_all_off(f); break;
    case EV_MOD: sampler_mod_wheel(f, e->b); break;
    }
}

/* Render total frames with events at exact frames, calls never longer than block. Events must be sorted. */
static void run_script(sampler_t *f, const event *ev, int nev, float *l, float *r, int total, int block)
{
    int cur = 0, ei = 0;
    while (cur < total) {
        while (ei < nev && ev[ei].frame <= cur) apply(f, &ev[ei++]);
        int n = total - cur;
        if (n > block) n = block;
        if (ei < nev && ev[ei].frame - cur < n) n = ev[ei].frame - cur;
        sampler_render(f, l + cur, r + cur, n);
        cur += n;
    }
}

static float *alloc_f(int n)
{
    float *p = (float *)calloc((size_t)n + 1, sizeof(float));
    if (!p) { printf("out of memory\n"); exit(2); }
    return p;
}

static float *make_sine(double hz, double rate, int frames, float amp)
{
    float *d = alloc_f(frames);
    for (int i = 0; i < frames; i++) d[i] = amp * (float)sin(2.0 * PI * hz * (double)i / rate);
    return d;
}

static sampler_sample view(const float *d, int frames, float rate)
{
    sampler_sample s;
    s.data = d;
    s.frames = frames;
    s.rate = rate;
    return s;
}

static float rms(const float *x, int from, int to)
{
    double a = 0;
    for (int i = from; i < to; i++) a += (double)x[i] * x[i];
    return (float)sqrt(a / (to > from ? to - from : 1));
}

static float peak_of(const float *x, int from, int to)
{
    float p = 0;
    for (int i = from; i < to; i++) if (fabsf(x[i]) > p) p = fabsf(x[i]);
    return p;
}

static int all_finite(const float *x, int n)
{
    for (int i = 0; i < n; i++) if (!isfinite(x[i])) return 0;
    return 1;
}

static float max_step(const float *x, int from, int to)
{
    float m = 0;
    for (int i = from + 1; i < to; i++) if (fabsf(x[i] - x[i - 1]) > m) m = fabsf(x[i] - x[i - 1]);
    return m;
}

/* Frequency from positive-going zero crossings in [from, to), linearly interpolated. */
static double measure_hz(const float *x, int from, int to, double rate)
{
    double first = -1, last = -1;
    int count = 0;
    for (int i = from + 1; i < to; i++) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double t = (double)i - 1.0 + (double)(-x[i - 1]) / (double)(x[i] - x[i - 1]);
            if (first < 0) first = t;
            last = t;
            count++;
        }
    }
    if (count < 3) return 0;
    return (double)(count - 1) * rate / (last - first);
}

static double cents(double a, double b) { return 1200.0 * log2(a / b); }

/* A synth with a looped long sine sample at 441 Hz (100 samples a cycle at 44100), root 69. */
#define SINE_FRAMES 88200
#define SINE_LOOP 0.02268f /* crossfade of 1000 frames, a whole number of cycles, so the seam is in phase */

static void setup_sine(sampler_t *s, float sr, const float *sine)
{
    sampler_init(s, sr);
    sampler_sample v = view(sine, SINE_FRAMES, 44100.0f);
    sampler_set_sample(s, &v);
    sampler_params p;
    sampler_default_params(&p);
    p.root = 69.0f;
    p.loop = SINE_LOOP;
    p.attack = 0.001f;
    sampler_set_params(s, &p);
}

/* ---- tests ---- */

static void test_pitch(void)
{
    float *sine = make_sine(441.0, 44100.0, SINE_FRAMES, 0.5f);
    const float rates[] = {22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f};
    const int notes[] = {45, 57, 69, 81, 93};
    double worst = 0;
    for (size_t ri = 0; ri < sizeof rates / sizeof rates[0]; ri++)
        for (size_t ni = 0; ni < sizeof notes / sizeof notes[0]; ni++) {
            sampler_t *s = (sampler_t *)malloc(sizeof *s);
            setup_sine(s, rates[ri], sine);
            const int total = (int)(0.8f * rates[ri]);
            float *l = alloc_f(total), *r = alloc_f(total);
            const event ev[] = {{0, EV_ON, notes[ni], 127.0f}};
            run_script(s, ev, 1, l, r, total, 128);
            const double want = 441.0 * pow(2.0, (notes[ni] - 69) / 12.0);
            const double got = measure_hz(l, (int)(0.1f * rates[ri]), total, rates[ri]);
            const double err = fabs(cents(got, want));
            if (err > worst) worst = err;
            CHECK(err < 5.0, "sr %.0f note %d: wanted %.2f Hz, measured %.2f Hz (%.2f cents off)", (double)rates[ri], notes[ni], want, got, err);
            free(l); free(r); free(s);
        }
    printf("pitch: worst error %.5f cents over %d note/rate pairs (limit 5)\n", worst, 25);
    free(sine);
}

static void test_tune_and_bend(void)
{
    float *sine = make_sine(441.0, 44100.0, SINE_FRAMES, 0.5f);
    const int total = 44100;
    float *l = alloc_f(total), *r = alloc_f(total);
    const float tunes[] = {-12.0f, -3.0f, 0.0f, 5.0f, 12.0f};
    for (size_t i = 0; i < sizeof tunes / sizeof tunes[0]; i++) {
        sampler_t *s = (sampler_t *)malloc(sizeof *s);
        setup_sine(s, 44100.0f, sine);
        sampler_params p = s->target;
        p.tune = tunes[i];
        sampler_set_params(s, &p);
        sampler_reset(s); /* start with the smoothing already at the new tune */
        sampler_note_on(s, 69, 127);
        sampler_render(s, l, r, total);
        const double want = 441.0 * pow(2.0, tunes[i] / 12.0);
        const double got = measure_hz(l, 6000, total, 44100.0);
        CHECK(fabs(cents(got, want)) < 5.0, "tune %+.0f st: wanted %.2f, got %.2f", (double)tunes[i], want, got);
        free(s);
    }
    /* full bend up with the default range of 2 semitones */
    sampler_t *s = (sampler_t *)malloc(sizeof *s);
    setup_sine(s, 44100.0f, sine);
    sampler_note_on(s, 69, 127);
    sampler_pitch_bend(s, 1.0f);
    sampler_render(s, l, r, total);
    const double want = 441.0 * pow(2.0, 2.0 / 12.0);
    const double got = measure_hz(l, 6000, total, 44100.0);
    CHECK(fabs(cents(got, want)) < 5.0, "bend: wanted %.2f, got %.2f", want, got);
    free(s); free(l); free(r); free(sine);
}

static void test_polyphony(void)
{
    const int total = 44100;
    float *demo = alloc_f(sampler_demo_frames(0, 44100.0f));
    const int nd = sampler_demo_frames(0, 44100.0f);
    sampler_demo_render(0, 44100.0f, demo, nd);
    sampler_t *s = (sampler_t *)malloc(sizeof *s);
    sampler_init(s, 44100.0f);
    sampler_sample v = view(demo, nd, 44100.0f);
    sampler_set_sample(s, &v);
    float *l = alloc_f(total), *r = alloc_f(total);
    event ev[12];
    for (int i = 0; i < 8; i++) { ev[i].frame = i * 100; ev[i].type = EV_ON; ev[i].a = 48 + i * 3; ev[i].b = 127.0f; }
    ev[8].frame = 5000; ev[8].type = EV_ON; ev[8].a = 90; ev[8].b = 127.0f; /* a ninth note steals a voice */
    run_script(s, ev, 9, l, r, total, 128);
    CHECK(all_finite(l, total) && all_finite(r, total), "NaN or inf with 9 notes");
    const float pk = peak_of(l, 0, total);
    CHECK(pk <= 1.0f && pk > 0.05f, "polyphony peak %f", (double)pk);
    CHECK(sampler_active_voices(s) <= SAMPLER_VOICES, "voice count");
    printf("polyphony: 9 notes on 8 voices, peak %.3f, finite\n", (double)pk);
    free(demo); free(s); free(l); free(r);
}

static void test_release(void)
{
    float *sine = make_sine(441.0, 44100.0, SINE_FRAMES, 0.5f);
    sampler_t *s = (sampler_t *)malloc(sizeof *s);
    setup_sine(s, 44100.0f, sine);
    sampler_params p = s->target;
    p.release = 0.1f;
    sampler_set_params(s, &p);
    const int total = 3 * 44100;
    float *l = alloc_f(total), *r = alloc_f(total);
    const event ev[] = {{0, EV_ON, 69, 127.0f}, {22050, EV_OFF, 69, 0}};
    run_script(s, ev, 2, l, r, total, 128);
    const float before = rms(l, 10000, 22000);
    /* 60 dB in 0.1 s: after 0.5 s the tail is 1/1e5 of the sustain, far below -80 dB */
    const float after = peak_of(l, 22050 + 22050, total);
    CHECK(before > 0.05f, "sustain level %f", (double)before);
    CHECK(after < 1e-4f, "tail after release %g", (double)after);
    CHECK(sampler_active_voices(s) == 0, "voice still active after release");
    /* time to fall 60 dB: measure the envelope of the peak per 100 frames */
    int t60 = -1;
    const float ref = peak_of(l, 21000, 22050);
    for (int i = 22050; i + 100 < total; i += 100)
        if (peak_of(l, i, i + 100) < ref * 0.001f) { t60 = i - 22050; break; }
    CHECK(t60 > 0 && fabs((double)t60 / 44100.0 - 0.1) < 0.02, "release time to -60 dB measured %.3f s, wanted 0.1", (double)t60 / 44100.0);

    /* sustain pedal: note off with the pedal down keeps sounding until the pedal comes up */
    sampler_reset(s);
    const event ev2[] = {{0, EV_PEDAL, 1, 0}, {0, EV_ON, 69, 127.0f}, {5000, EV_OFF, 69, 0}, {40000, EV_PEDAL, 0, 0}};
    run_script(s, ev2, 4, l, r, total, 128);
    CHECK(rms(l, 20000, 38000) > 0.05f, "pedal did not hold the note");
    CHECK(peak_of(l, 40000 + 22050, total) < 1e-4f, "pedal release did not end the note");
    free(s); free(l); free(r); free(sine);
}

static void test_no_clicks(void)
{
    /* A constant sample makes every click a step: the steady output has none, so any step is the steal or retrigger. */
    const int n = 20000;
    float *dc = alloc_f(n);
    for (int i = 0; i < n; i++) dc[i] = 0.2f;
    const int total = 30000;
    float *l = alloc_f(total), *r = alloc_f(total);
    sampler_t *s = (sampler_t *)malloc(sizeof *s);
    sampler_params p;
    sampler_default_params(&p);
    p.loop = 0.05f;
    p.attack = 0.001f;
    p.sustain = 1.0f;

    /* one voice alone, to know how big one voice is */
    sampler_init(s, 44100.0f);
    sampler_sample v = view(dc, n, 44100.0f);
    sampler_set_sample(s, &v);
    sampler_set_params(s, &p);
    sampler_note_on(s, 60, 127);
    sampler_render(s, l, r, 5000);
    const float one = l[4999];
    CHECK(one > 0.05f, "single voice level %f", (double)one);

    /* retrigger the same note while it sounds: it must not step by anything near a whole voice */
    sampler_init(s, 44100.0f);
    sampler_set_sample(s, &v);
    sampler_set_params(s, &p);
    const event ev[] = {{0, EV_ON, 60, 127.0f}, {10003, EV_ON, 60, 127.0f}};
    run_script(s, ev, 2, l, r, total, 128);
    float worst = max_step(l, 9000, 14000);
    printf("retrigger: one voice = %.4f, largest step around the retrigger %.5f (%.1f%% of a voice)\n", (double)one, (double)worst, 100.0 * (double)worst / (double)one);
    CHECK(worst < one * 0.05f, "retrigger step %f against a voice of %f", (double)worst, (double)one);

    /* steal: eight notes held, a ninth takes the oldest */
    sampler_init(s, 44100.0f);
    sampler_set_sample(s, &v);
    sampler_set_params(s, &p);
    event ev8[10];
    for (int i = 0; i < 8; i++) { ev8[i].frame = 0; ev8[i].type = EV_ON; ev8[i].a = 57 + i; ev8[i].b = 127.0f; }
    ev8[8].frame = 20001; ev8[8].type = EV_ON; ev8[8].a = 80; ev8[8].b = 127.0f;
    run_script(s, ev8, 9, l, r, total, 128);
    worst = max_step(l, 19000, 26000);
    printf("steal: largest step around the steal %.5f (%.1f%% of a voice)\n", (double)worst, 100.0 * (double)worst / (double)one);
    CHECK(worst < one * 0.05f, "steal step %f against a voice of %f", (double)worst, (double)one);

    /* a new sample while notes sound: the old voices are cut and decay out as a remainder */
    float *other = alloc_f(n);
    for (int i = 0; i < n; i++) other[i] = -0.2f;
    sampler_init(s, 44100.0f);
    sampler_set_sample(s, &v);
    sampler_set_params(s, &p);
    sampler_note_on(s, 60, 127);
    sampler_note_on(s, 64, 127);
    sampler_render(s, l, r, 15000);
    sampler_sample v2 = view(other, n, 44100.0f);
    sampler_set_sample(s, &v2);
    sampler_note_on(s, 60, 127);
    sampler_render(s, l + 15000, r + 15000, 5000);
    worst = max_step(l, 14000, 20000);
    printf("sample swap: largest step %.5f (%.1f%% of a voice)\n", (double)worst, 100.0 * (double)worst / (double)one);
    CHECK(worst < one * 0.1f, "sample swap step %f", (double)worst);
    free(other); free(dc);
    free(s); free(l); free(r);
}

/* Time in frames from note on until the sample is gone (|x| < thresh for the rest), -1 if it never ends. */
static int sounding_length(const float *x, int n, float thresh)
{
    int last = -1;
    for (int i = 0; i < n; i++) if (fabsf(x[i]) > thresh) last = i;
    return last;
}

static void test_knobs(void)
{
    const int total = 4 * 44100;
    float *l = alloc_f(total), *r = alloc_f(total), *l2 = alloc_f(total), *r2 = alloc_f(total);
    const int nbar = sampler_demo_frames(0, 44100.0f), npad = sampler_demo_frames(1, 44100.0f);
    float *bar = alloc_f(nbar), *pad = alloc_f(npad);
    sampler_demo_render(0, 44100.0f, bar, nbar);
    sampler_demo_render(1, 44100.0f, pad, npad);
    sampler_t *s = (sampler_t *)malloc(sizeof *s);
    sampler_sample vb = view(bar, nbar, 44100.0f), vp = view(pad, npad, 44100.0f);

    /* START: a later start plays a later, quieter part of a decaying bar */
    sampler_init(s, 44100.0f);
    sampler_set_sample(s, &vb);
    sampler_params p;
    sampler_default_params(&p);
    sampler_set_params(s, &p);
    sampler_note_on(s, 60, 127);
    sampler_render(s, l, r, 20000);
    p.start = 0.5f;
    sampler_set_params(s, &p);
    sampler_reset(s);
    sampler_note_on(s, 60, 127);
    sampler_render(s, l2, r2, 20000);
    const float rs0 = rms(l, 0, 20000), rs1 = rms(l2, 0, 20000);
    printf("start: rms from 0.0 = %.4f, from 0.5 = %.4f\n", (double)rs0, (double)rs1);
    CHECK(rs1 < rs0 * 0.5f, "START did not move playback later (%f vs %f)", (double)rs1, (double)rs0);

    /* END: shortening the region shortens a one-shot. pad is 1 s long */
    sampler_init(s, 44100.0f);
    sampler_set_sample(s, &vp);
    sampler_default_params(&p);
    sampler_set_params(s, &p);
    sampler_note_on(s, 60, 127);
    sampler_render(s, l, r, total);
    const int full = sounding_length(l, total, 0.01f);
    p.end = 0.25f;
    sampler_set_params(s, &p);
    sampler_reset(s);
    sampler_note_on(s, 60, 127);
    sampler_render(s, l2, r2, total);
    const int quarter = sounding_length(l2, total, 0.01f);
    printf("end: one-shot lasts %.3f s at end=1.0, %.3f s at end=0.25\n", full / 44100.0, quarter / 44100.0);
    CHECK(fabs(full / 44100.0 - 1.0) < 0.03, "full length %f s", full / 44100.0);
    CHECK(fabs(quarter / 44100.0 - 0.25) < 0.03, "quarter length %f s", quarter / 44100.0);

    /* LOOP: off, the 1 s pad is gone after 2.5 s; on, it carries on */
    sampler_default_params(&p);
    sampler_set_params(s, &p);
    sampler_reset(s);
    sampler_note_on(s, 60, 127);
    sampler_render(s, l, r, total);
    const float off_tail = rms(l, 3 * 44100, total);
    p.loop = 0.1f;
    sampler_set_params(s, &p);
    sampler_reset(s);
    sampler_note_on(s, 60, 127);
    sampler_render(s, l2, r2, total);
    const float on_tail = rms(l2, 3 * 44100, total);
    printf("loop: tail rms off %.5f, on %.4f\n", (double)off_tail, (double)on_tail);
    CHECK(off_tail < 1e-4f && on_tail > 0.1f, "LOOP off %f on %f", (double)off_tail, (double)on_tail);
    CHECK(all_finite(l2, total), "loop output not finite");

    /* envelope: attack time, sustain level, level */
    float *sine = make_sine(441.0, 44100.0, SINE_FRAMES, 0.5f);
    setup_sine(s, 44100.0f, sine);
    p = s->target;
    p.attack = 0.2f;
    p.decay = 0.2f;
    p.sustain = 0.3f;
    sampler_set_params(s, &p);
    sampler_note_on(s, 69, 127);
    sampler_render(s, l, r, total);
    int t90 = -1;
    const float top = peak_of(l, 0, 44100);
    for (int i = 0; i + 100 < total; i += 50)
        if (peak_of(l, i, i + 100) >= 0.9f * top) { t90 = i; break; }
    CHECK(t90 > 0 && fabs(t90 / 44100.0 - 0.18) < 0.03, "attack: reached 90%% at %.3f s, wanted about 0.18", t90 / 44100.0);
    const float sus = rms(l, 3 * 44100, 4 * 44100) * 1.41421356f; /* a sine, so peak = rms * sqrt 2 */
    const float expect_sus = 0.5f * 0.7f * 1.0f * 0.3f * sampler_level_gain(0.8f);
    printf("envelope: attack 90%% at %.3f s, sustain peak %.4f (expect %.4f)\n", t90 / 44100.0, (double)sus, (double)expect_sus);
    CHECK(fabsf(sus - expect_sus) < 0.1f * expect_sus, "sustain %f vs %f", (double)sus, (double)expect_sus);

    p.attack = 0.002f;
    p.decay = 1.0f;
    p.sustain = 1.0f;
    p.level = 1.0f;
    sampler_set_params(s, &p);
    sampler_reset(s);
    sampler_note_on(s, 69, 127);
    sampler_render(s, l, r, 22050);
    const float pk_full = peak_of(l, 5000, 22050);
    p.level = 0.5f;
    sampler_set_params(s, &p);
    sampler_reset(s);
    sampler_note_on(s, 69, 127);
    sampler_render(s, l, r, 22050);
    const float pk_half = peak_of(l, 5000, 22050);
    CHECK(fabsf(pk_half / pk_full - 0.25f) < 0.01f, "level 0.5 should give 0.25 of full amplitude, got %f", (double)(pk_half / pk_full));

    /* velocity is linear gain */
    p.level = 1.0f;
    sampler_set_params(s, &p);
    sampler_reset(s);
    sampler_note_on(s, 69, 64);
    sampler_render(s, l, r, 22050);
    CHECK(fabsf(peak_of(l, 5000, 22050) / pk_full - 64.0f / 127.0f) < 0.01f, "velocity scaling");

    free(sine); free(pad); free(bar); free(s); free(l); free(r); free(l2); free(r2);
}

static void test_mod_wheel(void)
{
    float *sine = make_sine(441.0, 44100.0, SINE_FRAMES, 0.5f);
    sampler_t *s = (sampler_t *)malloc(sizeof *s);
    const int total = 44100;
    float *a = alloc_f(total), *b = alloc_f(total), *r = alloc_f(total);
    setup_sine(s, 44100.0f, sine);
    sampler_note_on(s, 69, 127);
    sampler_render(s, a, r, total);
    setup_sine(s, 44100.0f, sine);
    sampler_mod_wheel(s, 1.0f);
    sampler_note_on(s, 69, 127);
    sampler_render(s, b, r, total);
    double d = 0;
    for (int i = 0; i < total; i++) d += fabs((double)a[i] - b[i]);
    CHECK(d > 10.0, "mod wheel changed nothing");
    CHECK(all_finite(b, total), "mod wheel NaN");
    free(s); free(a); free(b); free(r); free(sine);
}

static void test_determinism_and_split(void)
{
    const int total = 30000;
    const int nd = sampler_demo_frames(2, 44100.0f);
    float *demo = alloc_f(nd);
    sampler_demo_render(2, 44100.0f, demo, nd);
    sampler_sample v = view(demo, nd, 44100.0f);
    const event ev[] = {
        {0, EV_ON, 36, 100.0f}, {500, EV_ON, 43, 90.0f}, {3000, EV_BEND, 0, 0.5f}, {6000, EV_MOD, 0, 0.7f},
        {9000, EV_OFF, 36, 0}, {12001, EV_ON, 40, 127.0f}, {15000, EV_PEDAL, 1, 0}, {16000, EV_OFF, 43, 0},
        {20000, EV_PEDAL, 0, 0}, {22000, EV_ALLOFF, 0, 0},
    };
    const int blocks[] = {1, 7, 64, 128, 4096};
    float *ref_l = alloc_f(total), *ref_r = alloc_f(total);
    float *l = alloc_f(total), *r = alloc_f(total);
    sampler_t *s = (sampler_t *)malloc(sizeof *s);
    for (size_t bi = 0; bi < sizeof blocks / sizeof blocks[0]; bi++) {
        sampler_init(s, 44100.0f);
        sampler_set_sample(s, &v);
        sampler_params p;
        sampler_default_params(&p);
        p.loop = 0.1f;
        p.root = 36.0f;
        sampler_set_params(s, &p);
        run_script(s, ev, 10, bi == 0 ? ref_l : l, bi == 0 ? ref_r : r, total, blocks[bi]);
        if (bi > 0)
            CHECK(memcmp(ref_l, l, sizeof(float) * (size_t)total) == 0 && memcmp(ref_r, r, sizeof(float) * (size_t)total) == 0,
                  "output depends on block size (%d vs 1)", blocks[bi]);
    }
    CHECK(rms(ref_l, 0, total) > 0.01f, "determinism script is silent");
    /* fresh run of the same script is bit-identical */
    sampler_init(s, 44100.0f);
    sampler_set_sample(s, &v);
    sampler_params p;
    sampler_default_params(&p);
    p.loop = 0.1f;
    p.root = 36.0f;
    sampler_set_params(s, &p);
    run_script(s, ev, 10, l, r, total, 128);
    CHECK(memcmp(ref_l, l, sizeof(float) * (size_t)total) == 0, "second run differs from first");
    printf("determinism: bit-identical across block sizes 1, 7, 64, 128, 4096 and across runs\n");
    free(demo); free(ref_l); free(ref_r); free(l); free(r); free(s);
}

static void test_headroom(void)
{
    const int n = 4000;
    float *sq = alloc_f(n);
    for (int i = 0; i < n; i++) sq[i] = ((i / 50) & 1) ? -1.0f : 1.0f; /* full-scale square */
    const int total = 20000;
    float *l = alloc_f(total), *r = alloc_f(total);
    sampler_t *s = (sampler_t *)malloc(sizeof *s);
    sampler_init(s, 44100.0f);
    sampler_sample v = view(sq, n, 44100.0f);
    sampler_set_sample(s, &v);
    sampler_params p;
    sampler_default_params(&p);
    p.level = 1.0f;
    p.loop = 0.05f;
    p.attack = 0.0005f;
    sampler_set_params(s, &p);
    for (int i = 0; i < 8; i++) sampler_note_on(s, 60, 127); /* same note retriggers one voice: use distinct ones below */
    for (int i = 0; i < 8; i++) sampler_note_on(s, 60 + i, 127);
    sampler_render(s, l, r, total);
    const float pk = peak_of(l, 0, total);
    printf("headroom: 8 full-scale square voices at max velocity and level, peak %.4f\n", (double)pk);
    CHECK(pk <= 1.0f, "peak %f over 0 dBFS", (double)pk);
    CHECK(all_finite(l, total), "headroom NaN");
    free(sq); free(l); free(r); free(s);
}

static void test_robustness(void)
{
    const float rates[] = {22050.0f, 44100.0f, 192000.0f};
    const int nd = sampler_demo_frames(0, 44100.0f);
    float *demo = alloc_f(nd);
    sampler_demo_render(0, 44100.0f, demo, nd);
    sampler_t *s = (sampler_t *)malloc(sizeof *s);
    for (size_t ri = 0; ri < 3; ri++) {
        const int total = (int)(rates[ri] * 0.5f);
        float *l = alloc_f(total), *r = alloc_f(total);
        sampler_init(s, rates[ri]);
        sampler_sample v = view(demo, nd, 44100.0f);
        sampler_set_sample(s, &v);
        sampler_params p;
        sampler_default_params(&p);
        p.tune = 12.0f; p.loop = 1.0f; p.start = 0.9f; p.end = 0.1f; p.bend_range = 24.0f; p.attack = 0.0f; p.release = 0.0f;
        sampler_set_params(s, &p);
        sampler_pitch_bend(s, 1.0f);
        sampler_mod_wheel(s, 1.0f);
        const event ev[] = {{0, EV_ON, 127, 127.0f}, {10, EV_ON, 0, 1.0f}, {50, EV_OFF, 127, 0}, {60, EV_ON, 128, 127.0f}};
        run_script(s, ev, 4, l, r, total, 1);
        CHECK(all_finite(l, total) && all_finite(r, total), "NaN at sr %.0f extreme settings", (double)rates[ri]);
        CHECK(peak_of(l, 0, total) <= 1.0f, "extreme settings clip: %f", (double)peak_of(l, 0, total));
        free(l); free(r);
    }

    /* no sample, and tiny samples, must not crash and stay finite */
    float tiny[7] = {0.1f, -0.2f, 0.3f, -0.4f, 0.5f, -0.6f, 0.7f};
    float out_l[4096], out_r[4096];
    sampler_init(s, 44100.0f);
    sampler_note_on(s, 60, 100);
    sampler_render(s, out_l, out_r, 4096);
    CHECK(peak_of(out_l, 0, 4096) == 0.0f, "no sample should be silent");
    sampler_sample none = {NULL, 0, 44100.0f};
    sampler_set_sample(s, &none);
    sampler_note_on(s, 60, 100);
    sampler_render(s, out_l, out_r, 4096);
    CHECK(peak_of(out_l, 0, 4096) == 0.0f, "NULL sample should be silent");
    for (int n = 1; n <= 7; n++) {
        sampler_init(s, 44100.0f);
        sampler_sample v = view(tiny, n, 8000.0f);
        sampler_set_sample(s, &v);
        sampler_params p;
        sampler_default_params(&p);
        p.loop = 0.5f;
        sampler_set_params(s, &p);
        sampler_note_on(s, 60, 127);
        sampler_note_on(s, 72, 127);
        sampler_render(s, out_l, out_r, 4096);
        CHECK(all_finite(out_l, 4096), "tiny sample %d frames not finite", n);
        p.loop = 0.0f;
        sampler_set_params(s, &p);
        sampler_note_on(s, 48, 127);
        sampler_render(s, out_l, out_r, 4096);
        CHECK(all_finite(out_l, 4096), "tiny sample %d frames one-shot not finite", n);
    }
    /* NaN parameters are ignored rather than propagated */
    sampler_init(s, 44100.0f);
    sampler_sample v = view(demo, nd, 44100.0f);
    sampler_set_sample(s, &v);
    sampler_params p;
    sampler_default_params(&p);
    p.attack = NAN; p.decay = NAN; p.release = NAN;
    sampler_set_params(s, &p);
    sampler_note_on(s, 60, 127);
    sampler_render(s, out_l, out_r, 4096);
    CHECK(all_finite(out_l, 4096), "NaN envelope params leaked into the output");
    printf("robustness: sr 22050/44100/192000 block 1 with extreme settings, no sample, 1..7 frame samples, NaN params: finite\n");
    free(demo); free(s);
}

static void test_demo_samples(void)
{
    for (int k = 0; k < SAMPLER_DEMO_KINDS; k++) {
        const float rates[] = {22050.0f, 44100.0f, 48000.0f};
        for (int ri = 0; ri < 3; ri++) {
            const int n = sampler_demo_frames(k, rates[ri]);
            float *d = alloc_f(n);
            sampler_demo_render(k, rates[ri], d, n);
            CHECK(all_finite(d, n), "demo %d not finite", k);
            const float pk = peak_of(d, 0, n);
            CHECK(pk > 0.5f && pk <= 0.81f, "demo %d peak %f", k, (double)pk);
            if (k == 1) { /* the pad repeats exactly: the step from the last frame back to the first is an ordinary step */
                const float wrap = fabsf(d[0] - d[n - 1]);
                CHECK(wrap <= max_step(d, 0, n) * 1.05f + 1e-4f, "pad does not loop cleanly: wrap step %f vs max %f", (double)wrap, (double)max_step(d, 0, n));
            } else {
                CHECK(fabsf(d[n - 1]) < 1e-3f && fabsf(d[0]) < 1e-3f, "demo %d does not start and end at zero", k);
            }
            free(d);
        }
    }
    printf("demo samples: %d kinds finite, peak 0.7..0.8, pad loops without a step, others start and end at zero\n", SAMPLER_DEMO_KINDS);
}

static void test_activity(void)
{
    float *sine = make_sine(441.0, 44100.0, SINE_FRAMES, 0.5f);
    sampler_t *s = (sampler_t *)malloc(sizeof *s);
    setup_sine(s, 44100.0f, sine);
    float l[2048], r[2048];
    sampler_activity a;
    sampler_take_activity(s, &a);
    CHECK(a.head < 0.0f && a.voices == 0, "idle activity");
    const unsigned n0 = a.notes;
    sampler_note_on(s, 69, 127);
    sampler_render(s, l, r, 2048);
    sampler_take_activity(s, &a);
    const float h1 = a.head;
    sampler_render(s, l, r, 2048);
    sampler_take_activity(s, &a);
    CHECK(a.notes != n0 && a.voices == 1 && a.peak > 0.05f, "activity after a note: notes %u voices %d peak %f", a.notes, a.voices, (double)a.peak);
    CHECK(a.head > h1 && h1 > 0.0f, "playhead should move forward: %f then %f", (double)h1, (double)a.head);
    free(sine); free(s);
}

static void test_tune_mapping(void)
{
    CHECK(sampler_tune_from_norm(0.5f) == 0.0f, "tune center");
    CHECK(fabsf(sampler_tune_from_norm(0.0f) + 12.0f) < 1e-5f && fabsf(sampler_tune_from_norm(1.0f) - 12.0f) < 1e-5f, "tune ends");
    CHECK(fabsf(sampler_norm_from_tune(sampler_tune_from_norm(0.8f)) - 0.8f) < 1e-5f, "tune round trip");
}

#ifdef SAMPLER_TIMING_ONLY
static void bench(void)
{
    const int nd = sampler_demo_frames(0, 44100.0f);
    float *demo = alloc_f(nd);
    sampler_demo_render(0, 44100.0f, demo, nd);
    sampler_t *s = (sampler_t *)malloc(sizeof *s);
    sampler_init(s, 44100.0f);
    sampler_sample v = view(demo, nd, 44100.0f);
    sampler_set_sample(s, &v);
    sampler_params p;
    sampler_default_params(&p);
    p.loop = 0.2f;
    sampler_set_params(s, &p);
    for (int i = 0; i < 8; i++) sampler_note_on(s, 48 + i * 4, 100);
    float l[128], r[128];
    const int blocks = 20000;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int b = 0; b < blocks; b++) sampler_render(s, l, r, 128);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double us = ((double)(t1.tv_sec - t0.tv_sec) * 1e9 + (double)(t1.tv_nsec - t0.tv_nsec)) / 1e3 / blocks;
    printf("bench: 8 looping voices, %.2f us per 128-frame block = %.3f%% of the 2902 us budget\n", us, us / 2902.0 * 100.0);
    free(demo); free(s);
}
#endif

int main(void)
{
#ifdef SAMPLER_TIMING_ONLY
    bench();
    return 0;
#else
    test_tune_mapping();
    test_pitch();
    test_tune_and_bend();
    test_polyphony();
    test_release();
    test_no_clicks();
    test_knobs();
    test_mod_wheel();
    test_determinism_and_split();
    test_headroom();
    test_robustness();
    test_demo_samples();
    test_activity();
    if (failures) {
        printf("%d FAILED\n", failures);
        return 1;
    }
    printf("ALL CORE TESTS PASSED\n");
    return 0;
#endif
}
