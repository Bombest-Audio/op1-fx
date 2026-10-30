/* Behavioral tests for the Phase core. Build and run with core/tests/run.sh */
#include "../phase_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define PI 3.14159265358979323846

static unsigned rng_state = 12345u;
static float frand(void) /* -1..1 */
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) / (float)(1u << 24) * 2.0f - 1.0f;
}
static float urand(void) { return 0.5f + 0.5f * frand(); }

/* ---- helpers ---- */

enum { EV_ON, EV_OFF, EV_PARAMS, EV_BEND, EV_MOD, EV_PEDAL, EV_ALLOFF };
typedef struct {
    int at, kind, note;
    float val;
    phase_params prm;
} ev_t;

static phase_params base_params(void)
{
    phase_params p;
    phase_default_params(&p);
    p.attack = 0.002f;
    p.sustain = 1.0f;  /* a steady level, so measurements see a periodic signal */
    p.level = 1.0f;
    return p;
}

static phase_params knobs(float a, float b, float r, float w)
{
    phase_params p = base_params();
    p.a = a; p.b = b; p.r = r; p.w = w;
    return p;
}

/* Run events through the core. Blocks are cut at the host block grid and at every event, as the plugin does. */
static void simulate(phase_t *p, const ev_t *ev, int nev, float *l, float *r, int total, int block)
{
    int pos = 0, e = 0;
    while (pos < total) {
        while (e < nev && ev[e].at <= pos) {
            switch (ev[e].kind) {
            case EV_ON: phase_note_on(p, ev[e].note, ev[e].val); break;
            case EV_OFF: phase_note_off(p, ev[e].note); break;
            case EV_PARAMS: phase_set_params(p, &ev[e].prm); break;
            case EV_BEND: phase_pitch_bend(p, ev[e].val); break;
            case EV_MOD: phase_mod_wheel(p, ev[e].val); break;
            case EV_PEDAL: phase_sustain(p, ev[e].note); break;
            case EV_ALLOFF: phase_all_off(p); break;
            }
            e++;
        }
        int end = (pos / block + 1) * block;
        if (end > total) end = total;
        if (e < nev && ev[e].at < end) end = ev[e].at;
        phase_render(p, l + pos, r + pos, end - pos);
        pos = end;
    }
}

static int all_finite_bounded(const float *x, int n, float bound)
{
    for (int i = 0; i < n; i++)
        if (!isfinite(x[i]) || fabsf(x[i]) > bound)
            return 0;
    return 1;
}

static float peak_of(const float *x, int n)
{
    float m = 0;
    for (int i = 0; i < n; i++) m = fmaxf(m, fabsf(x[i]));
    return m;
}

static float rms_of(const float *x, int n)
{
    double s = 0;
    for (int i = 0; i < n; i++) s += (double)x[i] * x[i];
    return (float)sqrt(s / (n > 0 ? n : 1));
}

static float max_step(const float *x, int n)
{
    float m = 0;
    for (int i = 1; i < n; i++) m = fmaxf(m, fabsf(x[i] - x[i - 1]));
    return m;
}

/* Play held notes from time 0 and return n samples. */
static float *play(phase_params prm, const int *notes, int nn, float vel, float sr, int n, float **right_out)
{
    phase_t *p = malloc(sizeof *p);
    float *l = malloc(sizeof(float) * (size_t)n), *r = malloc(sizeof(float) * (size_t)n);
    ev_t ev[16];
    int ne = 0;
    memset(ev, 0, sizeof ev);
    ev[ne].at = 0; ev[ne].kind = EV_PARAMS; ev[ne].prm = prm; ne++;
    for (int i = 0; i < nn; i++) { ev[ne].at = 0; ev[ne].kind = EV_ON; ev[ne].note = notes[i]; ev[ne].val = vel; ne++; }
    phase_init(p, sr);
    phase_set_params(p, &prm);
    phase_reset(p);
    simulate(p, ev, ne, l, r, n, 128);
    free(p);
    if (right_out) *right_out = r; else free(r);
    return l;
}

/* In-place radix-2 complex FFT. n is a power of two. */
static void fft(double *re, double *im, int n)
{
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * PI / len;
        const double wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                const int a = i + k, b = i + k + len / 2;
                const double xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - xr; im[b] = im[a] - xi;
                re[a] += xr; im[a] += xi;
                const double t = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = t;
            }
        }
    }
}

#define FFT_N 32768
/* Hann-windowed power spectrum (bins 0..N/2) of x[0..FFT_N). */
static void power_spectrum(const float *x, double *pw)
{
    static double re[FFT_N], im[FFT_N];
    for (int i = 0; i < FFT_N; i++) {
        re[i] = x[i] * (0.5 - 0.5 * cos(2.0 * PI * i / FFT_N));
        im[i] = 0;
    }
    fft(re, im, FFT_N);
    for (int i = 0; i <= FFT_N / 2; i++) pw[i] = re[i] * re[i] + im[i] * im[i];
}

/* Level (dB) of harmonic k of f0, summing the bins within +-3 of its centre. */
static double harmonic_db(const double *pw, double f0, double sr, int k)
{
    const int c = (int)lround(k * f0 / sr * FFT_N);
    double s = 0;
    for (int b = c - 3; b <= c + 3; b++)
        if (b >= 0 && b <= FFT_N / 2) s += pw[b];
    return 10.0 * log10(s + 1e-30);
}

static double centroid_hz(const double *pw, double sr)
{
    double num = 0, den = 0;
    for (int b = 1; b <= FFT_N / 2; b++) {
        const double f = b * sr / FFT_N;
        num += f * pw[b];
        den += pw[b];
    }
    return den > 0 ? num / den : 0.0;
}

