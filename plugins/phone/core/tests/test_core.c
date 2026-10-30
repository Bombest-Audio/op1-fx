/* Behavioral tests for the Phone core. Build and run with core/tests/run.sh */
#include "../phone_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TWO_PI 6.28318530717958647f

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

static unsigned rng_state = 12345u;
static float frand(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) / (float)(1u << 24) * 2.0f - 1.0f;
}

/* The core is about 1.2 MB, so keep instances out of the stack. */
static phone_t g_a, g_b;

static phone_params make_params(float tone, int phonic, int baud, float mix)
{
    phone_params q = {tone, phonic, baud, mix};
    return q;
}

/* Tone value for a given playback rate (inverse of 16 v^2). */
static float tone_for_ratio(float ratio)
{
    return sqrtf(ratio / 16.0f);
}

static void fill_sine(float *x, int n, float sr, float hz, float amp)
{
    for (int i = 0; i < n; i++)
        x[i] = amp * sinf(TWO_PI * hz * (float)i / sr);
}

static float peak(const float *x, int n)
{
    float p = 0;
    for (int i = 0; i < n; i++)
        p = fmaxf(p, fabsf(x[i]));
    return p;
}

static float rms(const float *x, int n)
{
    double s = 0;
    for (int i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return (float)sqrt(s / n);
}

static int all_finite(const float *x, int n)
{
    for (int i = 0; i < n; i++)
        if (!isfinite(x[i]))
            return 0;
    return 1;
}

static int has_subnormal(const float *x, int n)
{
    for (int i = 0; i < n; i++)
        if (fpclassify(x[i]) == FP_SUBNORMAL)
            return 1;
    return 0;
}

/* Run in blocks of `block` frames. The core must give the same output for any block size. */
static void run(phone_t *p, const phone_params *prm, float *l, float *r, int n, int block)
{
    phone_set_params(p, prm);
    for (int i = 0; i < n; i += block) {
        int c = n - i < block ? n - i : block;
        phone_process(p, l + i, r + i, c);
    }
}

/* Fundamental of x[from..to) from rising zero crossings, with linear interpolation of each crossing. 0 if too few. */
static float estimate_hz(const float *x, int from, int to, float sr)
{
    int count = 0;
    double first = 0, last = 0;
    int armed = 0;
    for (int i = from + 1; i < to; i++) {
        if (x[i] < -0.02f)
            armed = 1;
        if (armed && x[i - 1] <= 0.0f && x[i] > 0.0f) {
            double t = (double)i - 1.0 + (double)(-x[i - 1]) / (double)(x[i] - x[i - 1]);
            if (count == 0)
                first = t;
            last = t;
            count++;
            armed = 0;
        }
    }
    if (count < 8)
        return 0.0f;
    return (float)((double)(count - 1) / (last - first) * (double)sr);
}

static void test_tone_mapping(void)
{
    CHECK(fabsf(phone_tone_ratio(0.25f) - 1.0f) < 1e-6f, "unity at 0.25, got %g", (double)phone_tone_ratio(0.25f));
    CHECK(fabsf(phone_tone_ratio(8000.0f / 32767.0f) - 0.9537f) < 0.001f, "default ratio %g",
          (double)phone_tone_ratio(8000.0f / 32767.0f));
    CHECK(phone_tone_ratio(1.0f) == 16.0f, "top is x16");
    CHECK(phone_tone_ratio(0.0f) == 1.0f / 16.0f, "bottom clamps to 1/16");
    CHECK(fabsf(phone_tone_semitones(0.5f) - 24.0f) < 1e-4f, "0.5 is two octaves up, got %g",
          (double)phone_tone_semitones(0.5f));
    float prev = -1000.0f;
    for (int i = 0; i <= 100; i++) {
        float s = phone_tone_semitones((float)i / 100.0f);
        CHECK(s >= prev, "tone must not go down as the knob goes up");
        prev = s;
    }
}

static void test_table(void)
{
    phone_init(&g_a, 44100.0f);
    for (int i = 0; i <= PHONE_PHASES; i++) {
        float sum = 0;
        for (int j = 0; j < PHONE_TAPS; j++)
            sum += g_a.table[i][j];
        CHECK(fabsf(sum - 1.0f) < 1e-5f, "row %d sums to %g", i, (double)sum);
    }
    for (int j = 0; j < PHONE_TAPS; j++) {
        CHECK(fabsf(g_a.table[0][j] - (j == 3 ? 1.0f : 0.0f)) < 1e-6f, "row 0 tap %d", j);
        CHECK(fabsf(g_a.table[PHONE_PHASES][j] - (j == 4 ? 1.0f : 0.0f)) < 1e-5f, "last row tap %d", j);
    }
}

static void test_bypass_bit_exact(void)
{
    enum { N = 8192 };
    static float l[N], r[N], l0[N], r0[N];
    phone_init(&g_a, 44100.0f);
    phone_params prm = make_params(0.3f, 4, 5, 0.0f);
    phone_set_params(&g_a, &prm);
    phone_reset(&g_a);
    for (int i = 0; i < N; i++) {
        l0[i] = l[i] = frand();
        r0[i] = r[i] = frand();
    }
    run(&g_a, &prm, l, r, N, 100);
    CHECK(memcmp(l, l0, sizeof l) == 0 && memcmp(r, r0, sizeof r) == 0, "TELEMATIC 0 must leave the buffers bit-identical");

    /* after a fade down from a wet setting the bypass is exact again */
    static float wl[44100], wr[44100];
    prm = make_params(0.3f, 4, 5, 0.8f);
    for (int i = 0; i < 44100; i++) {
        wl[i] = 0.4f * sinf(TWO_PI * 200.0f * (float)i / 44100.0f);
        wr[i] = wl[i];
    }
    run(&g_a, &prm, wl, wr, 44100, 64);
    prm.telematic = 0.0f;
    run(&g_a, &prm, wl, wr, 44100, 64); /* fade, 1 s */
    for (int i = 0; i < N; i++) {
        l0[i] = l[i] = frand();
        r0[i] = r[i] = frand();
    }
    run(&g_a, &prm, l, r, N, 100);
    CHECK(memcmp(l, l0, sizeof l) == 0 && memcmp(r, r0, sizeof r) == 0, "bypass must be exact after a fade to 0");
}

/* The loop plays back at TONE's rate, so a looped sine comes out at input pitch times ratio. */
static void test_pitch_follows_tone(void)
{
    const float sr = 44100.0f;
    enum { N = 44100 * 2 };
    static float l[N], r[N];
    const float ratios[] = {0.5f, 1.0f, 2.0f, 4.0f};
    for (int k = 0; k < 4; k++) {
        fill_sine(l, N, sr, 220.0f, 0.5f);
        memcpy(r, l, sizeof l);
        phone_init(&g_a, sr);
        phone_params prm = make_params(tone_for_ratio(ratios[k]), 3, 2, 1.0f);
        run(&g_a, &prm, l, r, N, 128);
        float hz = estimate_hz(l, 22050, N, sr);
        float want = 220.0f * ratios[k];
        CHECK(fabsf(hz - want) < 0.02f * want, "ratio %g: want %g Hz, got %g Hz", (double)ratios[k], (double)want, (double)hz);
    }
}

static void test_pitch_across_sample_rates(void)
{
    const float rates[] = {22050.0f, 32000.0f, 48000.0f, 96000.0f, 192000.0f};
    for (int k = 0; k < 5; k++) {
        const float sr = rates[k];
        const int n = (int)(sr * 1.5f);
        float *l = malloc(sizeof(float) * (size_t)n);
        float *r = malloc(sizeof(float) * (size_t)n);
        fill_sine(l, n, sr, 220.0f, 0.5f);
        memcpy(r, l, sizeof(float) * (size_t)n);
        phone_init(&g_a, sr);
        phone_params prm = make_params(tone_for_ratio(2.0f), 3, 2, 1.0f);
        run(&g_a, &prm, l, r, n, 256);
        float hz = estimate_hz(l, n / 3, n, sr);
        CHECK(fabsf(hz - 440.0f) < 8.8f, "sr %g: want 440 Hz, got %g Hz", (double)sr, (double)hz);
        float rm = rms(l + n / 2, n / 2);
        CHECK(rm > 0.2f && rm < 0.5f, "sr %g: wet level %g", (double)sr, (double)rm);
        free(l);
        free(r);
    }
}

/* At unity rate and full mix a loud tone keeps its level. */
static void test_level_at_unity(void)
{
    const float sr = 44100.0f;
    enum { N = 44100 };
    static float l[N], r[N];
    const float freqs[] = {100.0f, 440.0f, 1000.0f, 4000.0f};
    for (int k = 0; k < 4; k++) {
        fill_sine(l, N, sr, freqs[k], 0.5f);
        memcpy(r, l, sizeof l);
        phone_init(&g_a, sr);
        phone_params prm = make_params(0.25f, 3, 1, 1.0f);
        run(&g_a, &prm, l, r, N, 128);
        float in_rms = 0.5f * 0.70710678f;
        float db = 20.0f * log10f(rms(l + N / 2, N / 2) / in_rms);
        CHECK(fabsf(db) < 1.5f, "%g Hz: level change %g dB", (double)freqs[k], (double)db);
    }
}

/* PHONIC sets how many cycles go into a fragment and BAUD how often a fragment plays before the next one is taken. */
static void test_fragment_and_repeat_rates(void)
{
    const float sr = 44100.0f;
    enum { N = 44100 * 3 };
    static float l[N], r[N];
    struct {
        int phonic, baud;
        float want_per_sec;
    } cases[] = {
        {3, 1, 100.0f / 3.0f}, /* one fragment per 3 cycles of 100 Hz */
        {3, 4, 44100.0f / (4.0f * 1323.0f)}, /* each fragment plays 4 times: 4 x 1323 samples */
        {1, 1, 100.0f},
        {5, 1, 20.0f},
    };
    for (int k = 0; k < 4; k++) {
        fill_sine(l, N, sr, 100.0f, 0.5f);
        memcpy(r, l, sizeof l);
        phone_init(&g_a, sr);
        phone_params prm = make_params(0.25f, cases[k].phonic, cases[k].baud, 1.0f);
        phone_set_params(&g_a, &prm);
        phone_reset(&g_a);
        int swaps = 0;
        for (int i = 0; i < N; i += 128) {
            phone_process(&g_a, l + i, r + i, 128 < N - i ? 128 : N - i);
            if (i >= 22050)
                swaps += phone_take_swaps(&g_a);
            else
                phone_take_swaps(&g_a);
        }
        float per_sec = (float)swaps / ((float)(N - 22050) / sr);
        CHECK(fabsf(per_sec - cases[k].want_per_sec) < 0.1f * cases[k].want_per_sec + 1.0f,
              "phonic %d baud %d: want %g swaps/s, got %g", cases[k].phonic, cases[k].baud,
              (double)cases[k].want_per_sec, (double)per_sec);
    }
}

/* The loop is gated by the input level: when the input stops the wet signal dies out. */
static void test_gate_after_input_stops(void)
{
    const float sr = 44100.0f;
    enum { N = 44100 * 2 };
    static float l[N], r[N];
    memset(l, 0, sizeof l);
    fill_sine(l, 44100, sr, 220.0f, 0.5f);
    memcpy(r, l, sizeof l);
    phone_init(&g_a, sr);
    phone_params prm = make_params(PHONE_DEFAULT_TONE, 3, 7, 1.0f);
    run(&g_a, &prm, l, r, N, 128);
    float during = rms(l + 22050, 20000);
    float after = peak(l + 44100 + 22050, 22050);
    CHECK(during > 0.2f, "wet level while the input plays: %g", (double)during);
    CHECK(after < 0.001f, "wet must die out after the input stops, peak %g", (double)after);
}

/* A step has no crossings, so nothing is looped and only the dry part remains. */
static void test_step_response(void)
{
    const float sr = 44100.0f;
    enum { N = 44100 * 2 };
    static float l[N], r[N];
    for (int i = 0; i < N; i++)
        l[i] = r[i] = 0.5f;
    phone_init(&g_a, sr);
    phone_params prm = make_params(0.3f, 3, 7, 0.4f);
    run(&g_a, &prm, l, r, N, 128);
    CHECK(all_finite(l, N), "step response must be finite");
    CHECK(fabsf(l[N - 1] - 0.3f) < 0.003f, "step settles at the dry share, got %g", (double)l[N - 1]);
}

static void test_impulse_response(void)
{
    const float sr = 44100.0f;
    enum { N = 44100 * 2 };
    static float l[N], r[N];
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    l[1000] = r[1000] = 0.5f;
    phone_init(&g_a, sr);
    phone_params prm = make_params(PHONE_DEFAULT_TONE, 3, 7, 0.5f);
    run(&g_a, &prm, l, r, N, 128);
    CHECK(all_finite(l, N), "impulse response must be finite");
    CHECK(fabsf(l[1000] - 0.25f) < 0.01f, "the dry share of the impulse passes at its own sample, got %g", (double)l[1000]);
    CHECK(peak(l + 44100, 44100) < 1e-4f, "impulse response must die out, tail peak %g", (double)peak(l + 44100, 44100));
}

/* Noise, full-scale sines, every corner of the controls, several sample rates: finite and never above 1.0. */
static void test_stability_and_headroom(void)
{
    const float rates[] = {22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f};
    const float tones[] = {0.0f, 0.1f, 0.25f, 0.6f, 1.0f};
    const int cyc[] = {1, 15};
    const int rep[] = {1, 31};
    for (int ri = 0; ri < 5; ri++) {
        const float sr = rates[ri];
        const int n = (int)(sr * 0.6f);
        float *l = malloc(sizeof(float) * (size_t)n);
        float *r = malloc(sizeof(float) * (size_t)n);
        for (int ti = 0; ti < 5; ti++)
            for (int ci = 0; ci < 2; ci++)
                for (int bi = 0; bi < 2; bi++)
                    for (int src = 0; src < 2; src++) {
                        if (src == 0) {
                            for (int i = 0; i < n; i++) {
                                l[i] = frand();
                                r[i] = frand();
                            }
                        } else {
                            fill_sine(l, n, sr, 180.0f, 1.0f);
                            fill_sine(r, n, sr, 3000.0f, 1.0f);
                        }
                        phone_init(&g_a, sr);
                        phone_params prm = make_params(tones[ti], cyc[ci], rep[bi], 1.0f);
                        run(&g_a, &prm, l, r, n, 64);
                        CHECK(all_finite(l, n) && all_finite(r, n), "sr %g tone %g: non-finite output", (double)sr,
                              (double)tones[ti]);
                        float pk = fmaxf(peak(l, n), peak(r, n));
                        CHECK(pk <= 1.0001f, "sr %g tone %g cyc %d rep %d: peak %g", (double)sr, (double)tones[ti],
                              cyc[ci], rep[bi], (double)pk);
                    }
        free(l);
        free(r);
    }
}

/* Same output for any block size, down to 1 sample per call. */
static void test_block_size_independence(void)
{
    const float sr = 44100.0f;
    enum { N = 20000 };
    static float al[N], ar[N], bl[N], br[N];
    const int blocks[] = {1, 7, 64, 1000};
    float refl[N], refr[N];
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < N; i++) {
            al[i] = 0.5f * sinf(TWO_PI * 180.0f * (float)i / sr) + 0.1f * sinf(TWO_PI * 1300.0f * (float)i / sr);
            ar[i] = 0.4f * sinf(TWO_PI * 180.0f * (float)i / sr);
        }
        phone_init(&g_a, sr);
        phone_params prm = make_params(0.4f, 2, 3, 0.7f);
        run(&g_a, &prm, al, ar, N, blocks[k]);
        if (k == 0) {
            memcpy(refl, al, sizeof al);
            memcpy(refr, ar, sizeof ar);
        } else {
            CHECK(memcmp(refl, al, sizeof al) == 0 && memcmp(refr, ar, sizeof ar) == 0,
                  "block size %d gives different output than block size 1", blocks[k]);
        }
    }
    (void)bl;
    (void)br;
}

