/* Behavioral tests for the Delay core. Build and run with core/tests/run.sh */
#include "../delay_core.h"

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

static unsigned rng_state = 12345u;
static float frand(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) / (float)(1u << 24) * 2.0f - 1.0f;
}

/* One delay instance with its own memory. */
typedef struct {
    delay_t d;
    float *bl, *br;
} fx_t;

static void fx_new(fx_t *f, float sr, const delay_params *prm)
{
    const int n = delay_buffer_frames(sr);
    f->bl = (float *)malloc(sizeof(float) * (size_t)n);
    f->br = (float *)malloc(sizeof(float) * (size_t)n);
    delay_init(&f->d, sr, f->bl, f->br);
    delay_set_params(&f->d, prm);
    delay_reset(&f->d);
}

static void fx_free(fx_t *f)
{
    free(f->bl);
    free(f->br);
}

static void run_blocks(fx_t *f, float *l, float *r, int n, int block)
{
    for (int i = 0; i < n; i += block) {
        int c = n - i < block ? n - i : block;
        delay_process(&f->d, l + i, r + i, c);
    }
}

static float peak(const float *x, int n)
{
    float p = 0;
    for (int i = 0; i < n; i++)
        p = fmaxf(p, fabsf(x[i]));
    return p;
}

static double rms_range(const float *x, int a, int b)
{
    double s = 0;
    for (int i = a; i < b; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (double)(b - a));
}

static double sum_range(const float *x, int a, int b)
{
    double s = 0;
    for (int i = a; i < b; i++)
        s += x[i];
    return s;
}

static const float kRates[] = {22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f};

static void test_parameter_maps(void)
{
    CHECK(fabsf(delay_base_seconds(0.0f) - 1.0f / 32.0f) < 1e-6f, "TIME 0 must be 1/32 s");
    CHECK(fabsf(delay_base_seconds(1.0f) - 1.0f) < 1e-5f, "TIME 1 must be 1 s");
    float prev = 0.0f;
    for (int i = 0; i <= 100; i++) {
        float b = delay_base_seconds((float)i / 100.0f);
        CHECK(b > prev, "TIME must rise monotonically");
        prev = b;
    }
    /* The delay never goes past DELAY_MAX_SECONDS, at any knob position. */
    float worst = 0.0f;
    for (int t = 0; t <= 20; t++)
        for (int s = 0; s <= 20; s++) {
            float d = delay_time_seconds((float)t / 20.0f, (float)s / 20.0f);
            worst = fmaxf(worst, d);
            CHECK(d >= 1.0f / 32.0f - 1e-6f, "delay shorter than the shortest loop");
        }
    CHECK(worst <= DELAY_MAX_SECONDS + 1e-4f, "delay above the ceiling (%.3f)", worst);
    CHECK(fabsf(worst - DELAY_MAX_SECONDS) < 1e-3f, "ceiling should be reachable (%.3f)", worst);
    float r0 = delay_speed_ratio(0.0f, 0.0f), r1 = delay_speed_ratio(1.0f, 0.5f);
    CHECK(fabsf(r0 - 1.0f / 32.0f) < 1e-6f && r1 == 1.0f, "speed ratio ends (%.4f, %.4f)", r0, r1);
    printf("  default delay: %.3f s\n", delay_time_seconds(DELAY_DEFAULT_TIME, DELAY_DEFAULT_SPEED));
}

static void test_input_zero_is_bit_exact(void)
{
    enum { N = 8192 };
    static float l[N], r[N], l0[N], r0[N];
    for (int ri = 0; ri < 5; ri++) {
        for (int i = 0; i < N; i++) {
            l0[i] = l[i] = frand();
            r0[i] = r[i] = frand();
        }
        delay_params prm = {0.4f, 0.6f, 0.9f, 0.0f};
        fx_t f;
        fx_new(&f, kRates[ri], &prm);
        run_blocks(&f, l, r, N, 128);
        CHECK(memcmp(l, l0, sizeof l) == 0 && memcmp(r, r0, sizeof r) == 0,
              "INPUT 0 on an empty tape must leave audio untouched (sr %.0f)", kRates[ri]);
        fx_free(&f);
    }
}