/* Fundamental from the autocorrelation peak near K periods, so the lag error is divided by K. Returns Hz. */
static double estimate_freq(const float *x, int n, double sr, double f_expected)
{
    const double T = sr / f_expected;
    int K = (int)(0.2 * f_expected);
    if (K < 1) K = 1;
    const double lag_c = K * T;
    const int span = (int)(0.3 * T) + 1;
    const int lo = (int)lag_c - span, hi = (int)lag_c + span;
    const int m = n - hi - 1;
    double mean = 0;
    for (int i = 0; i < n; i++) mean += x[i];
    mean /= n;
    double best = -1e30, rv[3] = {0, 0, 0};
    int bl = lo;
    static double cache[1 << 14];
    if (hi - lo + 2 >= (int)(sizeof cache / sizeof cache[0])) return 0.0;
    for (int lag = lo; lag <= hi + 1; lag++) {
        double s = 0;
        for (int i = 0; i < m; i++) s += (x[i] - mean) * (x[i + lag] - mean);
        cache[lag - lo] = s;
    }
    for (int lag = lo + 1; lag <= hi; lag++)
        if (cache[lag - lo] > best) { best = cache[lag - lo]; bl = lag; }
    rv[0] = cache[bl - 1 - lo]; rv[1] = cache[bl - lo]; rv[2] = cache[bl + 1 - lo];
    const double den = rv[0] - 2 * rv[1] + rv[2];
    const double off = den != 0 ? 0.5 * (rv[0] - rv[2]) / den : 0.0;
    return K * sr / (bl + off);
}

static double cents(double f, double ref) { return 1200.0 * log2(f / ref); }

/* ---- tests ---- */

static void test_shape(void)
{
    for (int i = 0; i < 2000; i++) {
        const float a = urand(), b = urand(), r = urand() < 0.3f ? 0.0f : urand(), w = urand() < 0.3f ? 0.0f : urand();
        CHECK(phase_shape(0.0f, a, b, r, w) == 0.0f, "shape(0) = %g", (double)phase_shape(0.0f, a, b, r, w));
        CHECK(fabsf(phase_shape(0.99999f, a, b, r, w)) < 2e-3f, "shape near wrap = %g at a=%g b=%g r=%g w=%g",
              (double)phase_shape(0.99999f, a, b, r, w), (double)a, (double)b, (double)r, (double)w);
        for (int k = 0; k < 20; k++) {
            const float v = phase_shape(urand() * 0.999f, a, b, r, w);
            CHECK(isfinite(v) && fabsf(v) <= 1.0f / (1.0f - 0.35f * w) + 1e-4f, "shape out of range %g", (double)v);
        }
    }
    float worst = 0;
    for (int i = 0; i < 4096; i++) {
        const float ph = (float)i / 4096.0f;
        worst = fmaxf(worst, fabsf(phase_shape(ph, 0, 0, 0, 0) - sinf(6.28318530718f * ph)));
    }
    CHECK(worst < 1e-5f, "all knobs at 0 should be a plain sine, worst error %g", (double)worst);
    /* the warp has slope 1 at both ends for every exponent the knob can produce, is monotonic up to exponent 2.1 and
     * past that steps back by under 1e-3 of a half cycle */
    {
        float worst_dec = 1e9f, worst_end = 0, worst_low = 1e9f;
        for (int gi = 0; gi <= 100; gi++) {
            const float g = exp2f(3.0f * (float)gi / 100.0f);
            float prev = 0;
            for (int i = 1; i <= 2000; i++) {
                const float u = (float)i / 2000.0f, v = phase_warp(u, g);
                worst_dec = fminf(worst_dec, v - prev);
                if (g <= 2.1f) worst_low = fminf(worst_low, v - prev);
                prev = v;
            }
            const float e = 1e-3f;
            worst_end = fmaxf(worst_end, fabsf((phase_warp(e, g) - phase_warp(0, g)) / e - 1.0f));
            worst_end = fmaxf(worst_end, fabsf((phase_warp(1, g) - phase_warp(1 - e, g)) / e - 1.0f));
        }
        CHECK(worst_low >= 0.0f, "warp is not monotonic up to exponent 2.1 (smallest step %g)", (double)worst_low);
        CHECK(worst_dec > -1e-3f, "warp steps back by %g", (double)worst_dec);
        CHECK(worst_end < 0.02f, "warp end slope differs from 1 by %g", (double)worst_end);
        printf("warp: monotonic up to exponent 2.1, worst step back over exponents 1..8 is %.2e of a half cycle, end slopes within %.3f of 1\n", (double)worst_dec, (double)worst_end);
    }
    printf("shape: plain sine error %.2e, zero at 0 and at the wrap for 2000 random settings\n", (double)worst);

    /* continuity in the knobs: a tiny knob change moves the wave by a tiny amount, including R near 0 */
    float worst_k = 0;
    for (int i = 0; i < 4000; i++) {
        const float ph = urand() * 0.999f;
        const float a = urand(), b = urand(), r = urand(), w = urand();
        const float d = 1e-4f;
        worst_k = fmaxf(worst_k, fabsf(phase_shape(ph, a + d, b, r, w) - phase_shape(ph, a, b, r, w)));
        worst_k = fmaxf(worst_k, fabsf(phase_shape(ph, a, b + d, r, w) - phase_shape(ph, a, b, r, w)));
        worst_k = fmaxf(worst_k, fabsf(phase_shape(ph, a, b, r + d, w) - phase_shape(ph, a, b, r, w)));
        worst_k = fmaxf(worst_k, fabsf(phase_shape(ph, a, b, r, w + d) - phase_shape(ph, a, b, r, w)));
    }
    float worst_r0 = 0;
    for (int i = 0; i < 2000; i++) {
        const float ph = urand() * 0.999f;
        worst_r0 = fmaxf(worst_r0, fabsf(phase_shape(ph, 0.5f, 0.2f, 1e-4f, 0.3f) - phase_shape(ph, 0.5f, 0.2f, 0.0f, 0.3f)));
    }
    CHECK(worst_k < 0.1f, "knob step of 1e-4 moved the wave by %g", (double)worst_k);
    CHECK(worst_r0 < 0.05f, "R = 1e-4 moved the wave by %g relative to R = 0", (double)worst_r0);
    printf("shape: largest wave change for a 1e-4 knob step %.3f (R near 0: %.4f)\n", (double)worst_k, (double)worst_r0);
}

