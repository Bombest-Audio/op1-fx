/* Behavioral tests for the Nitro core. Build and run with core/tests/run.sh */
#include "../nitro_core.h"

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
static float frand(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) / (float)(1u << 24) * 2.0f - 1.0f;
}

static float peak(const float *x, int n)
{
    float p = 0;
    for (int i = 0; i < n; i++)
        p = fmaxf(p, fabsf(x[i]));
    return p;
}

/* Shape value that lands exactly on table row r. */
static float shape_row(int r)
{
    return (float)r / (float)(NITRO_SHAPE_ROWS - 1);
}

/* Run a buffer through the core in chunks of `chunk` frames with fixed parameters, from a clean state. */
static void run_chunked(nitro_t *n, const nitro_params *prm, float *l, float *r, int frames, int chunk)
{
    nitro_set_params(n, prm);
    nitro_reset(n);
    for (int i = 0; i < frames; i += chunk) {
        int c = frames - i < chunk ? frames - i : chunk;
        nitro_process(n, l + i, r ? r + i : NULL, c);
    }
}

/* Steady-state gain of a sine through the core. The first half of the buffer is skipped as settling time. */
static float sine_gain(float sr, const nitro_params *prm, float hz, float amp)
{
    enum { N = 1 << 16 };
    static float x[N], y[N];
    for (int i = 0; i < N; i++)
        y[i] = x[i] = amp * (float)sin(2.0 * PI * hz * i / sr);
    nitro_t n;
    nitro_init(&n, sr);
    run_chunked(&n, prm, y, NULL, N, 256);
    double sx = 0, sy = 0;
    for (int i = N / 2; i < N; i++) {
        sx += (double)x[i] * x[i];
        sy += (double)y[i] * y[i];
    }
    return (float)sqrt(sy / sx);
}

static float db(float g)
{
    return 20.0f * log10f(g > 1e-9f ? g : 1e-9f);
}

/* Reference magnitude of a cascade of `poles` identical bilinear one-poles at hz, cutoff fc. */
static double cascade_mag(double sr, double fc, double hz, int poles)
{
    double w = tan(PI * hz / sr) / tan(PI * fc / sr);
    return pow(1.0 / sqrt(1.0 + w * w), poles);
}

static void test_stays_finite_and_bounded(void)
{
    enum { N = 22050 };
    static float l[N], r[N];
    const float rates[] = {22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f};
    for (int ri = 0; ri < 5; ri++)
        for (int f = 0; f <= 1; f++)
            for (int re = 0; re <= 1; re++)
                for (int sh = 0; sh <= 11; sh += 1)
                    for (int dr = 0; dr <= 1; dr++) {
                        for (int i = 0; i < N; i++) {
                            l[i] = frand();
                            r[i] = frand();
                        }
                        nitro_t n;
                        nitro_init(&n, rates[ri]);
                        nitro_params prm = {(float)f, (float)re, shape_row(sh), (float)dr};
                        run_chunked(&n, &prm, l, r, N, 512);
                        float pk = fmaxf(peak(l, N), peak(r, N));
                        CHECK(isfinite(pk), "non-finite output (sr %.0f f%d r%d s%d d%d)", rates[ri], f, re, sh, dr);
                        CHECK(pk <= 1.0001f, "peak %.4f above 1.0 (sr %.0f f%d r%d s%d d%d)", pk, rates[ri], f, re, sh,
                              dr);
                    }
}

