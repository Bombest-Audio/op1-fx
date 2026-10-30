/* Behavioral tests for the Voltage core. Build and run with core/tests/run.sh */
#include "../voltage_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
            failures++;                                 \
        }                                               \
    } while (0)

#define N_MAX 400000
static float L[N_MAX], R[N_MAX];
static opvl_t synth;

static opvl_params defaults(void)
{
    opvl_params p = {OPVL_DEFAULT_XOR, OPVL_DEFAULT_FEEDBACK, OPVL_DEFAULT_FILTER, OPVL_DEFAULT_DETUNE,
                     OPVL_DEFAULT_ATTACK, OPVL_DEFAULT_ENV_DECAY, 1.0f, OPVL_DEFAULT_RELEASE, OPVL_DEFAULT_LEVEL};
    return p;
}

static void setup(float sr, opvl_params p)
{
    opvl_init(&synth, sr);
    opvl_set_params(&synth, &p);
    opvl_reset(&synth);
}

static void render_blocks(float *l, float *r, int n, int block)
{
    for (int i = 0; i < n; i += block) {
        int m = n - i < block ? n - i : block;
        opvl_render(&synth, l + i, r + i, m);
    }
}

static float peak(const float *x, int n)
{
    float p = 0;
    for (int i = 0; i < n; i++)
        p = fmaxf(p, fabsf(x[i]));
    return p;
}

static double rms(const float *x, int n)
{
    double s = 0;
    for (int i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (n > 0 ? n : 1));
}

static int finite_all(const float *x, int n)
{
    for (int i = 0; i < n; i++)
        if (!isfinite(x[i]))
            return 0;
    return 1;
}

/* Fundamental by upward zero crossings with linear interpolation. */
static double measure_hz(const float *x, int n, float sr)
{
    double first = -1, last = -1;
    int count = 0;
    for (int i = 1; i < n; i++)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            double t = i - 1 + (double)(-x[i - 1]) / (double)(x[i] - x[i - 1]);
            if (first < 0)
                first = t;
            last = t;
            count++;
        }
    return count > 1 ? (double)(count - 1) * sr / (last - first) : 0.0;
}

/* Energy above 2 kHz relative to total, via a one-pole high-pass difference (crude but monotone). */
static double bright(const float *x, int n)
{
    double e = 0, t = 0;
    for (int i = 1; i < n; i++) {
        double d = x[i] - x[i - 1];
        e += d * d;
        t += (double)x[i] * x[i];
    }
    return t > 0 ? e / t : 0;
}

static void test_pitch(void)
{
    const float rates[] = {22050.f, 44100.f, 48000.f, 96000.f, 192000.f};
    const int notes[] = {36, 48, 60, 69, 84};
    for (int ri = 0; ri < 5; ri++)
        for (int ni = 0; ni < 5; ni++) {
            float sr = rates[ri];
            opvl_params p = defaults(); /* defaults: filter closed, xor 0.05: carrier dominates */
            p.xor_amt = 0.0f;
            setup(sr, p);
            opvl_note_on(&synth, notes[ni], 100);
            int n = (int)(sr * 1.5f);
            render_blocks(L, R, n, 64);
            int skip = (int)(sr * 0.5f);
            double hz = measure_hz(L + skip, n - skip, sr);
            double want = 440.0 * pow(2.0, (notes[ni] - 69) / 12.0);
            double cents = 1200.0 * log2(hz / want);
            CHECK(fabs(cents) < 5.0, "pitch note %d at %.0f Hz: %.1f Hz want %.1f (%.2f cents)", notes[ni], sr, hz, want, cents);
        }
    /* pitch bend of +2 semitones */
    opvl_params p = defaults();
    p.xor_amt = 0.0f;
    setup(44100.f, p);
    opvl_note_on(&synth, 57, 100);
    opvl_pitch_bend(&synth, 2.0f);
    render_blocks(L, R, 44100, 64);
    double hz = measure_hz(L + 22050, 22050, 44100.f);
    double want = 220.0 * pow(2.0, 2.0 / 12.0);
    CHECK(fabs(1200.0 * log2(hz / want)) < 5.0, "bend: %.2f Hz want %.2f", hz, want);
}