static void test_reset_clears_state(void)
{
    const float sr = 44100.0f;
    enum { N = 30000 };
    static float al[N], ar[N], bl[N], br[N], cl[N], cr[N];
    for (int i = 0; i < N; i++) {
        al[i] = bl[i] = cl[i] = 0.5f * sinf(TWO_PI * 250.0f * (float)i / sr);
        ar[i] = br[i] = cr[i] = 0.3f * sinf(TWO_PI * 90.0f * (float)i / sr);
    }
    phone_init(&g_a, sr);
    phone_params prm = make_params(0.35f, 3, 5, 0.9f);
    phone_set_params(&g_a, &prm);
    phone_reset(&g_a);
    run(&g_a, &prm, al, ar, N, 128);
    /* dirty the state with different material, then reset and run the same input again */
    static float nl[N], nr[N];
    for (int i = 0; i < N; i++) {
        nl[i] = frand();
        nr[i] = frand();
    }
    phone_params other = make_params(0.9f, 9, 20, 1.0f);
    run(&g_a, &other, nl, nr, N, 128);
    phone_set_params(&g_a, &prm);
    phone_reset(&g_a);
    run(&g_a, &prm, bl, br, N, 128);
    CHECK(memcmp(al, bl, sizeof al) == 0 && memcmp(ar, br, sizeof ar) == 0, "reset must give the same output as a fresh instance");
    phone_init(&g_b, sr);
    phone_set_params(&g_b, &prm);
    phone_reset(&g_b);
    run(&g_b, &prm, cl, cr, N, 128);
    CHECK(memcmp(al, cl, sizeof al) == 0, "fresh instances must agree");
}

