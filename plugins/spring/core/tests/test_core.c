/* Behavioral tests for the Spring core. Build and run with core/tests/run.sh */
#include "../spring_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static unsigned rng_state = 12345u;
static float frand(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) / (float)(1u << 24) * 2.0f - 1.0f;
}

static spring_t *new_spring(float sr, float tone, float turns, float damp, float level)
{
    spring_t *s = (spring_t *)malloc(sizeof *s);
    spring_init(s, sr);
    spring_params p = {tone, turns, damp, level};
    spring_set_params(s, &p);
    spring_reset(s);
    return s;
}

static int all_finite(const float *x, int n)
{
    for (int i = 0; i < n; i++)
        if (!isfinite(x[i]))
            return 0;
    return 1;
}

static float peak(const float *x, int n)
{
    float p = 0;
    for (int i = 0; i < n; i++)
        p = fmaxf(p, fabsf(x[i]));
    return p;
}

static double energy(const float *x, int a, int b)
{
    double e = 0;
    for (int i = a; i < b; i++)
        e += (double)x[i] * x[i];
    return e;
}

/* Wet-only signal of an impulse response: run an impulse through the tank, subtract the dry impulse. */
static float *impulse_wet(float sr, float tone, float turns, float damp, float level, int n)
{
    spring_t *s = new_spring(sr, tone, turns, damp, level);
    float *l = (float *)calloc((size_t)n, sizeof(float));
    float *r = (float *)calloc((size_t)n, sizeof(float));
    l[0] = r[0] = 1.0f;
    spring_process(s, l, r, n);
    l[0] -= 1.0f;
    free(r);
    free(s);
    return l;
}

/* DFT of x at one frequency: returns phase in radians via re/im. */
static void dft_at(const float *x, int n, double f, double sr, double *re, double *im)
{
    double a = 0, b = 0;
    const double w = 2.0 * PI * f / sr;
    for (int i = 0; i < n; i++) {
        a += x[i] * cos(w * i);
        b -= x[i] * sin(w * i);
    }
    *re = a;
    *im = b;
}

/* Group delay in samples from the phase difference of two nearby bins. */
static double group_delay(const float *ir, int n, double f, double sr)
{
    const double df = 8.0;
    double r1, i1, r2, i2;
    dft_at(ir, n, f - df, sr, &r1, &i1);
    dft_at(ir, n, f + df, sr, &r2, &i2);
    double dphi = atan2(i2, r2) - atan2(i1, r1);
    while (dphi > PI)
        dphi -= 2 * PI;
    while (dphi < -PI)
        dphi += 2 * PI;
    return -dphi / (2.0 * PI * 2.0 * df / sr);
}

static void test_defaults(void)
{
    /* factory values 8000, 15000, 2000, 20000 out of 32767 */
    CHECK(fabsf(SPRING_DEFAULT_TONE - 8000.0f / 32767.0f) < 0.001f, "tone default");
    CHECK(fabsf(SPRING_DEFAULT_TURNS - 15000.0f / 32767.0f) < 0.001f, "turns default");
    CHECK(fabsf(SPRING_DEFAULT_DAMP - 2000.0f / 32767.0f) < 0.001f, "damp default");
    CHECK(fabsf(SPRING_DEFAULT_LEVEL - 20000.0f / 32767.0f) < 0.001f, "level default");
}