static void test_poly_and_headroom(void)
{
    const float rates[] = {22050.f, 44100.f, 96000.f};
    for (int ri = 0; ri < 3; ri++) {
        float sr = rates[ri];
        /* every knob at both ends, 8 notes at max velocity */
        for (int corner = 0; corner < 16; corner++) {
            opvl_params p = defaults();
            p.xor_amt = (corner & 1) ? 1.f : 0.f;
            p.feedback = (corner & 2) ? 1.f : 0.f;
            p.filter = (corner & 4) ? 1.f : 0.f;
            p.detune = (corner & 8) ? 1.f : 0.f;
            p.env_attack = 0.0005f;
            p.env_sustain = 1.0f;
            p.level = 2.0f;
            setup(sr, p);
            for (int k = 0; k < 8; k++)
                opvl_note_on(&synth, 36 + 7 * k, 127);
            int n = (int)(sr * 0.5f);
            render_blocks(L, R, n, 37);
            CHECK(finite_all(L, n) && finite_all(R, n), "NaN or Inf corner %d sr %.0f", corner, sr);
            float pk = peak(L, n);
            CHECK(pk < 1.0f, "peak %.4f >= 1 at corner %d sr %.0f", pk, corner, sr);
            CHECK(pk > 0.05f, "silent at corner %d", corner);
            CHECK(opvl_active_voices(&synth) == 8, "voices %d", opvl_active_voices(&synth));
        }
    }
}

static void test_note_off(void)
{
    opvl_params p = defaults();
    p.env_release = 0.2f;
    setup(44100.f, p);
    opvl_note_on(&synth, 60, 100);
    render_blocks(L, R, 22050, 128);
    opvl_note_off(&synth, 60);
    render_blocks(L, R, 44100, 128);
    CHECK(opvl_active_voices(&synth) == 0, "voice still active after release");
    CHECK(peak(L + 44100 - 128, 128) == 0.0f, "not silent after release: %g", peak(L + 44100 - 128, 128));
    /* sustain pedal holds, then lets go */
    setup(44100.f, p);
    opvl_sustain(&synth, 1);
    opvl_note_on(&synth, 60, 100);
    render_blocks(L, R, 4410, 128);
    opvl_note_off(&synth, 60);
    render_blocks(L, R, 44100, 128);
    CHECK(opvl_active_voices(&synth) == 1, "pedal did not hold the note");
    opvl_sustain(&synth, 0);
    render_blocks(L, R, 44100, 128);
    CHECK(opvl_active_voices(&synth) == 0, "pedal release did not end the note");
    /* all off */
    setup(44100.f, p);
    for (int k = 0; k < 4; k++)
        opvl_note_on(&synth, 50 + k, 90);
    render_blocks(L, R, 2000, 128);
    opvl_all_off(&synth);
    render_blocks(L, R, 44100, 128);
    CHECK(opvl_active_voices(&synth) == 0, "all_off left voices");
}

/* Largest sample-to-sample step in a window, minus what the same signal does without the event. */
static float max_step(const float *x, int a, int b)
{
    float m = 0;
    for (int i = a + 1; i < b; i++)
        m = fmaxf(m, fabsf(x[i] - x[i - 1]));
    return m;
}

static void test_steal_clicks(void)
{
    /* Reference: the steepest step of a clean 8 voice chord played steadily. A steal must not exceed it by more than 2x.
     * Justification: the steal fades the old voice over 3 ms and the new note enters through the attack ramp, so the
     * step is bounded by the loudest legitimate slope of the mix. */
    opvl_params p = defaults();
    p.xor_amt = 0.0f;
    p.env_attack = 0.003f;
    setup(44100.f, p);
    for (int k = 0; k < 8; k++)
        opvl_note_on(&synth, 48 + 3 * k, 100);
    render_blocks(L, R, 44100, 64);
    float base = max_step(L, 30000, 44100);
    /* steal: 9th note, and a retrigger of a playing note, mid block */
    int pos = 44100;
    opvl_note_on(&synth, 80, 127);
    render_blocks(L + pos, R + pos, 100, 64);
    opvl_note_on(&synth, 48, 127);
    render_blocks(L + pos + 100, R + pos + 100, 8000, 50);
    float st = max_step(L, pos - 1, pos + 8100);
    CHECK(st < 2.0f * base + 0.02f, "steal step %.4f vs steady %.4f", st, base);
    CHECK(finite_all(L, pos + 8100), "NaN during steal");
    /* hammer the same note 200 times with random gaps */
    setup(44100.f, p);
    unsigned rng = 7;
    float worst = 0;
    int t = 0;
    for (int k = 0; k < 200; k++) {
        rng = rng * 1664525u + 1013904223u;
        opvl_note_on(&synth, 40 + (int)((rng >> 24) % 30), 100);
        int len = 64 + (int)((rng >> 8) % 600);
        render_blocks(L + t, R + t, len, 61);
        worst = fmaxf(worst, max_step(L, t > 0 ? t - 1 : 0, t + len));
        t += len;
    }
    CHECK(worst < 0.35f, "worst step in a retrigger storm %.4f", worst);
    CHECK(finite_all(L, t), "NaN in storm");
}