/* Identical channels stay identical. Different channels stay different. */
static void test_mono_and_stereo(void)
{
    const float sr = 44100.0f;
    enum { N = 30000 };
    static float l[N], r[N];
    fill_sine(l, N, sr, 220.0f, 0.5f);
    memcpy(r, l, sizeof l);
    phone_init(&g_a, sr);
    phone_params prm = make_params(0.3f, 3, 4, 1.0f);
    run(&g_a, &prm, l, r, N, 128);
    CHECK(memcmp(l, r, sizeof l) == 0, "identical channels must give identical outputs");
    CHECK(peak(l, N) > 0.1f, "mono path must produce output");

    fill_sine(l, N, sr, 220.0f, 0.5f);
    fill_sine(r, N, sr, 220.0f, 0.1f);
    phone_init(&g_a, sr);
    run(&g_a, &prm, l, r, N, 128);
    float pl = peak(l + 15000, 15000), pr = peak(r + 15000, 15000);
    CHECK(pl > 3.0f * pr, "stereo image must survive: left peak %g, right peak %g", (double)pl, (double)pr);
}

/* A slow change of every control must not click. Max step of a 220 Hz sine at 0.5 is 0.016 per sample. */
static void test_no_clicks_on_control_changes(void)
{
    const float sr = 44100.0f;
    enum { N = 44100 * 3 };
    static float l[N], r[N];
    fill_sine(l, N, sr, 220.0f, 0.5f);
    memcpy(r, l, sizeof l);
    phone_init(&g_a, sr);
    for (int i = 0; i < N; i += 128) {
        int blk = i / 128;
        phone_params prm = make_params(0.15f + 0.2f * (float)(blk % 7) / 6.0f, 1 + blk % 5, 1 + blk % 4, 1.0f);
        phone_set_params(&g_a, &prm);
        phone_process(&g_a, l + i, r + i, 128 < N - i ? 128 : N - i);
    }
    float worst = 0.0f;
    for (int i = 22050 + 1; i < N; i++)
        worst = fmaxf(worst, fabsf(l[i] - l[i - 1]));
    CHECK(worst < 0.08f, "largest sample step %g", (double)worst);
}