static void test_chain_is_allpass_with_chirp(void)
{
    const int n = 8192, m = 4;
    const float a = -0.6f;
    spring_disp *d = (spring_disp *)malloc(sizeof *d);
    spring_disp_init(d, m);
    float *ir = (float *)malloc(sizeof(float) * (size_t)n);
    for (int i = 0; i < n; i++)
        ir[i] = spring_disp_step(d, i == 0 ? 1.0f : 0.0f, a);

    const double e = energy(ir, 0, n);
    CHECK(fabs(e - 1.0) < 0.01, "chain impulse energy %.4f, expected 1 (all-pass)", e);

    const double freqs[] = {100, 300, 1000, 2000, 4000, 8000};
    for (int k = 0; k < 6; k++) {
        double re, im;
        dft_at(ir, n, freqs[k], 44100.0, &re, &im);
        const double mag = sqrt(re * re + im * im);
        CHECK(fabs(mag - 1.0) < 0.02, "chain magnitude at %.0f Hz is %.4f, expected 1", freqs[k], mag);
    }

    /* Group delay of one stretched section: M (1 - a^2) / (1 + 2 a cos(M w) + a^2). The chain is 32 of them. */
    const double f_lo = 300, f_hi = 2000;
    const double g_lo = group_delay(ir, n, f_lo, 44100.0), g_hi = group_delay(ir, n, f_hi, 44100.0);
    for (int k = 0; k < 2; k++) {
        const double f = k ? f_hi : f_lo;
        const double w = 2.0 * PI * f / 44100.0;
        const double want = SPRING_STAGES * m * (1 - a * a) / (1 + 2 * a * cos(m * w) + a * a);
        const double got = k ? g_hi : g_lo;
        CHECK(fabs(got - want) < 0.05 * want, "group delay at %.0f Hz is %.1f samples, formula says %.1f", f, got, want);
    }
    CHECK(g_lo > 2.0 * g_hi, "chirp: 300 Hz should arrive well after 2 kHz (%.1f vs %.1f samples)", g_lo, g_hi);
    printf("  chain group delay: 300 Hz %.1f samples (%.2f ms), 2 kHz %.1f samples (%.2f ms)\n", g_lo,
           g_lo / 44.1, g_hi, g_hi / 44.1);
    free(ir);

    /* the three-at-once path used by the tank gives exactly the same samples as the single chain */
    spring_disp *three = (spring_disp *)malloc(3 * sizeof *three);
    spring_disp *one = (spring_disp *)malloc(sizeof *one);
    for (int i = 0; i < 3; i++)
        spring_disp_init(&three[i], m);
    spring_disp_init(one, m);
    int same = 1;
    for (int i = 0; i < 2000; i++) {
        const float in[3] = {frand(), 0.5f * frand(), 0.1f};
        float out3[3];
        spring_disp_step3(three, in, out3, a);
        if (out3[0] != spring_disp_step(one, in[0], a))
            same = 0;
    }
    CHECK(same, "spring_disp_step3 differs from spring_disp_step");
    free(three);
    free(one);
    free(d);
}

static void test_bypass_bit_exact(void)
{
    /* LEVEL 0 from the start: audio is untouched, bit for bit. */
    const int n = 20000;
    spring_t *s = new_spring(44100.0f, 0.5f, 0.5f, 0.2f, 0.0f);
    float *l = (float *)malloc(sizeof(float) * (size_t)n), *r = (float *)malloc(sizeof(float) * (size_t)n);
    float *l0 = (float *)malloc(sizeof(float) * (size_t)n), *r0 = (float *)malloc(sizeof(float) * (size_t)n);
    for (int i = 0; i < n; i++)
        l[i] = l0[i] = 0.8f * frand(), r[i] = r0[i] = 0.8f * frand();
    spring_process(s, l, r, n);
    CHECK(memcmp(l, l0, sizeof(float) * (size_t)n) == 0 && memcmp(r, r0, sizeof(float) * (size_t)n) == 0,
          "LEVEL 0 changed the audio");
    free(s);

    /* Drive the tank, then turn LEVEL to 0 with a short DAMP: once the tail is gone the audio is bit-exact again. */
    s = new_spring(44100.0f, 0.5f, 0.5f, 1.0f, 1.0f);
    const int big = 44100 * 6;
    float *bl = (float *)malloc(sizeof(float) * (size_t)big), *br = (float *)malloc(sizeof(float) * (size_t)big);
    float *b0 = (float *)malloc(sizeof(float) * (size_t)big);
    for (int i = 0; i < big; i++)
        bl[i] = br[i] = b0[i] = i < 44100 ? 0.5f * frand() : 0.0f;
    spring_process(s, bl, br, 44100);
    spring_params p = {0.5f, 0.5f, 1.0f, 0.0f};
    spring_set_params(s, &p);
    spring_process(s, bl + 44100, br + 44100, big - 44100);
    int exact = 1;
    for (int i = big - 2000; i < big; i++)
        if (bl[i] != b0[i])
            exact = 0;
    CHECK(exact, "after LEVEL went to 0 and the tail died, audio is still not bit-exact dry");
    free(bl); free(br); free(b0); free(l); free(r); free(l0); free(r0); free(s);
}