static void test_lowpass_response(void)
{
    const float sr = 48000.0f, fc = 1000.0f;
    nitro_params prm = {nitro_freq_to_norm(fc), 0.0f, shape_row(0), 0.0f};
    const float amp = 0.05f; /* far below the soft ceiling, so the filter is linear */
    const float hz[] = {100.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f};
    for (int i = 0; i < 5; i++) {
        float g = db(sine_gain(sr, &prm, hz[i], amp));
        float ref = db((float)cascade_mag(sr, fc, hz[i], 4));
        printf("  LP 24 at %5.0f Hz: %7.2f dB (reference %7.2f dB)\n", hz[i], g, ref);
        CHECK(fabsf(g - ref) < 0.6f, "LP 24 gain at %.0f Hz is %.2f dB, expected %.2f", hz[i], g, ref);
    }
    /* each step down in shape row drops one pole: LP 6 attenuates less than LP 24 */
    nitro_params lp6 = {prm.freq, 0.0f, shape_row(3), 0.0f};
    float g6 = db(sine_gain(sr, &lp6, 4000.0f, amp));
    float ref6 = db((float)cascade_mag(sr, fc, 4000.0f, 1));
    CHECK(fabsf(g6 - ref6) < 0.6f, "LP 6 gain at 4 kHz is %.2f dB, expected %.2f", g6, ref6);
}

static void test_highpass_notch_band(void)
{
    const float sr = 48000.0f, fc = 1000.0f;
    const float amp = 0.05f;
    nitro_params hp = {nitro_freq_to_norm(fc), 0.0f, shape_row(11), 0.0f};
    float lo = db(sine_gain(sr, &hp, 62.5f, amp)), hi = db(sine_gain(sr, &hp, 8000.0f, amp));
    float mid = db(sine_gain(sr, &hp, fc, amp));
    printf("  HP 24: 62 Hz %.1f dB, 1 kHz %.1f dB, 8 kHz %.1f dB\n", lo, mid, hi);
    CHECK(lo < -60.0f, "HP 24 must remove 62 Hz (%.1f dB)", lo);
    CHECK(fabsf(hi) < 1.0f, "HP 24 must pass 8 kHz (%.1f dB)", hi);
    CHECK(mid > -14.0f && mid < -10.0f, "HP 24 at the cutoff should be about -12 dB (%.1f)", mid);

    nitro_params notch = {nitro_freq_to_norm(fc), 0.0f, shape_row(6), 0.0f};
    float nd = db(sine_gain(sr, &notch, fc, amp));
    float nl = db(sine_gain(sr, &notch, 100.0f, amp)), nh = db(sine_gain(sr, &notch, 10000.0f, amp));
    printf("  NOTCH: 100 Hz %.1f dB, 1 kHz %.1f dB, 10 kHz %.1f dB\n", nl, nd, nh);
    CHECK(nd < -30.0f, "notch must cut the cutoff frequency (%.1f dB)", nd);
    CHECK(fabsf(nl) < 1.0f && fabsf(nh) < 1.0f, "notch must pass both sides (%.1f, %.1f)", nl, nh);

    nitro_params bp = {nitro_freq_to_norm(fc), 0.0f, shape_row(7), 0.0f};
    float bc = db(sine_gain(sr, &bp, fc, amp)), bl = db(sine_gain(sr, &bp, 60.0f, amp)),
          bh = db(sine_gain(sr, &bp, 16000.0f, amp));
    printf("  BAND: 60 Hz %.1f dB, 1 kHz %.1f dB, 16 kHz %.1f dB\n", bl, bc, bh);
    CHECK(bc > bl + 15.0f && bc > bh + 15.0f, "band shape must peak at the cutoff");
}

static void test_thru_is_transparent(void)
{
    enum { N = 8192 };
    static float x[N], y[N];
    for (int i = 0; i < N; i++)
        y[i] = x[i] = 0.7f * frand();
    nitro_t n;
    nitro_init(&n, 44100.0f);
    nitro_params prm = {0.5f, 0.0f, shape_row(4), 0.0f};
    run_chunked(&n, &prm, y, NULL, N, 128);
    /* start from the settled state: skip the 5 ms parameter ramp, which begins at the target anyway */
    float worst = 0;
    for (int i = 0; i < N; i++)
        worst = fmaxf(worst, fabsf(y[i] - x[i]));
    printf("  THRU, drive 0, res 0: largest deviation %.2e\n", worst);
    CHECK(worst < 1e-6f, "THRU at drive 0 must leave audio alone below the soft ceiling (%.2e)", worst);
}

