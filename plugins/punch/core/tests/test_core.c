/* Behavioral tests for the Punch core. Build and run with core/tests/run.sh */
#include "../punch_core.h"

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

/* Synthetic kick: 90 Hz sine burst with a fast exponential decay, then a snare-ish noise burst. */
static void make_drums(float *l, float *r, int n, float sr)
{
    memset(l, 0, sizeof(float) * (size_t)n);
    memset(r, 0, sizeof(float) * (size_t)n);
    for (int hit = 0; hit < 4; hit++) {
        int start = (int)(sr * 0.25f * (float)hit);
        for (int i = 0; start + i < n && i < (int)(sr * 0.2f); i++) {
            float t = (float)i / sr;
            float env = expf(-t * 25.0f);
            float s = 0.5f * env * sinf(2.0f * 3.14159265f * 90.0f * t);
            if (hit % 2 == 1)
                s = 0.35f * env * frand();
            l[start + i] = s;
            r[start + i] = s * 0.9f;
        }
    }
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

static void run(punch_t *p, const punch_params *prm, float *l, float *r, int n)
{
    punch_set_params(p, prm);
    punch_reset(p);
    for (int i = 0; i < n; i += 128) {
        int c = n - i < 128 ? n - i : 128;
        punch_process(p, l + i, r + i, c);
    }
}

static void test_power_zero_is_bit_exact(void)
{
    enum { N = 8192 };
    static float l[N], r[N], l0[N], r0[N];
    for (int i = 0; i < N; i++) {
        l0[i] = l[i] = frand();
        r0[i] = r[i] = frand();
    }
    punch_t p;
    punch_init(&p, 44100.0f);
    punch_params prm = {0.5f, 1.0f, 24, 0.0f};
    run(&p, &prm, l, r, N);
    CHECK(memcmp(l, l0, sizeof l) == 0 && memcmp(r, r0, sizeof r) == 0, "power 0 must leave audio untouched");
}

static void test_stays_finite_and_bounded(void)
{
    enum { N = 44100 };
    static float l[N], r[N];
    const float rates[] = {44100.0f, 48000.0f, 96000.0f};
    for (int ri = 0; ri < 3; ri++) {
        for (int f = 0; f <= 1; f++)
            for (int pu = 0; pu <= 1; pu++)
                for (int ro = 1; ro <= 24; ro += 23)
                    for (int po = 0; po <= 1; po++) {
                        for (int i = 0; i < N; i++) {
                            l[i] = frand();
                            r[i] = frand();
                        }
                        punch_t p;
                        punch_init(&p, rates[ri]);
                        punch_params prm = {(float)f, (float)pu, ro, (float)po};
                        run(&p, &prm, l, r, N);
                        float pk = fmaxf(peak(l, N), peak(r, N));
                        CHECK(isfinite(pk), "non-finite output (sr %.0f f%d p%d r%d w%d)", rates[ri], f, pu, ro, po);
                        CHECK(pk <= 1.0001f, "peak %.4f above 1.0 for input within +-1 (sr %.0f f%d p%d r%d w%d)", pk,
                              rates[ri], f, pu, ro, po);
                    }
    }
}

static void test_punch_raises_crest_factor(void)
{
    enum { N = 44100 };
    static float l[N], r[N], l1[N], r1[N];
    make_drums(l, r, N, 44100.0f);
    memcpy(l1, l, sizeof l);
    memcpy(r1, r, sizeof r);
    punch_t p;
    punch_init(&p, 44100.0f);
    punch_params off = {0.2f, 0.0f, 12, 1.0f};
    punch_params on = {0.2f, 1.0f, 12, 1.0f};
    run(&p, &off, l, r, N);
    run(&p, &on, l1, r1, N);
    /* push the transient of each hit up against the body: compare the first 20 ms of a hit
     * with the following 100 ms */
    int n20 = 882, n100 = 4410;
    float body_off = rms(l + n20 * 2, n100), body_on = rms(l1 + n20 * 2, n100);
    float att_off = rms(l, n20), att_on = rms(l1, n20);
    float ratio_off = att_off / body_off, ratio_on = att_on / body_on;
    printf("  attack/body ratio: punch0 %.3f  punch1 %.3f\n", ratio_off, ratio_on);
    CHECK(ratio_on > ratio_off * 1.05f, "PUNCH should raise the attack to body ratio");
}

static void test_rounds_change_the_sound(void)
{
    enum { N = 44100 };
    static float a[N], ar[N], b[N], br[N];
    make_drums(a, ar, N, 44100.0f);
    memcpy(b, a, sizeof a);
    memcpy(br, ar, sizeof ar);
    punch_t p;
    punch_init(&p, 44100.0f);
    punch_params one = {0.2f, 1.0f, 1, 1.0f};
    punch_params many = {0.2f, 1.0f, 24, 1.0f};
    run(&p, &one, a, ar, N);
    run(&p, &many, b, br, N);
    double d = 0;
    for (int i = 0; i < N; i++)
        d += (double)(a[i] - b[i]) * (a[i] - b[i]);
    float rd = (float)sqrt(d / N);
    printf("  rms difference between 1 and 24 rounds: %.5f\n", rd);
    CHECK(rd > 0.002f, "ROUNDS should audibly change the result");
}

static void test_param_sweep_no_clicks(void)
{
    enum { N = 44100 };
    static float l[N], r[N];
    for (int i = 0; i < N; i++)
        l[i] = r[i] = 0.4f * sinf(2.0f * 3.14159265f * 220.0f * (float)i / 44100.0f);
    punch_t p;
    punch_init(&p, 44100.0f);
    punch_params prm = {0.3f, 0.5f, 12, 1.0f};
    punch_set_params(&p, &prm);
    punch_reset(&p);
    float worst = 0, prev = 0;
    for (int i = 0; i < N; i += 128) {
        prm.rounds = 1 + (i / 128) % 24;
        prm.freq = (float)((i / 128) % 7) / 7.0f;
        punch_set_params(&p, &prm);
        punch_process(&p, l + i, r + i, N - i < 128 ? N - i : 128);
    }
    for (int i = 1; i < N; i++) {
        worst = fmaxf(worst, fabsf(l[i] - l[i - 1]));
        prev = l[i];
    }
    (void)prev;
    printf("  largest sample step with parameters changing each block: %.4f\n", worst);
    CHECK(worst < 0.2f, "parameter changes caused a discontinuity (%.4f)", worst);
}

static void test_activity_follows_transients(void)
{
    enum { N = 8192 };
    static float l[N], r[N];
    punch_t p;
    punch_params prm = {PUNCH_DEFAULT_FREQ, PUNCH_DEFAULT_PUNCH, PUNCH_DEFAULT_ROUNDS, PUNCH_DEFAULT_POWER};
    punch_init(&p, 44100.0f);
    punch_set_params(&p, &prm);
    punch_reset(&p);

    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    punch_process(&p, l, r, N);
    const float quiet = punch_take_activity(&p);

    for (int i = 0; i < N; i++)
        l[i] = r[i] = (i % 2048) < 24 ? 0.8f * (i % 2 ? 1.0f : -1.0f) : 0.0f;
    punch_process(&p, l, r, N);
    const float hit = punch_take_activity(&p);
    const float again = punch_take_activity(&p);

    printf("  activity: silence %.3f, clicks %.3f, after take %.3f\n", quiet, hit, again);
    CHECK(quiet < 0.01f, "silence must read as no activity (%.3f)", quiet);
    CHECK(hit > 0.5f, "clicks must read as strong activity (%.3f)", hit);
    CHECK(again == 0.0f, "taking activity must clear it");
}

int main(void)
{
    test_power_zero_is_bit_exact();
    test_stays_finite_and_bounded();
    test_punch_raises_crest_factor();
    test_rounds_change_the_sound();
    test_param_sweep_no_clicks();
    test_activity_follows_transients();
    CHECK(punch_rounds_from_norm(punch_rounds_to_norm(19)) == 19, "rounds mapping must round trip");
    CHECK(punch_rounds_from_norm(0.0f) == 1 && punch_rounds_from_norm(1.0f) == 24, "rounds range");
    if (failures) {
        printf("%d FAILED\n", failures);
        return 1;
    }
    printf("PASSED\n");
    return 0;
}
