/* Behavioral tests for the Cwo core. Build and run with core/tests/run.sh */
#include "../cwo_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI_D 3.14159265358979323846

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

/* One instance is about 1 MB, so keep it out of the stack. */
static cwo_t g_core;

static float peak(const float *x, int n)
{
    float p = 0;
    for (int i = 0; i < n; i++)
        p = fmaxf(p, fabsf(x[i]));
    return p;
}

static void run(cwo_t *p, const cwo_params *prm, float *l, float *r, int n, int block)
{
    cwo_set_params(p, prm);
    cwo_reset(p);
    for (int i = 0; i < n; i += block) {
        int c = n - i < block ? n - i : block;
        cwo_process(p, l + i, r + i, c);
    }
}

/* Inverse of cwo_shift_hz. */
static float norm_for_hz(float hz)
{
    return logf(2.0f * hz + 1.0f) / logf(8000.0f);
}

/* Hann-windowed single-bin DFT magnitude (peak amplitude of a sine at hz), over x[start .. start + n). */
static double tone_amp(const float *x, int start, int n, double hz, double sr)
{
    double re = 0, im = 0, wsum = 0;
    for (int i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(2.0 * PI_D * i / (n - 1));
        double ph = 2.0 * PI_D * hz * (start + i) / sr;
        re += w * x[start + i] * cos(ph);
        im += w * x[start + i] * sin(ph);
        wsum += w;
    }
    return 2.0 * sqrt(re * re + im * im) / wsum;
}

static double db(double v)
{
    return 20.0 * log10(v > 1e-12 ? v : 1e-12);
}

static void test_curves(void)
{
    CHECK(cwo_shift_hz(0.0f) == 0.0f, "shift 0 must be 0 Hz");
    CHECK(fabsf(cwo_shift_hz(1.0f) - 3999.5f) < 1.0f, "shift 1 should be 4 kHz, got %f", (double)cwo_shift_hz(1.0f));
    CHECK(fabsf(cwo_delay_ms(0.0f) - CWO_MIN_DELAY_MS) < 1e-4f, "delay 0 should be the minimum");
    CHECK(fabsf(cwo_delay_ms(1.0f) - CWO_MAX_DELAY_MS) < 1e-3f, "delay 1 should be the maximum");
    CHECK(cwo_feedback_gain(0.0f) == 0.0f && fabsf(cwo_feedback_gain(1.0f) - CWO_MAX_FEEDBACK) < 1e-6f, "feedback range");
    float prev = -1.0f;
    for (int i = 0; i <= 100; i++) {
        float v = cwo_shift_hz((float)i / 100.0f);
        CHECK(v >= prev, "shift curve must be monotonic");
        prev = v;
    }
    CHECK(fabsf(CWO_DEFAULT_FREQ - 5000.0f / 32767.0f) < 1e-3f, "default freq");
    CHECK(fabsf(CWO_DEFAULT_DELAY - 8192.0f / 32767.0f) < 1e-3f, "default delay");
    CHECK(fabsf(CWO_DEFAULT_FDBACK - 16384.0f / 32767.0f) < 1e-3f, "default fdback");
    CHECK(fabsf(CWO_DEFAULT_SIDEBAND - 1.0f) < 1e-6f, "default sideband");
    printf("  default shift %.2f Hz, delay %.1f ms, feedback gain %.3f\n", (double)cwo_shift_hz(CWO_DEFAULT_FREQ),
           (double)cwo_delay_ms(CWO_DEFAULT_DELAY), (double)cwo_feedback_gain(CWO_DEFAULT_FDBACK));
}

static void test_silence_and_reset(void)
{
    enum { N = 8192 };
    static float l[N], r[N];
    cwo_init(&g_core, 44100.0f);
    cwo_params prm = {0.4f, 0.1f, 1.0f, 0.3f};
    run(&g_core, &prm, l, r, N, 128);
    CHECK(peak(l, N) == 0.0f && peak(r, N) == 0.0f, "silence in must give silence out");

    for (int i = 0; i < N; i++) {
        l[i] = frand();
        r[i] = frand();
    }
    cwo_process(&g_core, l, r, N);
    cwo_reset(&g_core);
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    cwo_process(&g_core, l, r, N);
    CHECK(peak(l, N) == 0.0f && peak(r, N) == 0.0f, "reset must clear every echo");
}