/* Loud input then silence: nothing in the state may drift into denormals. */
static void test_no_denormals(void)
{
    const float sr = 44100.0f;
    enum { N = 44100 * 6 };
    static float l[N], r[N];
    memset(l, 0, sizeof l);
    fill_sine(l, 22050, sr, 300.0f, 0.8f);
    memcpy(r, l, sizeof l);
    phone_init(&g_a, sr);
    phone_params prm = make_params(0.2f, 3, 7, 0.6f);
    run(&g_a, &prm, l, r, N, 128);
    CHECK(!has_subnormal(l, N) && !has_subnormal(r, N), "output must not contain denormals");
    CHECK(!has_subnormal(&g_a.env, 1) && !has_subnormal(g_a.lp1, 2) && !has_subnormal(g_a.lp2, 2) &&
              !has_subnormal(g_a.dc_lp, 2) && !has_subnormal(g_a.ring[0], PHONE_TAPS) &&
              !has_subnormal(g_a.ring[1], PHONE_TAPS),
          "filter state must not drift into denormals");
}

/* Input that never crosses zero (silence, DC) must not capture anything and must not crash. */
static void test_no_crossings(void)
{
    const float sr = 44100.0f;
    enum { N = 44100 };
    static float l[N], r[N];
    for (int i = 0; i < N; i++)
        l[i] = r[i] = 0.3f;
    phone_init(&g_a, sr);
    phone_params prm = make_params(PHONE_DEFAULT_TONE, 15, 31, 1.0f);
    run(&g_a, &prm, l, r, N, 100);
    CHECK(all_finite(l, N) && peak(l + N / 2, N / 2) < 1e-3f, "DC input must produce no wet signal");
    CHECK(phone_take_swaps(&g_a) == 0, "no fragment may be played for DC input");
}