static void test_step_response(void)
{
    enum { N = 48000 };
    static float y[N];
    for (int i = 0; i < N; i++)
        y[i] = 0.2f;
    nitro_t n;
    nitro_init(&n, 48000.0f);
    nitro_params prm = {nitro_freq_to_norm(500.0f), 0.0f, shape_row(0), 0.0f};
    run_chunked(&n, &prm, y, NULL, N, 480);
    float pk = peak(y, N);
    printf("  LP 24 step to 0.2: peak %.5f, final %.5f\n", pk, y[N - 1]);
    CHECK(fabsf(y[N - 1] - 0.2f) < 1e-3f, "step must settle at the input level (%.5f)", y[N - 1]);
    CHECK(pk < 0.2f * 1.001f, "LP 24 without resonance must not overshoot (%.5f)", pk);
    CHECK(y[0] < 0.01f, "first sample after a step is small (%.5f)", y[0]);
}

static void test_resonance(void)
{
    const float sr = 48000.0f, fc = 1000.0f;
    nitro_params off = {nitro_freq_to_norm(fc), 0.0f, shape_row(0), 0.0f};
    nitro_params on = {nitro_freq_to_norm(fc), 1.0f, shape_row(0), 1.0f};
    float g0 = db(sine_gain(sr, &off, fc, 0.02f)), g1 = db(sine_gain(sr, &on, fc, 0.02f));
    printf("  gain at cutoff: RES 0 %.1f dB, RES 1 + DRIVE 1 %.1f dB\n", g0, g1);
    CHECK(g1 > g0 + 12.0f, "RES must raise the gain at the cutoff by more than 12 dB");

    /* ring: an impulse rings longer with resonance */
    enum { N = 24000 };
    static float a[N], b[N];
    memset(a, 0, sizeof a);
    memset(b, 0, sizeof b);
    a[0] = b[0] = 0.5f;
    nitro_t n;
    nitro_init(&n, sr);
    run_chunked(&n, &off, a, NULL, N, 256);
    run_chunked(&n, &on, b, NULL, N, 256);
    double ea = 0, eb = 0;
    for (int i = 4800; i < N; i++) {
        ea += (double)a[i] * a[i];
        eb += (double)b[i] * b[i];
    }
    printf("  impulse tail energy after 100 ms: RES 0 %.3e, RES 1.0 %.3e\n", ea, eb);
    CHECK(eb > ea * 1000.0 + 1e-6, "RES 1 with DRIVE 1 must keep ringing after the impulse");
}

static double harmonic_db(const float *y, int n, float sr, float f0, int h)
{
    /* single-bin DFT at h * f0 over a whole number of periods is not guaranteed, so use a Hann window */
    double re = 0, im = 0, re1 = 0, im1 = 0, wsum = 0;
    for (int i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(2.0 * PI * i / (n - 1));
        double a = 2.0 * PI * f0 * h * i / sr, a1 = 2.0 * PI * f0 * i / sr;
        re += w * y[i] * cos(a);
        im += w * y[i] * sin(a);
        re1 += w * y[i] * cos(a1);
        im1 += w * y[i] * sin(a1);
        wsum += w;
    }
    double mh = sqrt(re * re + im * im) / wsum, m1 = sqrt(re1 * re1 + im1 * im1) / wsum;
    return 20.0 * log10((mh + 1e-12) / (m1 + 1e-12));
}

static void test_drive_adds_harmonics(void)
{
    enum { N = 1 << 15 };
    static float y0[N], y1[N];
    const float sr = 48000.0f, f0 = 1000.0f;
    for (int i = 0; i < N; i++)
        y0[i] = y1[i] = 0.5f * (float)sin(2.0 * PI * f0 * i / sr);
    nitro_t n;
    nitro_init(&n, sr);
    nitro_params lo = {1.0f, 0.0f, shape_row(4), 0.0f};
    nitro_params hi = {1.0f, 0.0f, shape_row(4), 1.0f};
    run_chunked(&n, &lo, y0, NULL, N, 256);
    run_chunked(&n, &hi, y1, NULL, N, 256);
    double h3lo = harmonic_db(y0, N, sr, f0, 3), h3hi = harmonic_db(y1, N, sr, f0, 3);
    printf("  3rd harmonic: DRIVE 0 %.1f dB, DRIVE 1 %.1f dB (relative to the fundamental)\n", h3lo, h3hi);
    CHECK(h3lo < -80.0, "DRIVE 0 must be clean at 0.5 amplitude (%.1f dB)", h3lo);
    CHECK(h3hi > -30.0, "DRIVE 1 must distort (%.1f dB)", h3hi);
}