/* Impulse in, then check that echoes arrive where delay_time_seconds says and that each is FDB times the one before. */
static void test_impulse_echoes(void)
{
    const float sr = 44100.0f;
    enum { N = 44100 * 3 };
    static float l[N], r[N];
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    l[0] = r[0] = 0.5f;
    delay_params prm = {0.8f, 1.0f, 0.5f, 1.0f};
    fx_t f;
    fx_new(&f, sr, &prm);
    run_blocks(&f, l, r, N, 128);

    const int D = (int)lroundf(delay_time_seconds(0.8f, 1.0f) * sr);
    CHECK(D == 22050, "expected a 0.5 s loop, got %d samples", D);
    CHECK(l[0] == 0.5f, "dry impulse must pass untouched");
    int quiet = 1;
    for (int i = 1; i < D - 10; i++)
        if (l[i] != 0.0f)
            quiet = 0;
    CHECK(quiet, "nothing may be heard before the first echo");
    double prev = 0.0;
    for (int k = 0; k < 4; k++) {
        const int c = D * (k + 1);
        const double s = sum_range(l, c - 60, c + 60);
        const double want = 0.5 * pow(0.5, k);
        printf("  echo %d: area %.5f (want %.5f)\n", k + 1, s, want);
        CHECK(fabs(s - want) < 0.01 * want, "echo %d area %.5f, wanted %.5f", k + 1, s, want);
        if (k > 0)
            CHECK(fabs(s / prev - 0.5) < 0.01, "echo %d should be FDB times echo %d", k + 1, k);
        prev = s;
    }
    CHECK(memcmp(l, r, sizeof l) == 0, "identical channels must give identical output");
    fx_free(&f);
}

static void test_step_response(void)
{
    const float sr = 44100.0f;
    enum { N = 44100 * 2 };
    static float l[N], r[N];
    for (int i = 0; i < N; i++)
        l[i] = r[i] = 0.3f;
    delay_params prm = {0.8f, 1.0f, 0.0f, 0.5f}; /* 0.5 s, no feedback, half the input sent to the tape */
    fx_t f;
    fx_new(&f, sr, &prm);
    run_blocks(&f, l, r, N, 256);
    int D = 22050, ok_before = 1;
    for (int i = 0; i < D - 10; i++)
        if (l[i] != 0.3f)
            ok_before = 0;
    CHECK(ok_before, "before the first echo the output is the dry step");
    printf("  step settles to %.5f (want 0.45000)\n", l[N - 1]);
    CHECK(fabsf(l[N - 1] - 0.45f) < 1e-3f, "step should settle at dry + INPUT * dry");
    fx_free(&f);
}

static float sine_gain(float hz, float speed)
{
    const float sr = 44100.0f;
    enum { N = 44100 * 2 };
    static float l[N], r[N], dry[N];
    for (int i = 0; i < N; i++)
        dry[i] = l[i] = r[i] = 0.4f * sinf(2.0f * 3.14159265f * hz * (float)i / sr);
    delay_params prm = {0.4f, speed, 0.0f, 1.0f};
    fx_t f;
    fx_new(&f, sr, &prm);
    run_blocks(&f, l, r, N, 128);
    for (int i = 0; i < N; i++)
        l[i] -= dry[i]; /* what is left is the echo */
    fx_free(&f);
    return (float)(rms_range(l, N - 20000, N) / rms_range(dry, N - 20000, N));
}

static void test_slow_tape_is_darker(void)
{
    const float lo_fast = sine_gain(500.0f, 1.0f), lo_slow = sine_gain(500.0f, 0.3f);
    const float hi_fast = sine_gain(10000.0f, 1.0f), hi_slow = sine_gain(10000.0f, 0.3f);
    printf("  echo gain: 500 Hz %.3f (full speed) %.3f (0.3);  10 kHz %.3f (full) %.3f (0.3)\n", lo_fast, lo_slow,
           hi_fast, hi_slow);
    CHECK(fabsf(lo_fast - 1.0f) < 0.03f && fabsf(lo_slow - 1.0f) < 0.03f, "lows must pass at any tape speed");
    CHECK(hi_fast > 0.7f, "10 kHz at full speed should be close to open (%.3f)", hi_fast);
    CHECK(hi_slow < 0.6f * hi_fast, "slow tape must darken the echo (%.3f vs %.3f)", hi_slow, hi_fast);
}