/* Before the first echo arrives, the output is the input untouched. */
static void test_dry_before_first_echo(void)
{
    enum { N = 4096 };
    static float l[N], r[N], l0[N], r0[N];
    for (int i = 0; i < 400; i++) {
        l0[i] = l[i] = 0.5f * frand();
        r0[i] = r[i] = 0.5f * frand();
    }
    for (int i = 400; i < N; i++)
        l0[i] = l[i] = r0[i] = r[i] = 0.0f;
    cwo_init(&g_core, 44100.0f);
    cwo_params prm = {0.7f, 0.0f, 0.9f, 1.0f}; /* shortest delay: 1 ms is 44.1 samples, 45 after rounding up */
    run(&g_core, &prm, l, r, N, 128);
    const int n_dry = 40; /* inside the 44 sample minimum delay */
    CHECK(memcmp(l, l0, sizeof(float) * n_dry) == 0 && memcmp(r, r0, sizeof(float) * n_dry) == 0,
          "output must equal input until the first echo arrives");
    /* and the echo must then arrive: energy after the delay that the input does not account for */
    double e = 0;
    for (int i = 60; i < 400; i++)
        e += (double)(l[i] - l0[i]) * (l[i] - l0[i]);
    CHECK(e > 0.01, "the echo never arrived (energy %.5f)", e);
}

static void test_delay_time_across_rates(void)
{
    const float rates[] = {22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f};
    const float want_ms = cwo_delay_ms(0.5f);
    for (int ri = 0; ri < 5; ri++) {
        const float sr = rates[ri];
        const int n = (int)(sr * 0.5f);
        float *l = (float *)calloc((size_t)n, sizeof(float));
        float *r = (float *)calloc((size_t)n, sizeof(float));
        l[0] = r[0] = 0.5f;
        cwo_init(&g_core, sr);
        cwo_params prm = {0.0f, 0.5f, 0.0f, 1.0f}; /* no shift, no feedback: one clean echo */
        run(&g_core, &prm, l, r, n, 256);
        int best = 1;
        float bp = 0;
        for (int i = 8; i < n; i++)
            if (fabsf(l[i]) > bp) {
                bp = fabsf(l[i]);
                best = i;
            }
        float got_ms = 1000.0f * (float)best / sr;
        printf("  sr %6.0f: echo peak at %.2f ms (want %.2f), level %.3f\n", (double)sr, (double)got_ms, (double)want_ms,
               (double)bp);
        CHECK(fabsf(got_ms - want_ms) < 2.0f, "sr %.0f: echo at %.2f ms, want %.2f", (double)sr, (double)got_ms,
              (double)want_ms);
        CHECK(bp > 0.1f, "sr %.0f: echo too weak (%.3f)", (double)sr, (double)bp);
        free(l);
        free(r);
    }
}

static void test_sidebands(void)
{
    const double sr = 44100.0, f0 = 1000.0, shift = 200.0;
    enum { N = 44100 };
    static float l[N], r[N], dry[N];
    const float amp = 0.3f;
    const float fnorm = norm_for_hz((float)shift);
    const float sb[3] = {1.0f, 0.0f, 0.5f};
    const char *name[3] = {"upper", "lower", "ring"};
    for (int s = 0; s < 3; s++) {
        for (int i = 0; i < N; i++) {
            dry[i] = amp * (float)sin(2.0 * PI_D * f0 * i / sr);
            l[i] = r[i] = dry[i];
        }
        cwo_init(&g_core, (float)sr);
        cwo_params prm = {fnorm, 0.0f, 0.0f, sb[s]};
        run(&g_core, &prm, l, r, N, 128);
        for (int i = 0; i < N; i++)
            l[i] -= dry[i]; /* the wet signal alone */
        const int start = 8192, len = 32768;
        double up = tone_amp(l, start, len, f0 + shift, sr);
        double dn = tone_amp(l, start, len, f0 - shift, sr);
        double ctr = tone_amp(l, start, len, f0, sr);
        printf("  %-5s sideband: upper %.1f dB, lower %.1f dB, unshifted %.1f dB (input %.1f dB)\n", name[s], db(up),
               db(dn), db(ctr), db(amp));
        if (s == 0) {
            CHECK(db(up) - db(dn) > 25.0, "upper sideband: image only %.1f dB down", db(up) - db(dn));
            CHECK(fabs(db(up) - db(amp)) < 1.0, "upper sideband should keep unit gain, got %.2f dB", db(up) - db(amp));
        } else if (s == 1) {
            CHECK(db(dn) - db(up) > 25.0, "lower sideband: image only %.1f dB down", db(dn) - db(up));
            CHECK(fabs(db(dn) - db(amp)) < 1.0, "lower sideband should keep unit gain, got %.2f dB", db(dn) - db(amp));
        } else {
            CHECK(fabs(db(up) - db(dn)) < 1.0, "ring modulation should give both sidebands equally");
            CHECK(db(up) > db(amp) - 7.0, "ring modulation sidebands too weak");
        }
    }
}