static void test_pitch(void)
{
    const float srs[] = {22050.f, 32000.f, 44100.f, 48000.f, 88200.f, 96000.f, 192000.f};
    const int notes[] = {28, 40, 52, 60, 69, 81, 96, 108};
    const phase_params sets[2] = {base_params(), knobs(1, 1, 1, 1)};
    double worst = 0;
    int cnt = 0;
    for (size_t s = 0; s < sizeof srs / sizeof srs[0]; s++)
        for (size_t k = 0; k < sizeof notes / sizeof notes[0]; k++)
            for (int ps = 0; ps < 2; ps++) {
                const float sr = srs[s];
                const double f = phase_note_hz(notes[k]);
                if (f > sr * 0.2) continue;  /* only keys the host rate can reproduce with a few partials */
                const int n = (int)(0.9f * sr);
                float *x = play(sets[ps], &notes[k], 1, 1.0f, sr, n, NULL);
                const int skip = (int)(0.2f * sr);
                const double est = estimate_freq(x + skip, n - skip, sr, f);
                const double err = fabs(cents(est, f));
                if (err > worst) worst = err;
                if (err > 1.0) printf("  note %d sr %g set %d: %.2f cents\n", notes[k], (double)sr, ps, err);
                CHECK(err < 5.0, "note %d at %g Hz: measured %.3f Hz, %.2f cents off (sr %g, set %d)", notes[k], f, est, err, (double)sr, ps);
                cnt++;
                free(x);
            }
    printf("pitch: %d note/rate/knob combinations, worst error %.3f cents (limit 5)\n", cnt, worst);

    /* pitch bend */
    for (int rng = 0; rng < 2; rng++) {
        const float ranges[2] = {2.0f, 12.0f};
        phase_params prm = base_params();
        prm.bend_range = ranges[rng];
        phase_t *p = malloc(sizeof *p);
        const int n = 44100;
        float *l = malloc(sizeof(float) * n), *r = malloc(sizeof(float) * n);
        ev_t ev[3];
        memset(ev, 0, sizeof ev);
        ev[0].kind = EV_PARAMS; ev[0].prm = prm;
        ev[1].kind = EV_ON; ev[1].note = 57; ev[1].val = 1.0f;
        ev[2].at = 100; ev[2].kind = EV_BEND; ev[2].val = 1.0f;
        phase_init(p, 44100.f);
        simulate(p, ev, 3, l, r, n, 128);
        double f = phase_note_hz(57) * pow(2.0, ranges[rng] / 12.0);
        double est = estimate_freq(l + 11000, n - 11000, 44100.0, f);
        CHECK(fabs(cents(est, f)) < 5.0, "bend up range %g: %.2f cents off", (double)ranges[rng], cents(est, f));
        ev[2].val = -1.0f;
        phase_init(p, 44100.f);
        simulate(p, ev, 3, l, r, n, 128);
        f = phase_note_hz(57) * pow(2.0, -ranges[rng] / 12.0);
        est = estimate_freq(l + 11000, n - 11000, 44100.0, f);
        CHECK(fabs(cents(est, f)) < 5.0, "bend down range %g: %.2f cents off", (double)ranges[rng], cents(est, f));
        printf("bend: range %g semitones, up and down within 5 cents (last error %.3f)\n", (double)ranges[rng], cents(est, f));
        free(p); free(l); free(r);
    }
}

static void test_poly_and_headroom(void)
{
    int notes[8];
    for (int i = 0; i < 8; i++) notes[i] = 36 + i * 3;
    const float cases[][4] = {{0.2442f, 0, 0, 0}, {1, 1, 1, 1}, {1, 0, 1, 0}, {0, 1, 0, 1}, {1, 1, 0, 0}, {0, 0, 1, 1}, {0, 0, 0, 0}};
    float worst_peak = 0;
    for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        phase_params prm = knobs(cases[c][0], cases[c][1], cases[c][2], cases[c][3]);
        prm.attack = 0.001f;
        float *r;
        float *l = play(prm, notes, 8, 1.0f, 44100.f, 44100, &r);
        CHECK(all_finite_bounded(l, 44100, 1.0f) && all_finite_bounded(r, 44100, 1.0f), "case %zu: not finite or above 0 dBFS", c);
        worst_peak = fmaxf(worst_peak, fmaxf(peak_of(l, 44100), peak_of(r, 44100)));
        free(l); free(r);
    }
    printf("headroom: 8 voices, velocity 1, level 1, 7 knob corners: peak %.4f (limit 1.0)\n", (double)worst_peak);
    CHECK(worst_peak <= 1.0f, "peak %g above full scale", (double)worst_peak);

    /* a single centred note at the defaults */
    const int one = 60;
    phase_params d = base_params();
    d.level = PHASE_DEFAULT_LEVEL;
    float *l = play(d, &one, 1, 1.0f, 44100.f, 44100, NULL);
    printf("headroom: one note at the defaults and velocity 1 peaks at %.3f (%.1f dBFS)\n", (double)peak_of(l, 44100), 20.0 * log10(peak_of(l, 44100)));
    free(l);

    /* nine notes: the ninth must steal, not crash or exceed the bounds */
    phase_t *p = malloc(sizeof *p);
    float *a = malloc(sizeof(float) * 44100), *b = malloc(sizeof(float) * 44100);
    ev_t ev[20];
    memset(ev, 0, sizeof ev);
    int ne = 0;
    ev[ne].kind = EV_PARAMS; ev[ne].prm = knobs(1, 1, 1, 1); ne++;
    for (int i = 0; i < 12; i++) { ev[ne].at = i * 2000; ev[ne].kind = EV_ON; ev[ne].note = 40 + i * 2; ev[ne].val = 1.0f; ne++; }
    phase_init(p, 44100.f);
    simulate(p, ev, ne, a, b, 44100, 100);
    int voices = 0;
    phase_take_activity(p, &voices);
    CHECK(voices == PHASE_VOICES, "12 held notes should leave %d sounding voices, got %d", PHASE_VOICES, voices);
    CHECK(all_finite_bounded(a, 44100, 1.0f) && all_finite_bounded(b, 44100, 1.0f), "stealing produced NaN or overs");
    printf("polyphony: 12 notes into %d voices, %d sounding, output finite and bounded\n", PHASE_VOICES, voices);
    free(p); free(a); free(b);
}