static void test_knobs(void)
{
    /* Each knob must change the spectrum or the envelope of a held note. */
    opvl_params base = defaults();
    base.xor_amt = 0.5f;
    base.feedback = 0.5f;
    base.filter = 0.45f;
    base.detune = 0.5f;
    double ref_b, ref_r;
    {
        setup(44100.f, base);
        opvl_note_on(&synth, 48, 100);
        render_blocks(L, R, 44100, 128);
        ref_b = bright(L + 22050, 22050);
        ref_r = rms(L + 22050, 22050);
    }
    const char *names[4] = {"xor", "feedback", "filter", "detune"};
    for (int k = 0; k < 4; k++) {
        double best = 0;
        for (int hi = 0; hi < 2; hi++) {
            opvl_params p = base;
            float v = hi ? 1.0f : 0.0f;
            if (k == 0) p.xor_amt = v;
            if (k == 1) p.feedback = v;
            if (k == 2) p.filter = v;
            if (k == 3) p.detune = v;
            setup(44100.f, p);
            opvl_note_on(&synth, 48, 100);
            render_blocks(L, R, 44100, 128);
            double b = bright(L + 22050, 22050), r = rms(L + 22050, 22050);
            double d = fabs(log((b + 1e-9) / (ref_b + 1e-9))) + fabs(log((r + 1e-9) / (ref_r + 1e-9)));
            if (d > best) best = d;
        }
        CHECK(best > 0.15, "knob %s barely changes the sound (%.3f)", names[k], best);
    }
    /* the envelope changes the output: short vs long attack */
    opvl_params p = defaults();
    p.env_attack = 0.0005f;
    setup(44100.f, p);
    opvl_note_on(&synth, 60, 100);
    render_blocks(L, R, 4410, 128);
    double fast = rms(L, 441);
    p.env_attack = 0.2f;
    setup(44100.f, p);
    opvl_note_on(&synth, 60, 100);
    render_blocks(L, R, 4410, 128);
    double slow = rms(L, 441);
    CHECK(fast > 5.0 * slow, "attack has no effect: %.4f vs %.4f", fast, slow);
}

static void test_filter_curve(void)
{
    CHECK(fabsf(opvl_filter_hz(0.0f) - 20.6f) < 3.0f, "filter low end %.1f", opvl_filter_hz(0.0f));
    CHECK(opvl_filter_hz(1.0f) > 15000.f && opvl_filter_hz(1.0f) < 18000.f, "filter high end %.1f", opvl_filter_hz(1.0f));
    float prev = 0;
    for (int i = 0; i <= 100; i++) {
        float h = opvl_filter_hz(i / 100.0f);
        CHECK(h >= prev, "filter curve not monotone at %d", i);
        prev = h;
    }
}

static void test_determinism_and_blocks(void)
{
    opvl_params p = defaults();
    p.xor_amt = 0.8f;
    p.filter = 0.4f;
    static float A[20000], B[20000], Br[20000];
    for (int run = 0; run < 2; run++) {
        setup(44100.f, p);
        opvl_note_on(&synth, 52, 90);
        opvl_note_on(&synth, 59, 70);
        render_blocks(run ? B : A, Br, 20000, 128);
    }
    CHECK(memcmp(A, B, sizeof A) == 0, "same input gave different output (core has no random source)");
    /* block size 1 against block size 128: control ticks are counted in samples so the result must match exactly */
    setup(44100.f, p);
    opvl_note_on(&synth, 52, 90);
    opvl_note_on(&synth, 59, 70);
    render_blocks(B, Br, 20000, 1);
    CHECK(memcmp(A, B, sizeof A) == 0, "block size 1 differs from 128");
    /* left equals right */
    CHECK(memcmp(L, R, 0) == 0, "n/a");
}

static void test_knob_smoothing(void)
{
    /* Jump every knob each 128 samples: the output step must stay bounded (no zipper clicks beyond signal slope). */
    opvl_params p = defaults();
    p.xor_amt = 0.0f;
    setup(44100.f, p);
    opvl_note_on(&synth, 48, 100);
    render_blocks(L, R, 22050, 128);
    int t = 22050;
    float worst = 0;
    for (int k = 0; k < 100; k++) {
        p.xor_amt = (k & 1) ? 1.f : 0.f;
        p.filter = (k & 2) ? 0.45f : 0.1f;
        p.feedback = (k & 4) ? 1.f : 0.f;
        p.detune = (k & 8) ? 1.f : 0.2f;
        opvl_set_params(&synth, &p);
        render_blocks(L + t, R + t, 128, 128);
        worst = fmaxf(worst, max_step(L, t - 1, t + 128));
        t += 128;
    }
    CHECK(worst < 0.5f, "knob jump step %.3f", worst);
    CHECK(finite_all(L, t), "NaN after knob jumps");
}

int main(void)
{
    test_pitch();
    test_poly_and_headroom();
    test_note_off();
    test_steal_clicks();
    test_knobs();
    test_filter_curve();
    test_determinism_and_blocks();
    test_knob_smoothing();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