static void test_feedback_decays(void)
{
    const float sr = 44100.0f;
    enum { N = 44100 * 2 };
    static float l[N], r[N];
    const float d_norm = 0.1f;
    const int D = (int)(cwo_delay_ms(d_norm) * 0.001f * sr);
    const float fbs[3] = {0.5f, 1.0f, 0.0f};
    for (int k = 0; k < 3; k++) {
        memset(l, 0, sizeof l);
        memset(r, 0, sizeof r);
        l[0] = 0.5f;
        cwo_init(&g_core, sr);
        cwo_params prm = {0.3f, d_norm, fbs[k], 1.0f};
        run(&g_core, &prm, l, r, N, 128);
        double e[6];
        for (int w = 0; w < 6; w++) {
            e[w] = 0;
            for (int i = (w + 1) * D - 60; i < (w + 1) * D + D - 60 && i < N; i++)
                e[w] += (double)l[i] * l[i];
        }
        printf("  feedback %.2f: echo energy per repeat %.4f %.4f %.4f %.4f %.4f %.4f\n", (double)fbs[k], e[0], e[1],
               e[2], e[3], e[4], e[5]);
        CHECK(isfinite(peak(l, N)), "feedback %.2f produced non-finite output", (double)fbs[k]);
        if (k == 2)
            /* what is left in window 1 is the allpass ringing of the Hilbert pair, not a repeat */
            CHECK(e[1] < e[0] * 0.01, "no feedback must give a single echo");
        else
            for (int w = 1; w < 5; w++)
                CHECK(e[w] < e[w - 1] * 0.95, "feedback %.2f: repeat %d did not decay", (double)fbs[k], w);
    }
    /* The tail dies out completely at the highest feedback. */
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    l[0] = 0.5f;
    cwo_init(&g_core, sr);
    cwo_params prm = {0.3f, 0.0f, 1.0f, 1.0f};
    run(&g_core, &prm, l, r, N, 128);
    float tail = peak(l + N - 4410, 4410);
    printf("  tail peak in the last 100 ms at full feedback, 1 ms delay: %.2e\n", (double)tail);
    CHECK(tail < 0.01f, "full feedback must still decay (tail %.4f)", (double)tail);
}

static void test_stays_finite_and_bounded(void)
{
    const float rates[] = {22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f};
    static float l[192000], r[192000];
    int runs = 0;
    for (int ri = 0; ri < 5; ri++) {
        const int n = (int)rates[ri];
        for (int f = 0; f <= 1; f++)
            for (int d = 0; d <= 1; d++)
                for (int fb = 0; fb <= 1; fb++)
                    for (int sb = 0; sb <= 2; sb++)
                        for (int kind = 0; kind < 2; kind++) {
                            for (int i = 0; i < n; i++) {
                                l[i] = kind == 0 ? frand() : 0.99f; /* full-scale noise, then DC */
                                r[i] = kind == 0 ? frand() : -0.99f;
                            }
                            cwo_init(&g_core, rates[ri]);
                            cwo_params prm = {(float)f, (float)d, (float)fb, 0.5f * (float)sb};
                            run(&g_core, &prm, l, r, n, 128);
                            float pk = fmaxf(peak(l, n), peak(r, n));
                            runs++;
                            CHECK(isfinite(pk), "non-finite output (sr %.0f f%d d%d fb%d sb%d k%d)", (double)rates[ri], f,
                                  d, fb, sb, kind);
                            CHECK(pk <= 1.0001f, "peak %.4f above 1.0 (sr %.0f f%d d%d fb%d sb%d k%d)", (double)pk,
                                  (double)rates[ri], f, d, fb, sb, kind);
                        }
    }
    printf("  %d corner runs, all finite and within +-1\n", runs);
}