static void test_release(void)
{
    for (int pass = 0; pass < 2; pass++) {
        const float rel = pass ? 1.0f : 0.05f;
        phase_params prm = base_params();
        prm.release = rel;
        phase_t *p = malloc(sizeof *p);
        const int n = 3 * 44100;
        float *l = malloc(sizeof(float) * n), *r = malloc(sizeof(float) * n);
        ev_t ev[3];
        memset(ev, 0, sizeof ev);
        ev[0].kind = EV_PARAMS; ev[0].prm = prm;
        ev[1].kind = EV_ON; ev[1].note = 52; ev[1].val = 0.9f;
        ev[2].at = 20000; ev[2].kind = EV_OFF; ev[2].note = 52;
        phase_init(p, 44100.f);
        simulate(p, ev, 3, l, r, n, 256);
        const float before = rms_of(l + 15000, 4000);
        /* time at which the 20 ms RMS falls 40 dB below the held level */
        int t40 = -1;
        for (int i = 20000; i + 882 < n; i += 441)
            if (rms_of(l + i, 882) < before * 0.01f) { t40 = i - 20000; break; }
        const float tail = peak_of(l + n - 20000, 20000);
        int voices = 0;
        phase_take_activity(p, &voices);
        CHECK(t40 > 0, "release %g: never fell 40 dB", (double)rel);
        CHECK(tail < 1e-5f, "release %g: tail peak %g not silent", (double)rel, (double)tail);
        CHECK(voices == 0, "release %g: %d voices still sounding", (double)rel, voices);
        const double expect = rel * 44100.0 * 40.0 / 60.0;
        CHECK(fabs(t40 - expect) < 0.35 * expect + 900, "release %g: -40 dB at %d samples, expected near %.0f", (double)rel, t40, expect);
        printf("release: %g s release, -40 dB after %d samples (expected about %.0f), tail peak %.2e, voices %d\n", (double)rel, t40, expect, (double)tail, voices);
        free(p); free(l); free(r);
    }
}

static void test_pedal_and_all_off(void)
{
    phase_params prm = base_params();
    prm.release = 0.05f;
    phase_t *p = malloc(sizeof *p);
    const int n = 44100;
    float *l = malloc(sizeof(float) * n), *r = malloc(sizeof(float) * n);
    ev_t ev[8];
    memset(ev, 0, sizeof ev);
    ev[0].kind = EV_PARAMS; ev[0].prm = prm;
    ev[1].kind = EV_PEDAL; ev[1].note = 1;
    ev[2].at = 100; ev[2].kind = EV_ON; ev[2].note = 55; ev[2].val = 1.0f;
    ev[3].at = 5000; ev[3].kind = EV_OFF; ev[3].note = 55;
    ev[4].at = 22050; ev[4].kind = EV_PEDAL; ev[4].note = 0;
    phase_init(p, 44100.f);
    simulate(p, ev, 5, l, r, n, 128);
    const float held = rms_of(l + 15000, 2000), after = peak_of(l + n - 4000, 4000);
    CHECK(held > 0.05f, "pedal down: note should keep sounding after note-off, rms %g", (double)held);
    CHECK(after < 1e-5f, "pedal up: note should release, peak %g", (double)after);
    printf("pedal: held rms %.3f after note-off, silent after pedal up (%.2e)\n", (double)held, (double)after);

    memset(ev, 0, sizeof ev);
    ev[0].kind = EV_PARAMS; ev[0].prm = prm;
    for (int i = 0; i < 4; i++) { ev[1 + i].kind = EV_ON; ev[1 + i].note = 50 + i * 4; ev[1 + i].val = 0.8f; }
    ev[5].at = 10000; ev[5].kind = EV_ALLOFF;
    phase_init(p, 44100.f);
    simulate(p, ev, 6, l, r, n, 128);
    CHECK(peak_of(l + n - 4000, 4000) < 1e-5f, "all notes off should release every voice");
    printf("all notes off: 4 voices released to silence\n");
    free(p); free(l); free(r);
}