static void test_stability_and_headroom(void)
{
    const float rates[] = {22050, 44100, 48000, 96000, 192000};
    const float corners[][4] = {{0, 0, 0, 1}, {1, 1, 0, 1}, {1, 0, 0, 1}, {0, 1, 0, 1}, {1, 1, 1, 1},
                                {0.244f, 0.458f, 0.061f, 0.61f}, {0.5f, 0.5f, 0.0f, 1.0f}};
    float worst = 0.0f;
    for (int ri = 0; ri < 5; ri++) {
        for (int ci = 0; ci < 7; ci++) {
            const float *c = corners[ci];
            spring_t *s = new_spring(rates[ri], c[0], c[1], c[2], c[3]);
            const int n = (int)(rates[ri] * 1.5f);
            float *l = (float *)malloc(sizeof(float) * (size_t)n), *r = (float *)malloc(sizeof(float) * (size_t)n);
            for (int i = 0; i < n; i++)
                l[i] = r[i] = 0.9f * frand();
            spring_process(s, l, r, n);
            CHECK(all_finite(l, n) && all_finite(r, n), "NaN or Inf at %.0f Hz corner %d", rates[ri], ci);
            const float pk = fmaxf(peak(l, n), peak(r, n));
            worst = fmaxf(worst, pk);
            CHECK(pk < 6.0f, "peak %.2f at %.0f Hz corner %d", pk, rates[ri], ci);

            /* input stops: the tank must die out */
            const int tail = (int)(rates[ri] * 12.0f);
            float *tl = (float *)calloc((size_t)tail, sizeof(float)), *tr = (float *)calloc((size_t)tail, sizeof(float));
            spring_process(s, tl, tr, tail);
            const double last = energy(tl, tail - (int)rates[ri], tail) / rates[ri];
            CHECK(all_finite(tl, tail) && last < 1e-6, "tail did not die at %.0f Hz corner %d (rms^2 %.3g)", rates[ri],
                  ci, last);
            free(tl); free(tr); free(l); free(r); free(s);
        }
    }
    printf("  worst peak for 0.9 full-scale noise across rates and corners: %.2f\n", worst);
}

static void test_damp_shortens_tail(void)
{
    const int n = 44100 * 4;
    double e_prev = 1e30;
    const float damps[] = {0.0f, 0.4f, 0.7f, 1.0f};
    for (int k = 0; k < 4; k++) {
        float *w = impulse_wet(44100.0f, 0.5f, 0.5f, damps[k], 1.0f, n);
        const double late = energy(w, 44100, 2 * 44100);
        printf("  damp %.1f: energy in second 1 = %.3g\n", damps[k], late);
        CHECK(late < e_prev, "DAMP %.1f tail (%.3g) is not shorter than the previous (%.3g)", damps[k], late, e_prev);
        e_prev = late;
        free(w);
    }
    /* decay rate at the default: compare second 1 to second 2 */
    float *w = impulse_wet(44100.0f, SPRING_DEFAULT_TONE, SPRING_DEFAULT_TURNS, SPRING_DEFAULT_DAMP,
                           SPRING_DEFAULT_LEVEL, n);
    const double e1 = energy(w, 44100, 2 * 44100), e2 = energy(w, 2 * 44100, 3 * 44100);
    const double db_per_s = 10.0 * log10(e2 / e1);
    printf("  default decay: %.1f dB per second (rt60 formula says %.2f s for the bass)\n", db_per_s,
           spring_rt60(SPRING_DEFAULT_DAMP));
    CHECK(db_per_s < -8.0 && db_per_s > -90.0, "default decay %.1f dB/s out of range", db_per_s);
    free(w);
}

static double hf_ratio(const float *x, int a, int b)
{
    /* energy of the first difference over energy of the signal: grows with brightness */
    double d = 0, e = 0;
    for (int i = a + 1; i < b; i++) {
        const double df = x[i] - x[i - 1];
        d += df * df;
        e += (double)x[i] * x[i];
    }
    return d / (e + 1e-30);
}

static void test_tone_brightens(void)
{
    /* TONE sets the low-pass inside the loops, so it shows in the tail (after a few trips), not in the first return. */
    const int n = 44100 * 2;
    float *dark = impulse_wet(44100.0f, 0.0f, 0.5f, 0.3f, 1.0f, n);
    float *bright = impulse_wet(44100.0f, 1.0f, 0.5f, 0.3f, 1.0f, n);
    const double rd = hf_ratio(dark, 22050, n), rb = hf_ratio(bright, 22050, n);
    printf("  tail brightness (first-difference energy ratio): tone 0 %.4f, tone 1 %.4f\n", rd, rb);
    CHECK(rb > 2.0 * rd, "TONE 1 (%.4f) is not clearly brighter than TONE 0 (%.4f)", rb, rd);
    free(dark); free(bright);
}