static void test_activity_meter(void)
{
    const float sr = 44100.0f;
    enum { N = 22050 };
    static float l[N], r[N];
    fill_sine(l, N, sr, 220.0f, 0.5f);
    memcpy(r, l, sizeof l);
    phone_init(&g_a, sr);
    phone_params prm = make_params(PHONE_DEFAULT_TONE, 3, 7, 0.5f);
    run(&g_a, &prm, l, r, N, 128);
    float a = phone_take_activity(&g_a);
    CHECK(a > 0.9f && a <= 1.0f, "activity for a loud input: %g", (double)a);
    CHECK(phone_take_activity(&g_a) == 0.0f, "activity must clear after it is taken");
}

int main(void)
{
    test_tone_mapping();
    test_table();
    test_bypass_bit_exact();
    test_pitch_follows_tone();
    test_pitch_across_sample_rates();
    test_level_at_unity();
    test_fragment_and_repeat_rates();
    test_gate_after_input_stops();
    test_step_response();
    test_impulse_response();
    test_stability_and_headroom();
    test_block_size_independence();
    test_reset_clears_state();
    test_mono_and_stereo();
    test_no_clicks_on_control_changes();
    test_no_denormals();
    test_no_crossings();
    test_activity_meter();
    if (failures) {
        printf("%d test(s) FAILED\n", failures);
        return 1;
    }
    printf("all core tests passed\n");
    return 0;
}