static void test_clicks(void)
{
    phase_params prm = knobs(0.6f, 0.4f, 0.3f, 0.2f);
    prm.release = 1.0f;
    phase_t *p = malloc(sizeof *p);
    const int n = 3 * 44100;
    float *l = malloc(sizeof(float) * n), *r = malloc(sizeof(float) * n);
    ev_t ev[64];
    int ne;

    /* steady state first: eight held low notes, the largest sample-to-sample step is the reference */
    memset(ev, 0, sizeof ev);
    ne = 0;
    ev[ne].kind = EV_PARAMS; ev[ne].prm = prm; ne++;
    for (int i = 0; i < 8; i++) { ev[ne].at = i * 10; ev[ne].kind = EV_ON; ev[ne].note = 36 + i * 2; ev[ne].val = 0.9f; ne++; }
    phase_init(p, 44100.f);
    simulate(p, ev, ne, l, r, n, 128);
    const float steady = fmaxf(max_step(l + 22050, 44100), max_step(r + 22050, 44100));

    /* now steal a voice every 40 ms and retrigger a held key */
    ne = 0;
    memset(ev, 0, sizeof ev);
    ev[ne].kind = EV_PARAMS; ev[ne].prm = prm; ne++;
    for (int i = 0; i < 8; i++) { ev[ne].at = i * 10; ev[ne].kind = EV_ON; ev[ne].note = 36 + i * 2; ev[ne].val = 0.9f; ne++; }
    for (int i = 0; i < 20; i++) {
        ev[ne].at = 30000 + i * 1764; ev[ne].kind = EV_ON; ev[ne].val = 0.9f;
        ev[ne].note = (i % 2) ? 36 + (i % 8) * 2 : 37 + (i % 14);  /* odd i: retrigger a held key, even i: a new key in the same octave range that must steal */
        ne++;
    }
    phase_init(p, 44100.f);
    simulate(p, ev, ne, l, r, n, 128);
    const int from = 29000, to = 30000 + 20 * 1764 + 4000;
    const float stolen = fmaxf(max_step(l + from, to - from), max_step(r + from, to - from));
    const float limit = 1.15f * steady;
    CHECK(stolen <= limit, "steal/retrigger step %g above limit %g", (double)stolen, (double)limit);
    printf("clicks: steady-state max step %.4f, max step while stealing and retriggering %.4f (limit 1.15x steady = %.4f)\n",
           (double)steady, (double)stolen, (double)limit);

    /* the same limit must be able to see a click: hard-cut four voices (what a steal without a fade would do to the
     * stolen voice, summed over four steals) and the step has to exceed it */
    ne = 0;
    memset(ev, 0, sizeof ev);
    ev[ne].kind = EV_PARAMS; ev[ne].prm = prm; ne++;
    for (int i = 0; i < 8; i++) { ev[ne].at = i * 10; ev[ne].kind = EV_ON; ev[ne].note = 36 + i * 2; ev[ne].val = 0.9f; ne++; }
    phase_init(p, 44100.f);
    simulate(p, ev, ne, l, r, 40000, 128);
    float tmp_l[8], tmp_r[8], ref_l[8], ref_r[8];
    phase_t *q = malloc(sizeof *q);
    memcpy(q, p, sizeof *q);
    phase_render(p, ref_l, ref_r, 8);
    for (int i = 0; i < 4; i++) q->voice[i].stage = 0;
    phase_render(q, tmp_l, tmp_r, 8);
    float jump = 0;
    for (int i = 0; i < 8; i++) jump = fmaxf(jump, fabsf(tmp_l[i] - ref_l[i]));
    CHECK(jump > limit, "a hard cut of four voices changes the output by only %g, below the click limit %g, so the test could not see a click",
          (double)jump, (double)limit);
    printf("clicks: control run, cutting four voices dead moves the output by %.4f, which the limit %.4f catches\n", (double)jump, (double)limit);
    (void)ref_r; (void)tmp_r;
    free(q); free(p); free(l); free(r);
}