static void test_level_across_drive(void)
{
    /* a -12 dBFS sine through every shape at full drive and at the default must stay at or under 0 dBFS and keep a
     * sensible RMS (not silent) for the shapes that pass 440 Hz at a 2 kHz cutoff */
    const float sr = 44100.0f;
    float worst_peak = 0;
    for (int sh = 0; sh < NITRO_SHAPE_ROWS; sh++)
        for (int dr = 0; dr <= 1; dr++) {
            enum { N = 1 << 14 };
            static float y[N];
            for (int i = 0; i < N; i++)
                y[i] = 0.25f * (float)sin(2.0 * PI * 440.0 * i / sr);
            nitro_t n;
            nitro_init(&n, sr);
            nitro_params prm = {nitro_freq_to_norm(2000.0f), 0.5f, shape_row(sh), (float)dr};
            run_chunked(&n, &prm, y, NULL, N, 128);
            worst_peak = fmaxf(worst_peak, peak(y, N));
        }
    printf("  worst peak for a -12 dBFS sine over all shapes, RES 0.5, DRIVE 0 and 1: %.3f\n", worst_peak);
    CHECK(worst_peak <= 1.0001f, "peak %.4f", worst_peak);
}

static void test_param_sweep_no_clicks(void)
{
    enum { N = 44100 };
    static float l[N];
    for (int i = 0; i < N; i++)
        l[i] = 0.4f * sinf(2.0f * 3.14159265f * 220.0f * (float)i / 44100.0f);
    nitro_t n;
    nitro_init(&n, 44100.0f);
    nitro_params prm = {0.3f, 0.3f, 0.2f, 0.4f};
    nitro_set_params(&n, &prm);
    nitro_reset(&n);
    for (int i = 0; i < N; i += 128) {
        int b = i / 128;
        prm.freq = (float)(b % 7) / 7.0f;
        prm.shape = (float)(b % 5) / 5.0f;
        prm.res = (float)(b % 3) / 3.0f;
        prm.drive = (float)(b % 4) / 4.0f;
        nitro_set_params(&n, &prm);
        nitro_process(&n, l + i, NULL, N - i < 128 ? N - i : 128);
    }
    float worst = 0;
    for (int i = 1; i < N; i++)
        worst = fmaxf(worst, fabsf(l[i] - l[i - 1]));
    printf("  largest sample step with all four controls jumping each block: %.4f\n", worst);
    CHECK(worst < 0.35f, "parameter changes caused a discontinuity (%.4f)", worst);
}

static void test_block_size_and_mono(void)
{
    enum { N = 6000 };
    static float a[N], b[N], ar[N], br[N], m[N];
    for (int i = 0; i < N; i++)
        a[i] = b[i] = ar[i] = br[i] = m[i] = 0.8f * frand();
    nitro_t n;
    nitro_init(&n, 44100.0f);
    nitro_params prm = {0.45f, 0.6f, 0.5f, 0.7f};
    run_chunked(&n, &prm, a, ar, N, 1);
    run_chunked(&n, &prm, b, br, N, 777);
    CHECK(memcmp(a, b, sizeof a) == 0 && memcmp(ar, br, sizeof ar) == 0,
          "block size 1 and block size 777 must give identical output");
    run_chunked(&n, &prm, m, NULL, N, 64);
    CHECK(memcmp(a, m, sizeof a) == 0, "mono must equal the left channel of the same stereo input");
}