static void test_param_sweep_no_clicks(void)
{
    enum { N = 44100 * 2 };
    static float l[N], r[N];
    for (int i = 0; i < N; i++)
        l[i] = r[i] = 0.4f * sinf(2.0f * 3.14159265f * 220.0f * (float)i / 44100.0f);
    cwo_init(&g_core, 44100.0f);
    cwo_params prm = {0.3f, 0.5f, 0.6f, 0.8f};
    cwo_set_params(&g_core, &prm);
    cwo_reset(&g_core);
    for (int i = 0; i < N; i += 128) {
        int b = i / 128;
        prm.freq = (float)(b % 7) / 7.0f;
        prm.delay = (float)((b * 3) % 11) / 11.0f;
        prm.fdback = (float)(b % 5) / 5.0f;
        prm.sideband = (float)((b * 7) % 4) / 3.0f;
        cwo_set_params(&g_core, &prm);
        cwo_process(&g_core, l + i, r + i, N - i < 128 ? N - i : 128);
    }
    float worst = 0;
    for (int i = 1; i < N; i++)
        worst = fmaxf(worst, fabsf(l[i] - l[i - 1]));
    printf("  largest sample step with every control changing each block: %.4f\n", (double)worst);
    /* The sine alone steps 0.013 per sample; with its own echo on top the sum can reach about 0.05. */
    CHECK(worst < 0.15f, "parameter changes caused a discontinuity (%.4f)", (double)worst);
}

static void test_channels_independent(void)
{
    enum { N = 16384 };
    static float l[N], r[N];
    cwo_init(&g_core, 44100.0f);
    for (int i = 0; i < 2000; i++)
        l[i] = 0.5f * frand();
    cwo_params prm = {0.5f, 0.05f, 0.8f, 0.5f};
    run(&g_core, &prm, l, r, N, 128);
    CHECK(peak(r, N) == 0.0f, "a silent channel must stay silent (peak %g)", (double)peak(r, N));
    CHECK(peak(l + 100, N - 100) > 0.01f, "left channel should have echoes");
}

static void test_block_size_independence(void)
{
    enum { N = 20000 };
    static float a[N], ar[N], b[N], br[N];
    for (int i = 0; i < N; i++) {
        a[i] = b[i] = 0.7f * frand();
        ar[i] = br[i] = 0.7f * frand();
    }
    cwo_init(&g_core, 48000.0f);
    cwo_params prm = {0.45f, 0.08f, 0.7f, 0.2f};
    run(&g_core, &prm, a, ar, N, 1);
    cwo_init(&g_core, 48000.0f);
    run(&g_core, &prm, b, br, N, 128);
    CHECK(memcmp(a, b, sizeof a) == 0 && memcmp(ar, br, sizeof ar) == 0, "block size 1 and 128 must match bit for bit");
}

static void test_activity(void)
{
    enum { N = 16384 };
    static float l[N], r[N];
    cwo_init(&g_core, 44100.0f);
    cwo_params prm = {CWO_DEFAULT_FREQ, 0.1f, CWO_DEFAULT_FDBACK, CWO_DEFAULT_SIDEBAND};
    cwo_set_params(&g_core, &prm);
    cwo_reset(&g_core);
    cwo_process(&g_core, l, r, N);
    const float quiet = cwo_take_activity(&g_core);
    l[0] = r[0] = 0.8f;
    cwo_process(&g_core, l, r, N);
    const float hit = cwo_take_activity(&g_core);
    const float again = cwo_take_activity(&g_core);
    printf("  activity: silence %.3f, after an impulse %.3f, after take %.3f\n", (double)quiet, (double)hit, (double)again);
    CHECK(quiet == 0.0f, "silence must read as no activity (%.3f)", (double)quiet);
    CHECK(hit > 0.05f, "an echo must read as activity (%.3f)", (double)hit);
    CHECK(again == 0.0f, "taking activity must clear it");
}

int main(void)
{
    test_curves();
    test_silence_and_reset();
    test_dry_before_first_echo();
    test_delay_time_across_rates();
    test_sidebands();
    test_feedback_decays();
    test_stays_finite_and_bounded();
    test_param_sweep_no_clicks();
    test_channels_independent();
    test_block_size_independence();
    test_activity();
    if (failures) {
        printf("%d FAILED\n", failures);
        return 1;
    }
    printf("PASSED\n");
    return 0;
}