static void test_knobs(void)
{
    const float sr = 44100.f;
    const int note = 57;
    const double f0 = phase_note_hz(note);
    const char *names[4] = {"A", "B", "R", "W"};
    for (int k = 0; k < 4; k++) {
        float lo[4] = {PHASE_DEFAULT_A, 0, 0, 0}, hi[4] = {PHASE_DEFAULT_A, 0, 0, 0};  /* the factory sound, one knob moved */
        lo[k] = 0.0f; hi[k] = 0.9f;
        static double pa[FFT_N / 2 + 1], pb[FFT_N / 2 + 1];
        float *x = play(knobs(lo[0], lo[1], lo[2], lo[3]), &note, 1, 1.0f, sr, 20000 + FFT_N, NULL);
        power_spectrum(x + 20000, pa);
        free(x);
        x = play(knobs(hi[0], hi[1], hi[2], hi[3]), &note, 1, 1.0f, sr, 20000 + FFT_N, NULL);
        power_spectrum(x + 20000, pb);
        free(x);
        double acc = 0;
        int cnt = 0;
        for (int h = 1; h <= 30; h++) {
            const double da = harmonic_db(pa, f0, sr, h), db = harmonic_db(pb, f0, sr, h);
            if (da < -90 && db < -90) continue;
            acc += (da - db) * (da - db);
            cnt++;
        }
        const double rmsdiff = sqrt(acc / (cnt ? cnt : 1));
        const double ca = centroid_hz(pa, sr), cb = centroid_hz(pb, sr);
        CHECK(rmsdiff > 3.0, "knob %s: harmonic spectrum differs by only %.2f dB RMS between 0 and 0.9", names[k], rmsdiff);
        if (k >= 1)
            CHECK(cb > ca, "knob %s: expected a brighter spectrum at 0.9 (centroid %.0f -> %.0f Hz)", names[k], ca, cb);
        printf("knob %s: 0 -> 0.9 from the factory sound changes the first 30 harmonics by %.1f dB RMS, spectral centroid %.0f -> %.0f Hz\n",
               names[k], rmsdiff, ca, cb);
    }

    /* envelope: attack */
    {
        float tt[2];
        const float atk[2] = {0.002f, 0.4f};
        for (int i = 0; i < 2; i++) {
            phase_params prm = knobs(0, 0, 0, 0);
            prm.attack = atk[i];
            float *x = play(prm, &note, 1, 1.0f, sr, 44100, NULL);
            float plateau = peak_of(x + 30000, 4000);
            int t = 0;
            for (; t + 200 < 44100; t += 50)
                if (peak_of(x + t, 200) > 0.9f * plateau) break;
            tt[i] = (float)t / sr;
            free(x);
        }
        CHECK(tt[1] > tt[0] + 0.2f, "attack knob: time to 90%% %g s vs %g s", (double)tt[0], (double)tt[1]);
        printf("attack: 2 ms setting reaches 90%% at %.3f s, 400 ms setting at %.3f s\n", (double)tt[0], (double)tt[1]);
    }
    /* envelope: decay and sustain */
    {
        /* a sine at sustain 1 is the reference: same phase, so the peak ratio is the envelope value */
        float lev[4];
        const float dec[4] = {0.05f, 1.5f, 0.05f, 0.05f};
        const float sus[4] = {0.2f, 0.2f, 0.8f, 0.2f};
        const int at[4] = {13230, 13230, 13230, 88200};  /* 0.3 s, and 2 s */
        phase_params ref = knobs(0, 0, 0, 0);
        float *xr = play(ref, &note, 1, 1.0f, sr, 3 * 44100, NULL);
        for (int i = 0; i < 4; i++) {
            phase_params prm = knobs(0, 0, 0, 0);
            prm.decay = dec[i]; prm.sustain = sus[i];
            float *x = play(prm, &note, 1, 1.0f, sr, 3 * 44100, NULL);
            lev[i] = peak_of(x + at[i], 800) / peak_of(xr + at[i], 800);
            free(x);
        }
        free(xr);
        CHECK(lev[1] > lev[0] + 0.2f, "decay knob: level at 0.3 s %g (fast decay) vs %g (slow decay)", (double)lev[0], (double)lev[1]);
        CHECK(fabsf(lev[3] - 0.2f) < 0.03f && fabsf(lev[2] - 0.8f) < 0.05f, "sustain knob: levels %g and %g, expected 0.2 and 0.8", (double)lev[3], (double)lev[2]);
        printf("decay and sustain: at 0.3 s the level is %.3f with decay 0.05 s and %.3f with decay 1.5 s (sustain 0.2); held at 0.3 s with sustain 0.8: %.3f; at 2 s with sustain 0.2: %.3f\n",
               (double)lev[0], (double)lev[1], (double)lev[2], (double)lev[3]);
    }
    /* master level */
    {
        phase_params p1 = knobs(0, 0, 0, 0), p2 = p1;
        p1.level = 0.4f; p2.level = 0.8f;
        float *x1 = play(p1, &note, 1, 1.0f, sr, 20000, NULL), *x2 = play(p2, &note, 1, 1.0f, sr, 20000, NULL);
        const float ratio = rms_of(x1 + 8000, 8000) / rms_of(x2 + 8000, 8000);
        CHECK(fabsf(ratio - 0.5f) < 0.01f, "level 0.4 / 0.8 gave ratio %g", (double)ratio);
        printf("level: 0.4 vs 0.8 gives amplitude ratio %.4f\n", (double)ratio);
        free(x1); free(x2);
    }
    /* velocity and mod wheel */
    {
        float *x1 = play(knobs(0.3f, 0, 0, 0), &note, 1, 0.3f, sr, 20000, NULL), *x2 = play(knobs(0.3f, 0, 0, 0), &note, 1, 0.9f, sr, 20000, NULL);
        const float ratio = rms_of(x1 + 8000, 8000) / rms_of(x2 + 8000, 8000);
        CHECK(fabsf(ratio - 1.0f / 3.0f) < 0.01f, "velocity ratio %g", (double)ratio);
        free(x1); free(x2);
        phase_t *p = malloc(sizeof *p);
        float *l = malloc(sizeof(float) * (20000 + FFT_N)), *r = malloc(sizeof(float) * (20000 + FFT_N));
        static double p0[FFT_N / 2 + 1], p1[FFT_N / 2 + 1];
        ev_t ev[3];
        memset(ev, 0, sizeof ev);
        ev[0].kind = EV_PARAMS; ev[0].prm = knobs(0.1f, 0, 0, 0);
        ev[1].kind = EV_ON; ev[1].note = note; ev[1].val = 1.0f;
        ev[2].kind = EV_MOD; ev[2].val = 0.0f;
        phase_init(p, sr);
        simulate(p, ev, 3, l, r, 20000 + FFT_N, 128);
        power_spectrum(l + 20000, p0);
        ev[2].val = 1.0f;
        phase_init(p, sr);
        simulate(p, ev, 3, l, r, 20000 + FFT_N, 128);
        power_spectrum(l + 20000, p1);
        CHECK(centroid_hz(p1, sr) > 1.1 * centroid_hz(p0, sr), "mod wheel should brighten: %g -> %g Hz", centroid_hz(p0, sr), centroid_hz(p1, sr));
        printf("mod wheel: spectral centroid %.0f -> %.0f Hz at full wheel\n", centroid_hz(p0, sr), centroid_hz(p1, sr));
        free(p); free(l); free(r);
    }
}

static void test_dc_and_alias(void)
{
    const float sr = 44100.f;
    /* DC: asymmetric bends must not leave an offset */
    const int n1 = 52;
    float *x = play(knobs(0.9f, 0.0f, 0.0f, 0.0f), &n1, 1, 1.0f, sr, 44100, NULL);
    double mean = 0;
    for (int i = 22050; i < 44100; i++) mean += x[i];
    mean /= 22050.0;
    CHECK(fabs(mean) < 0.003, "DC offset %g after the blocker", mean);
    printf("dc: knob A at 0.9, mean of the held note %.5f (rms %.3f)\n", mean, (double)rms_of(x + 22050, 22050));
    free(x);

    /* aliasing: power more than 6 bins from any harmonic of f0, against total power */
    const int notes[] = {60, 72, 84, 96};  /* up to 2.1 kHz; above that the bandlimiting trades the bends away, see docs/DSP.md */
    const float sets[][4] = {{0.2442f, 0, 0, 0}, {1, 1, 0, 0}, {0.5f, 0.5f, 1, 0.5f}, {1, 1, 1, 1}};
    for (size_t s = 0; s < 4; s++) {
        double worst = -999;
        for (size_t k = 0; k < 4; k++) {
            static double pw[FFT_N / 2 + 1];
            const double f0 = phase_note_hz(notes[k]);
            float *y = play(knobs(sets[s][0], sets[s][1], sets[s][2], sets[s][3]), &notes[k], 1, 1.0f, sr, 20000 + FFT_N, NULL);
            power_spectrum(y + 20000, pw);
            free(y);
            double tot = 0, off = 0;
            for (int b = 4; b <= FFT_N / 2; b++) {
                const double f = b * sr / FFT_N;
                const double h = f / f0;
                const double dist = fabs(h - floor(h + 0.5)) * f0 / sr * FFT_N;  /* distance to the nearest harmonic, in bins */
                tot += pw[b];
                if (dist > 6.0) off += pw[b];
            }
            const double db = 10.0 * log10(off / tot + 1e-30);
            if (db > worst) worst = db;
        }
        printf("alias: knobs a=%.2f b=%.2f r=%.2f w=%.2f, worst non-harmonic power over notes 60..96: %.1f dB below total\n",
               (double)sets[s][0], (double)sets[s][1], (double)sets[s][2], (double)sets[s][3], -worst);
        CHECK(worst < -40.0, "alias level %.1f dB too high for set %zu", worst, s);
    }
}