static void test_delay_time_law(void)
{
    struct { float t, s, sr; } cases[] = {
        {DELAY_DEFAULT_TIME, DELAY_DEFAULT_SPEED, 44100.0f}, {1.0f, 1.0f, 44100.0f}, {0.0f, 1.0f, 44100.0f},
        {0.0f, 0.0f, 44100.0f},  {0.5f, 0.5f, 48000.0f},  {1.0f, 0.0f, 44100.0f},  {DELAY_DEFAULT_TIME, DELAY_DEFAULT_SPEED, 22050.0f},
        {DELAY_DEFAULT_TIME, DELAY_DEFAULT_SPEED, 192000.0f}, {0.9f, 0.8f, 96000.0f},
    };
    for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        const float sr = cases[c].sr;
        const float want_s = delay_time_seconds(cases[c].t, cases[c].s);
        const int n = (int)(sr * (want_s + 0.5f));
        float *l = (float *)calloc((size_t)n, sizeof(float));
        float *r = (float *)calloc((size_t)n, sizeof(float));
        l[0] = r[0] = 0.8f;
        delay_params prm = {cases[c].t, cases[c].s, 0.0f, 1.0f};
        fx_t f;
        fx_new(&f, sr, &prm);
        run_blocks(&f, l, r, n, 256);
        int at = 1;
        for (int i = 1; i < n; i++)
            if (fabsf(l[i]) > fabsf(l[at]))
                at = i;
        const float got = (float)at / sr;
        printf("  time %.2f speed %.2f @%.0f: wanted %.4f s, echo at %.4f s\n", (double)cases[c].t,
               (double)cases[c].s, (double)sr, (double)want_s, (double)got);
        /* The loop low-pass delays the peak by a few samples at slow tape speeds. */
        CHECK(fabsf(got - want_s) * sr < 12.0f, "delay is %.4f s, wanted %.4f s", (double)got, (double)want_s);
        fx_free(&f);
        free(l);
        free(r);
    }
}

static void test_finite_and_bounded(void)
{
    for (int ri = 0; ri < 5; ri++) {
        const float sr = kRates[ri];
        const int n = (int)(sr * 2.0f);
        float *l = (float *)malloc(sizeof(float) * (size_t)n);
        float *r = (float *)malloc(sizeof(float) * (size_t)n);
        float *dl = (float *)malloc(sizeof(float) * (size_t)n);
        for (int corner = 0; corner < 16; corner++) {
            delay_params prm = {(float)(corner & 1), (float)((corner >> 1) & 1), (float)((corner >> 2) & 1),
                                (float)((corner >> 3) & 1)};
            for (int i = 0; i < n; i++) {
                dl[i] = l[i] = frand();
                r[i] = frand();
            }
            fx_t f;
            fx_new(&f, sr, &prm);
            run_blocks(&f, l, r, n, 128);
            float worst = 0;
            int finite = 1;
            for (int i = 0; i < n; i++) {
                if (!isfinite(l[i]) || !isfinite(r[i]))
                    finite = 0;
                worst = fmaxf(worst, fabsf(l[i] - dl[i]));
            }
            CHECK(finite, "non-finite output (sr %.0f corner %d)", (double)sr, corner);
            /* The tape saturates at 1.0; the cubic read can overshoot a little on noise. */
            CHECK(worst <= 1.3f, "echo above 1.3 (%.3f) at sr %.0f corner %d", (double)worst, (double)sr, corner);
            fx_free(&f);
        }
        free(l);
        free(r);
        free(dl);
    }
}

static void test_feedback_hold_and_decay(void)
{
    const float sr = 44100.0f;
    enum { N = 44100 * 10 };
    static float l[N], r[N];

    /* FDB 1: the tape holds. It stays bounded and does not die out. */
    for (int i = 0; i < N; i++) {
        l[i] = i < 4410 ? 0.9f * frand() : 0.0f;
        r[i] = l[i];
    }
    delay_params hold = {0.0f, 1.0f, 1.0f, 1.0f};
    fx_t f;
    fx_new(&f, sr, &hold);
    run_blocks(&f, l, r, N, 128);
    float pk = peak(l + 4410, N - 4410); /* after the dry burst, so this is the tape alone */
    double tail = rms_range(l, N - 44100, N);
    printf("  FDB 1 after 10 s: peak %.3f, rms of the last second %.4f\n", (double)pk, tail);
    CHECK(pk <= 1.3f && isfinite(pk), "held tape must stay bounded (%.3f)", (double)pk);
    CHECK(tail > 0.01, "FDB 1 should keep ringing (rms %.5f)", tail);
    fx_free(&f);

    /* FDB 0.5: it decays, and reaches exact zero (no denormal tail). */
    for (int i = 0; i < N; i++) {
        l[i] = i < 4410 ? 0.9f * frand() : 0.0f;
        r[i] = l[i];
    }
    delay_params decay = {0.0f, 1.0f, 0.5f, 1.0f};
    fx_new(&f, sr, &decay);
    run_blocks(&f, l, r, N, 128);
    int zero = 1;
    for (int i = N - 44100; i < N; i++)
        if (l[i] != 0.0f || r[i] != 0.0f)
            zero = 0;
    CHECK(zero, "echoes must decay to exact zero");
    fx_free(&f);
}