static void test_turns_spreads_the_hit(void)
{
    /* The first return of the tank: with more TURNS the chirp smears it over a longer time. */
    const int n = 44100 / 4;
    double spread[2];
    const float turns[2] = {0.0f, 1.0f};
    for (int k = 0; k < 2; k++) {
        float *w = impulse_wet(44100.0f, 1.0f, turns[k], 1.0f, 1.0f, n);
        double e = 0, t = 0, t2 = 0;
        for (int i = 0; i < 2400; i++) { /* first 54 ms: before the second trip of the shortest spring */
            const double p = (double)w[i] * w[i];
            e += p;
            t += p * i;
            t2 += p * (double)i * i;
        }
        const double mean = t / e;
        spread[k] = sqrt(t2 / e - mean * mean) / 44.1;
        free(w);
    }
    printf("  time spread of the first returns: TURNS 0 %.2f ms, TURNS 1 %.2f ms\n", spread[0], spread[1]);
    CHECK(spread[1] > spread[0] + 1.0, "TURNS 1 spread %.2f ms not larger than TURNS 0 %.2f ms", spread[1], spread[0]);
}

static void test_block_size_independence(void)
{
    /* Same input, a parameter move at sample 10000, different block sizes (all divide 10000): bit-identical output. */
    const int n = 20000;
    const int sizes[] = {1, 8, 100, 2000};
    float *ref = NULL;
    for (int k = 0; k < 4; k++) {
        spring_t *s = new_spring(44100.0f, 0.4f, 0.7f, 0.2f, 0.8f);
        float *l = (float *)malloc(sizeof(float) * (size_t)n), *r = (float *)malloc(sizeof(float) * (size_t)n);
        rng_state = 99;
        for (int i = 0; i < n; i++)
            l[i] = r[i] = 0.5f * frand();
        for (int pos = 0; pos < n; pos += sizes[k]) {
            if (pos == n / 2) {
                spring_params p = {0.9f, 0.2f, 0.5f, 0.3f};
                spring_set_params(s, &p);
            }
            spring_process(s, l + pos, r + pos, sizes[k]);
        }
        if (!ref) {
            ref = l;
        } else {
            CHECK(memcmp(ref, l, sizeof(float) * (size_t)n) == 0, "block size %d gives different samples than block size 1",
                  sizes[k]);
            free(l);
        }
        free(r);
        free(s);
    }
    free(ref);
}

static void test_no_clicks_on_param_moves(void)
{
    /* Mode 0: steady at the loudest of the two states. Mode 1: all four knobs flip between two extremes every block. */
    const int n = 44100 * 2, blk = 128;
    float step[2];
    for (int mode = 0; mode < 2; mode++) {
        spring_t *s = new_spring(44100.0f, 1.0f, 1.0f, 0.0f, 1.0f);
        float *l = (float *)malloc(sizeof(float) * (size_t)n), *r = (float *)malloc(sizeof(float) * (size_t)n);
        for (int i = 0; i < n; i++)
            l[i] = r[i] = 0.3f * sinf(2.0f * (float)PI * 220.0f * (float)i / 44100.0f);
        for (int pos = 0; pos + blk <= n; pos += blk) {
            if (mode == 1) {
                const int k = (pos / blk) & 1;
                spring_params p = {k ? 1.0f : 0.0f, k ? 1.0f : 0.0f, k ? 0.0f : 1.0f, k ? 1.0f : 0.2f};
                spring_set_params(s, &p);
            }
            spring_process(s, l + pos, r + pos, blk);
        }
        float mx = 0;
        for (int i = 1; i < n; i++)
            mx = fmaxf(mx, fabsf(l[i] - l[i - 1]));
        step[mode] = mx;
        free(l); free(r); free(s);
    }
    printf("  largest sample step: steady %.4f, all four knobs flipped every 128 samples %.4f\n", step[0], step[1]);
    CHECK(step[1] < 1.25f * step[0], "sample step %.4f while the knobs jump, steady is %.4f", step[1], step[0]);
}