static void test_determinism_and_blocks(void)
{
    const int n = 2 * 44100;
    phase_t *p = malloc(sizeof *p);
    float *ref_l = malloc(sizeof(float) * n), *ref_r = malloc(sizeof(float) * n);
    float *l = malloc(sizeof(float) * n), *r = malloc(sizeof(float) * n);
    ev_t ev[64];
    int ne = 0;
    memset(ev, 0, sizeof ev);
    ev[ne].kind = EV_PARAMS; ev[ne].prm = knobs(0.4f, 0.2f, 0.3f, 0.1f); ne++;
    ev[ne].at = 0; ev[ne].kind = EV_ON; ev[ne].note = 48; ev[ne].val = 0.8f; ne++;
    ev[ne].at = 3000; ev[ne].kind = EV_ON; ev[ne].note = 55; ev[ne].val = 0.6f; ne++;
    ev[ne].at = 9000; ev[ne].kind = EV_PARAMS; ev[ne].prm = knobs(0.9f, 0.7f, 0.8f, 0.5f); ev[ne].prm.release = 0.5f; ne++;
    ev[ne].at = 12345; ev[ne].kind = EV_BEND; ev[ne].val = 0.7f; ne++;
    ev[ne].at = 20001; ev[ne].kind = EV_MOD; ev[ne].val = 0.6f; ne++;
    ev[ne].at = 30000; ev[ne].kind = EV_OFF; ev[ne].note = 48; ne++;
    ev[ne].at = 31111; ev[ne].kind = EV_ON; ev[ne].note = 72; ev[ne].val = 1.0f; ne++;
    ev[ne].at = 50000; ev[ne].kind = EV_PEDAL; ev[ne].note = 1; ne++;
    ev[ne].at = 52000; ev[ne].kind = EV_OFF; ev[ne].note = 55; ne++;
    ev[ne].at = 70000; ev[ne].kind = EV_PEDAL; ev[ne].note = 0; ne++;
    phase_init(p, 44100.f);
    simulate(p, ev, ne, ref_l, ref_r, n, 128);

    phase_init(p, 44100.f);
    simulate(p, ev, ne, l, r, n, 128);
    CHECK(!memcmp(l, ref_l, sizeof(float) * n) && !memcmp(r, ref_r, sizeof(float) * n), "two fresh instances differ");

    {
        phase_params d;
        phase_default_params(&d);
        phase_set_params(p, &d);  /* a reset instance starts from the controls it was given, so give it the same ones a fresh one has */
        phase_reset(p);
    }
    simulate(p, ev, ne, l, r, n, 128);
    CHECK(!memcmp(l, ref_l, sizeof(float) * n) && !memcmp(r, ref_r, sizeof(float) * n), "output after phase_reset differs from a fresh instance");

    const int blocks[] = {1, 7, 64, 100, 127, 480, 4096};
    for (size_t i = 0; i < sizeof blocks / sizeof blocks[0]; i++) {
        phase_init(p, 44100.f);
        simulate(p, ev, ne, l, r, n, blocks[i]);
        const int same = !memcmp(l, ref_l, sizeof(float) * n) && !memcmp(r, ref_r, sizeof(float) * n);
        CHECK(same, "block size %d gives different output from 128", blocks[i]);
    }
    printf("determinism: fresh instance, reset instance and block sizes 1, 7, 64, 100, 127, 480, 4096 all bit-identical to block 128\n");
    free(p); free(ref_l); free(ref_r); free(l); free(r);
}

static void test_parameter_sweeps(void)
{
    /* Moving every knob across its whole range every 128 samples must stay as smooth as the roughest static wave. */
    const float sr = 44100.f;
    const int note = 40;
    float steady = 0;
    const float corners[][4] = {{0, 0, 0, 0}, {1, 1, 1, 1}, {1, 0, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}, {0.5f, 0.5f, 0.5f, 0.5f}};
    for (size_t c = 0; c < sizeof corners / sizeof corners[0]; c++) {
        float *x = play(knobs(corners[c][0], corners[c][1], corners[c][2], corners[c][3]), &note, 1, 1.0f, sr, 30000, NULL);
        steady = fmaxf(steady, max_step(x + 10000, 20000));
        free(x);
    }
    phase_t *p = malloc(sizeof *p);
    const int n = 4 * 44100;
    float *l = malloc(sizeof(float) * n), *r = malloc(sizeof(float) * n);
    ev_t *ev = calloc(2000, sizeof *ev);
    int ne = 0;
    ev[ne].kind = EV_PARAMS; ev[ne].prm = base_params(); ne++;
    ev[ne].kind = EV_ON; ev[ne].note = note; ev[ne].val = 1.0f; ne++;
    for (int at = 5000; at < n - 128 && ne < 1990; at += 1024) {
        ev[ne].at = at; ev[ne].kind = EV_PARAMS;
        const float v = ((at / 1024) % 2) ? 1.0f : 0.0f;
        ev[ne].prm = knobs(v, 1.0f - v, v, 1.0f - v);
        ne++;
    }
    phase_init(p, sr);
    simulate(p, ev, ne, l, r, n, 128);
    const float swept = max_step(l + 6000, n - 6000);
    CHECK(swept <= 1.5f * steady, "knob sweep step %g above 1.5x the roughest static wave %g", (double)swept, (double)steady);
    CHECK(all_finite_bounded(l, n, 1.0f), "knob sweep not finite or above 1.0");
    printf("sweep: all four knobs flipped 0<->1 every 1024 samples, max step %.4f vs roughest static wave %.4f (limit 1.5x)\n", (double)swept, (double)steady);
    free(p); free(l); free(r); free(ev);
}