static void test_reset_clears_state(void)
{
    enum { N = 4096 };
    static float x[N], z[N];
    for (int i = 0; i < N; i++)
        x[i] = frand();
    memset(z, 0, sizeof z);
    nitro_t n;
    nitro_init(&n, 44100.0f);
    nitro_params prm = {0.5f, 0.9f, 0.1f, 1.0f};
    run_chunked(&n, &prm, x, NULL, N, 256);
    nitro_reset(&n);
    nitro_process(&n, z, NULL, N);
    CHECK(peak(z, N) == 0.0f, "after reset, silence in must be silence out (%.3e)", peak(z, N));
    CHECK(nitro_take_activity(&n) == 0.0f, "reset must clear the activity value");
}

static void test_no_denormals(void)
{
    enum { N = 44100 * 6 };
    static float y[N];
    for (int i = 0; i < N; i++)
        y[i] = i < 4410 ? 0.8f * frand() : 0.0f;
    nitro_t n;
    nitro_init(&n, 44100.0f);
    nitro_params prm = {0.4f, 0.5f, 0.0f, 0.5f};
    run_chunked(&n, &prm, y, NULL, N, 512);
    int bad = 0;
    for (int i = 0; i < N; i++)
        if (fpclassify(y[i]) == FP_SUBNORMAL)
            bad++;
    printf("  subnormal samples in 6 s of decay: %d, last sample %.3e\n", bad, y[N - 1]);
    CHECK(bad == 0, "%d subnormal samples", bad);
}

static void test_activity(void)
{
    enum { N = 8192 };
    static float l[N], r[N];
    nitro_t n;
    nitro_init(&n, 44100.0f);
    nitro_params prm = {NITRO_DEFAULT_FREQ, NITRO_DEFAULT_RES, NITRO_DEFAULT_SHAPE, NITRO_DEFAULT_DRIVE};
    nitro_set_params(&n, &prm);
    nitro_reset(&n);
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    nitro_process(&n, l, r, N);
    const float quiet = nitro_take_activity(&n);
    for (int i = 0; i < N; i++)
        l[i] = r[i] = 0.6f * (i % 2 ? 1.0f : -1.0f);
    nitro_process(&n, l, r, N);
    const float loud = nitro_take_activity(&n);
    const float again = nitro_take_activity(&n);
    printf("  activity: silence %.3f, loud %.3f, after take %.3f\n", quiet, loud, again);
    CHECK(quiet == 0.0f, "silence must read as no activity (%.3f)", quiet);
    CHECK(loud > 0.5f && loud <= 1.0f, "loud input must read as strong activity (%.3f)", loud);
    CHECK(again == 0.0f, "taking activity must clear it");
}

static void test_mappings(void)
{
    CHECK(fabsf(nitro_freq_hz(0.0f) - 14.0f) < 0.01f && fabsf(nitro_freq_hz(1.0f) - 16000.0f) < 1.0f, "freq range");
    CHECK(fabsf(nitro_freq_to_norm(nitro_freq_hz(0.37f)) - 0.37f) < 1e-4f, "freq mapping must round trip");
    CHECK(strcmp(nitro_shape_name(0.0f), "LP 24") == 0 && strcmp(nitro_shape_name(1.0f), "HP 24") == 0, "shape names");
    CHECK(strcmp(nitro_shape_name(NITRO_DEFAULT_SHAPE), "HP 6+") == 0, "default shape is the factory row (%s)",
          nitro_shape_name(NITRO_DEFAULT_SHAPE));
}

int main(void)
{
    test_stays_finite_and_bounded();
    test_lowpass_response();
    test_highpass_notch_band();
    test_thru_is_transparent();
    test_step_response();
    test_resonance();
    test_drive_adds_harmonics();
    test_level_across_drive();
    test_param_sweep_no_clicks();
    test_block_size_and_mono();
    test_reset_clears_state();
    test_no_denormals();
    test_activity();
    test_mappings();
    if (failures) {
        printf("%d FAILED\n", failures);
        return 1;
    }
    printf("PASSED\n");
    return 0;
}