static void test_block_size_independence(void)
{
    enum { N = 30000 };
    static float in_l[N], in_r[N], ref_l[N], ref_r[N], l[N], r[N];
    for (int i = 0; i < N; i++) {
        in_l[i] = 0.5f * frand();
        in_r[i] = 0.5f * frand();
    }
    delay_params prm = {0.1f, 0.7f, 0.6f, 0.8f};
    const int blocks[] = {128, 1, 7, 5000};
    for (int b = 0; b < 4; b++) {
        memcpy(l, in_l, sizeof l);
        memcpy(r, in_r, sizeof r);
        fx_t f;
        fx_new(&f, 44100.0f, &prm);
        run_blocks(&f, l, r, N, blocks[b]);
        fx_free(&f);
        if (b == 0) {
            memcpy(ref_l, l, sizeof l);
            memcpy(ref_r, r, sizeof r);
        } else {
            CHECK(memcmp(ref_l, l, sizeof l) == 0 && memcmp(ref_r, r, sizeof r) == 0,
                  "block size %d must give the same samples as 128", blocks[b]);
        }
    }
}

static void test_channels_are_independent(void)
{
    /* The mono path in the plugins runs the left signal against a scratch right channel. It only works if the left
     * output does not depend on what is in the right input. */
    enum { N = 20000 };
    static float a_l[N], a_r[N], b_l[N], b_r[N];
    for (int i = 0; i < N; i++) {
        a_l[i] = b_l[i] = 0.5f * frand();
        a_r[i] = 0.5f * frand();
        b_r[i] = b_l[i];
    }
    delay_params prm = {0.05f, 0.9f, 0.7f, 1.0f};
    fx_t f;
    fx_new(&f, 44100.0f, &prm);
    run_blocks(&f, a_l, a_r, N, 128);
    fx_free(&f);
    fx_new(&f, 44100.0f, &prm);
    run_blocks(&f, b_l, b_r, N, 128);
    fx_free(&f);
    CHECK(memcmp(a_l, b_l, sizeof a_l) == 0, "left output must not depend on the right input");
}

static void test_param_sweep_no_clicks(void)
{
    const float sr = 44100.0f;
    enum { N = 44100 * 6 };
    static float l[N], r[N];
    for (int i = 0; i < N; i++)
        l[i] = r[i] = 0.4f * sinf(2.0f * 3.14159265f * 220.0f * (float)i / sr);
    /* FDB stays low here. With it near 1 the echoes recirculate through the pitch changes and build real high
     * frequency content (a rising or falling tape spiral), which is the effect working, not a click. */
    delay_params prm = {0.3f, 0.5f, 0.25f, 1.0f};
    fx_t f;
    fx_new(&f, sr, &prm);
    for (int i = 0; i < N; i += 128) {
        const int c = N - i < 128 ? N - i : 128;
        if ((i / 128) % 40 == 0) { /* a hard jump every 40 blocks, anywhere in the TIME and SPEED range */
            prm.time = 0.5f * (frand() + 1.0f);
            prm.speed = 0.5f * (frand() + 1.0f);
            delay_set_params(&f.d, &prm);
        }
        delay_process(&f.d, l + i, r + i, c);
    }
    float worst = 0;
    for (int i = 1; i < N; i++)
        worst = fmaxf(worst, fabsf(l[i] - l[i - 1]));
    printf("  largest sample step with TIME and SPEED jumping every 0.12 s: %.4f\n", (double)worst);
    CHECK(worst < 0.08f, "parameter jumps caused a discontinuity (%.4f)", (double)worst);
    fx_free(&f);
}