static void test_fuzz(void)
{
    const float srs[] = {22050.f, 44100.f, 96000.f, 192000.f};
    for (int s = 0; s < 4; s++) {
        phase_t *p = malloc(sizeof *p);
        const int n = (int)(8 * srs[s]);
        float *l = malloc(sizeof(float) * n), *r = malloc(sizeof(float) * n);
        ev_t *ev = calloc(3000, sizeof *ev);
        int ne = 0;
        for (int at = 0; at < n - 10 && ne < 2990; at += 40 + (int)(urand() * 4000)) {
            ev[ne].at = at;
            const float u = urand();
            if (u < 0.35f) { ev[ne].kind = EV_ON; ev[ne].note = (int)(urand() * 127.99f); ev[ne].val = urand(); }
            else if (u < 0.6f) { ev[ne].kind = EV_OFF; ev[ne].note = (int)(urand() * 127.99f); }
            else if (u < 0.8f) {
                ev[ne].kind = EV_PARAMS;
                ev[ne].prm.a = urand(); ev[ne].prm.b = urand(); ev[ne].prm.r = urand(); ev[ne].prm.w = urand();
                ev[ne].prm.attack = 0.001f + urand() * urand() * 2; ev[ne].prm.decay = 0.005f + urand() * urand() * 2;
                ev[ne].prm.sustain = urand(); ev[ne].prm.release = 0.005f + urand() * urand() * 3;
                ev[ne].prm.level = urand(); ev[ne].prm.bend_range = urand() * 12;
            }
            else if (u < 0.88f) { ev[ne].kind = EV_BEND; ev[ne].val = frand(); }
            else if (u < 0.93f) { ev[ne].kind = EV_MOD; ev[ne].val = urand(); }
            else if (u < 0.98f) { ev[ne].kind = EV_PEDAL; ev[ne].note = urand() < 0.5f; }
            else ev[ne].kind = EV_ALLOFF;
            ne++;
        }
        phase_init(p, srs[s]);
        simulate(p, ev, ne, l, r, n, 37 + s * 91);
        CHECK(all_finite_bounded(l, n, 1.0f) && all_finite_bounded(r, n, 1.0f), "fuzz at %g Hz: NaN, Inf or sample above 1.0", (double)srs[s]);
        printf("fuzz: %g Hz, 8 s, %d random events, output finite and within +-1 (peak %.3f)\n", (double)srs[s], ne, (double)fmaxf(peak_of(l, n), peak_of(r, n)));
        free(p); free(l); free(r); free(ev);
    }
}

static void test_denormals(void)
{
    /* after everything has released, the core must produce exact zeros or tiny normal numbers, never denormals */
    phase_t *p = malloc(sizeof *p);
    const int n = 6 * 44100;
    float *l = malloc(sizeof(float) * n), *r = malloc(sizeof(float) * n);
    ev_t ev[4];
    memset(ev, 0, sizeof ev);
    ev[0].kind = EV_PARAMS; ev[0].prm = knobs(0.9f, 0.1f, 0.5f, 0.5f); ev[0].prm.release = 0.1f;
    ev[1].kind = EV_ON; ev[1].note = 60; ev[1].val = 1.0f;
    ev[2].at = 20000; ev[2].kind = EV_OFF; ev[2].note = 60;
    ev[3].at = 40000; ev[3].kind = EV_PARAMS; ev[3].prm = knobs(0, 0, 0, 0);
    phase_init(p, 44100.f);
    simulate(p, ev, 4, l, r, n, 128);
    int denorm = 0;
    for (int i = 0; i < n; i++)
        if (fpclassify(l[i]) == FP_SUBNORMAL || fpclassify(r[i]) == FP_SUBNORMAL) denorm++;
    CHECK(denorm == 0, "%d subnormal output samples after release", denorm);
    CHECK(fpclassify(p->s_a) != FP_SUBNORMAL && fpclassify(p->s_b) != FP_SUBNORMAL && p->s_a == 0.0f, "smoothed knob did not snap to its target");
    printf("denormals: %d subnormal samples in 6 s including a long release tail and knobs smoothed to 0\n", denorm);
    free(p); free(l); free(r);
}

static void test_stereo_and_mono_sum(void)
{
    const int notes[2] = {48, 60};
    float *r;
    float *l = play(knobs(0.3f, 0, 0, 0), notes, 2, 1.0f, 44100.f, 22050, &r);
    float diff = 0;
    for (int i = 5000; i < 22050; i++) diff = fmaxf(diff, fabsf(l[i] - r[i]));
    CHECK(diff > 0.005f, "left and right should differ a little (voice spread), diff %g", (double)diff);
    CHECK(rms_of(l + 5000, 10000) > 0.02f && rms_of(r + 5000, 10000) > 0.02f, "both channels should carry the notes");
    printf("stereo: two notes spread across the pair, max L-R difference %.3f\n", (double)diff);
    free(l); free(r);
}

int main(void)
{
    test_shape();
    test_pitch();
    test_poly_and_headroom();
    test_release();
    test_pedal_and_all_off();
    test_clicks();
    test_knobs();
    test_dc_and_alias();
    test_determinism_and_blocks();
    test_parameter_sweeps();
    test_fuzz();
    test_denormals();
    test_stereo_and_mono_sum();
    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("ALL PASSED\n");
    return 0;
}