static void test_stereo_and_mono(void)
{
    const int n = 44100 * 2;
    spring_t *s = new_spring(44100.0f, 0.5f, 0.5f, 0.3f, 1.0f);
    float *l = (float *)calloc((size_t)n, sizeof(float)), *r = (float *)calloc((size_t)n, sizeof(float));
    float *l0 = (float *)calloc((size_t)n, sizeof(float));
    for (int i = 0; i < n; i++)
        l[i] = l0[i] = i < 4410 ? 0.5f * frand() : 0.0f; /* the same signal on both sides */
    memcpy(r, l, sizeof(float) * (size_t)n);
    spring_process(s, l, r, n);
    double ll = 0, rr = 0, lr = 0;
    for (int i = 4410; i < n; i++) {
        ll += (double)l[i] * l[i];
        rr += (double)r[i] * r[i];
        lr += (double)l[i] * r[i];
    }
    const double corr = lr / sqrt(ll * rr);
    printf("  L/R correlation of the wet tail: %.3f\n", corr);
    CHECK(corr < 0.9 && corr > -0.9, "wet channels are not decorrelated (%.3f)", corr);
    CHECK(ll > 0 && rr > 0 && fabs(10 * log10(ll / rr)) < 3.0, "wet L/R balance off: %.2f dB", 10 * log10(ll / rr));

    /* mono-in: energy only on the left, right silent. The tank sums to mono, output stays finite and non-silent. */
    memset(r, 0, sizeof(float) * (size_t)n);
    spring_reset(s);
    memcpy(l, l0, sizeof(float) * (size_t)n);
    spring_process(s, l, r, n);
    CHECK(all_finite(l, n) && all_finite(r, n) && energy(r, 4410, n) > 1e-8, "left-only input broke the right channel");
    free(l); free(r); free(l0); free(s);
}

static void test_sample_rate_scaling(void)
{
    /* The first return should land at about the same time in milliseconds at every rate. */
    const float rates[] = {22050, 44100, 96000, 192000};
    double t1[4];
    for (int k = 0; k < 4; k++) {
        const int n = (int)(rates[k] * 0.2f);
        float *w = impulse_wet(rates[k], 0.8f, 0.5f, 0.5f, 1.0f, n);
        const double total = energy(w, 0, (int)(rates[k] * 0.12f)); /* before the second trips */
        double acc = 0;
        int i = 0;
        for (; i < n; i++) {
            acc += (double)w[i] * w[i];
            if (acc > 0.1 * total)
                break;
        }
        t1[k] = 1000.0 * i / rates[k];
        free(w);
    }
    printf("  time to 10%% of the first-return energy: %.1f / %.1f / %.1f / %.1f ms at 22.05 / 44.1 / 96 / 192 kHz\n",
           t1[0], t1[1], t1[2], t1[3]);
    for (int k = 0; k < 4; k++)
        CHECK(fabs(t1[k] - t1[1]) < 3.0, "rate %.0f: first return at %.1f ms vs %.1f ms at 44.1 kHz", rates[k], t1[k], t1[1]);
}

static void test_activity_and_reset(void)
{
    spring_t *s = new_spring(44100.0f, 0.5f, 0.5f, 0.5f, 1.0f);
    const int n = 44100;
    float *l = (float *)calloc((size_t)n, sizeof(float)), *r = (float *)calloc((size_t)n, sizeof(float));
    CHECK(spring_take_activity(s) == 0.0f, "activity before any audio");
    for (int i = 0; i < 2000; i++)
        l[i] = r[i] = 0.5f * frand();
    spring_process(s, l, r, n);
    const float a = spring_take_activity(s);
    CHECK(a > 0.01f && a < 3.0f, "activity after a burst is %.4f", a);
    CHECK(spring_take_activity(s) == 0.0f, "activity was not cleared by the read");

    /* reset: same input, same output as a fresh instance, and a quiet tank */
    spring_reset(s);
    memset(l, 0, sizeof(float) * (size_t)n);
    memset(r, 0, sizeof(float) * (size_t)n);
    spring_process(s, l, r, n);
    CHECK(peak(l, n) < 1e-20f && peak(r, n) < 1e-20f, "tank is not silent after reset (%.3g)", peak(l, n));
    free(l); free(r); free(s);
}

int main(void)
{
    test_defaults();
    test_chain_is_allpass_with_chirp();
    test_bypass_bit_exact();
    test_stability_and_headroom();
    test_damp_shortens_tail();
    test_tone_brightens();
    test_turns_spreads_the_hit();
    test_block_size_independence();
    test_no_clicks_on_param_moves();
    test_stereo_and_mono();
    test_sample_rate_scaling();
    test_activity_and_reset();
    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