static void test_delay_glide_is_bounded(void)
{
    const float sr = 44100.0f;
    float l[1], r[1];
    delay_params prm = {0.0f, 1.0f, 0.0f, 0.0f};
    fx_t f;
    fx_new(&f, sr, &prm);
    prm.time = 1.0f; /* 31 ms to 1 s in one step */
    delay_set_params(&f.d, &prm);
    double lo = 1e9, hi = -1e9, prev = f.d.d_cur;
    for (int i = 0; i < (int)(sr * 4.0f); i++) {
        l[0] = r[0] = 0.0f;
        delay_process(&f.d, l, r, 1);
        const double dd = f.d.d_cur - prev;
        lo = dd < lo ? dd : lo;
        hi = dd > hi ? dd : hi;
        prev = f.d.d_cur;
    }
    printf("  delay change per sample while gliding 31 ms -> 1 s: %.3f .. %.3f; settled at %.4f s\n", lo, hi, (double)delay_current_seconds(&f.d));
    CHECK(hi <= 0.5001 && lo >= -1.0001, "glide must stay within one octave of pitch change (%.3f .. %.3f)", lo, hi);
    CHECK(fabsf(delay_current_seconds(&f.d) - 1.0f) < 0.01f, "delay must settle on the target");
    fx_free(&f);
}

static void test_reset_and_bad_input(void)
{
    enum { N = 44100 };
    static float l[N], r[N];
    delay_params prm = {0.0f, 1.0f, 0.9f, 1.0f};
    fx_t f;
    fx_new(&f, 44100.0f, &prm);
    for (int i = 0; i < N; i++)
        l[i] = r[i] = frand();
    run_blocks(&f, l, r, N, 128);
    delay_reset(&f.d);
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    run_blocks(&f, l, r, N, 128);
    CHECK(peak(l, N) == 0.0f && peak(r, N) == 0.0f, "reset must clear the tape");

    /* NaN, infinity and huge values on the input must not get stored. Only the dry samples themselves are bad. */
    delay_reset(&f.d);
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    l[10] = NAN;
    l[11] = INFINITY;
    l[12] = -INFINITY;
    l[13] = 1e30f;
    r[10] = NAN;
    run_blocks(&f, l, r, N, 128);
    int clean = 1;
    for (int i = 14; i < N; i++)
        if (l[i] != 0.0f || r[i] != 0.0f)
            clean = 0;
    CHECK(clean, "bad input samples must not poison the feedback loop");
    fx_free(&f);
}

static void test_activity(void)
{
    const float sr = 44100.0f;
    enum { N = 44100 };
    static float l[N], r[N];
    delay_params prm = {0.3f, 1.0f, 0.4f, 1.0f};
    fx_t f;
    fx_new(&f, sr, &prm);
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    delay_process(&f.d, l, r, 4096);
    const float quiet = delay_take_activity(&f.d);
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    l[0] = r[0] = 0.8f;
    delay_process(&f.d, l, r, N);
    const float hit = delay_take_activity(&f.d);
    const float again = delay_take_activity(&f.d);
    printf("  activity: silence %.3f, impulse and its echoes %.3f, after take %.3f\n", (double)quiet, (double)hit,
           (double)again);
    CHECK(quiet == 0.0f, "silence must read as no activity (%.3f)", (double)quiet);
    CHECK(hit > 0.5f, "an echo of a 0.8 impulse must read as strong activity (%.3f)", (double)hit);
    CHECK(again == 0.0f, "taking activity must clear it");
    fx_free(&f);
}

static void test_tail_length(void)
{
    delay_params a = {0.5f, 1.0f, 0.0f, 1.0f}, b = {0.5f, 1.0f, 0.5f, 1.0f}, c = {0.5f, 1.0f, 1.0f, 1.0f};
    const float t = delay_time_seconds(0.5f, 1.0f);
    const float ta = delay_tail_seconds(&a), tb = delay_tail_seconds(&b), tc = delay_tail_seconds(&c);
    printf("  tail: FDB 0 %.2f s, FDB 0.5 %.2f s, FDB 1 %.1f s (delay %.3f s)\n", (double)ta, (double)tb,
           (double)tc, (double)t);
    CHECK(fabsf(ta - t) < 1e-4f, "no feedback: one delay");
    CHECK(tb > 5.0f * t && tb < 30.0f, "half feedback rings for several repeats");
    CHECK(tc == 30.0f, "full feedback reports the cap");
}

int main(void)
{
    test_parameter_maps();
    test_input_zero_is_bit_exact();
    test_impulse_echoes();
    test_step_response();
    test_slow_tape_is_darker();
    test_delay_time_law();
    test_finite_and_bounded();
    test_feedback_hold_and_decay();
    test_block_size_independence();
    test_channels_are_independent();
    test_param_sweep_no_clicks();
    test_delay_glide_is_bounded();
    test_reset_and_bad_input();
    test_activity();
    test_tail_length();
    if (failures) {
        printf("%d FAILED\n", failures);
        return 1;
    }
    printf("PASSED\n");
    return 0;
}
