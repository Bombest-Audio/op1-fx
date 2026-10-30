/* Behavioral tests for the Digital core. Build and run with run.sh (clang, ASan + UBSan). */
#include "../digital_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond, ...)                                                                                              \
    do {                                                                                                              \
        g_checks++;                                                                                                   \
        if (!(cond)) {                                                                                                \
            g_fail++;                                                                                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                               \
            printf(__VA_ARGS__);                                                                                      \
            printf("\n");                                                                                             \
        }                                                                                                             \
    } while (0)

#define MAXN 400000
static float bufL[MAXN], bufR[MAXN];

static digital_params defaults(void)
{
    digital_params p;
    digital_default_params(&p);
    return p;
}

/* No feedback, second oscillator silent (RATIO at the top), no XOR depth: a plain sine. */
static digital_params clean(void)
{
    digital_params p = defaults();
    p.feedback = 0.0f;
    p.ratio = 1.0f;
    p.detune = 0.0f;
    p.crunch = 0.0f;
    p.sustain = 1.0f;
    p.attack = 0.0005f;
    p.level = 1.0f;
    return p;
}

static void make(digital_t *d, float sr, const digital_params *p)
{
    digital_init(d, sr);
    digital_set_params(d, p);
    digital_reset(d);
}

static void render(digital_t *d, int n, int at)
{
    digital_render(d, bufL + at, bufR + at, n);
}

static int all_finite(const float *x, int n)
{
    for (int i = 0; i < n; i++)
        if (!isfinite(x[i]))
            return 0;
    return 1;
}

static float peak_of(const float *x, int n)
{
    float m = 0.0f;
    for (int i = 0; i < n; i++)
        if (fabsf(x[i]) > m)
            m = fabsf(x[i]);
    return m;
}

static float max_step(const float *x, int n)
{
    float m = 0.0f;
    for (int i = 1; i < n; i++) {
        const float s = fabsf(x[i] - x[i - 1]);
        if (s > m)
            m = s;
    }
    return m;
}

static double rms_of(const float *x, int n) __attribute__((unused));
static double rms_of(const float *x, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (n > 0 ? n : 1));
}

static double cents(double measured, double expected)
{
    return 1200.0 * log2(measured / expected);
}

static double midi_hz(double note)
{
    return 440.0 * pow(2.0, (note - 69.0) / 12.0);
}

/* Frequency from rising zero crossings, with linear interpolation. */
static double zero_cross_hz(const float *x, int n, double sr)
{
    double first = -1.0, last = -1.0;
    int count = 0;
    for (int i = 1; i < n; i++) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double t = (double)(i - 1) + (double)(-x[i - 1]) / (double)(x[i] - x[i - 1]);
            if (first < 0.0)
                first = t;
            last = t;
            count++;
        }
    }
    if (count < 3)
        return 0.0;
    return (double)(count - 1) * sr / (last - first);
}

/* Windowed Goertzel magnitude at one frequency. */
static double goertzel(const float *x, int n, double sr, double hz)
{
    const double w = 2.0 * M_PI * hz / sr;
    const double c = 2.0 * cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; i++) {
        const double win = 0.5 - 0.5 * cos(2.0 * M_PI * i / (n - 1));
        const double s = (double)x[i] * win + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2);
}

/* ---- spectrum ---- */

#define FFT_N 8192
static void fft(double *re, double *im, int n)
{
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < len / 2; k++) {
                const double wr = cos(ang * k), wi = sin(ang * k);
                const double ur = re[i + k], ui = im[i + k];
                const double vr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
                const double vi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
                re[i + k] = ur + vr;
                im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr;
                im[i + k + len / 2] = ui - vi;
            }
    }
}

/* Magnitude spectrum in dB (bins 0..FFT_N/2), Hann windowed. */
static void spectrum_db(const float *x, double *db)
{
    static double re[FFT_N], im[FFT_N];
    for (int i = 0; i < FFT_N; i++) {
        re[i] = (double)x[i] * (0.5 - 0.5 * cos(2.0 * M_PI * i / (FFT_N - 1)));
        im[i] = 0.0;
    }
    fft(re, im, FFT_N);
    for (int k = 0; k <= FFT_N / 2; k++) {
        const double m = sqrt(re[k] * re[k] + im[k] * im[k]) / (FFT_N / 4.0);
        db[k] = 20.0 * log10(m + 1e-6);
    }
}

static double centroid(const double *db)
{
    double num = 0.0, den = 0.0;
    for (int k = 1; k <= FFT_N / 2; k++) {
        const double a = pow(10.0, db[k] / 20.0);
        num += a * k;
        den += a;
    }
    return den > 0.0 ? num / den : 0.0;
}

/* Steady spectrum of note 57 held for a while, at 44.1 kHz. */
static void steady_spectrum(const digital_params *p, double *db, int note)
{
    digital_t d;
    make(&d, 44100.0f, p);
    digital_note_on(&d, note, 100);
    render(&d, 44100 / 2, 0);
    render(&d, FFT_N, 0);
    spectrum_db(bufL, db);
}

static double mean_abs_diff_db(const double *a, const double *b)
{
    double s = 0.0;
    for (int k = 1; k < 3000; k++)
        s += fabs(a[k] - b[k]);
    return s / 2999.0;
}

/* ---- tests ---- */

static void test_defaults_and_mappings(void)
{
    const digital_params p = defaults();
    /* raw factory values 2000, 5000, -1000, 2000 of 32767 */
    CHECK(fabsf(p.feedback - 2000.0f / 32767.0f) < 2e-4f, "feedback default %f", (double)p.feedback);
    CHECK(fabsf(p.ratio - 5000.0f / 32767.0f) < 2e-4f, "ratio default %f", (double)p.ratio);
    CHECK(fabsf(p.detune - (-1000.0f / 32767.0f)) < 2e-4f, "detune default %f", (double)p.detune);
    CHECK(fabsf(p.crunch - 2000.0f / 32767.0f) < 2e-4f, "crunch default %f", (double)p.crunch);

    /* octave steps: eight positions from -3 to 4 */
    CHECK(digital_octave_step(0.0f) == -3, "step at 0 is %d", digital_octave_step(0.0f));
    CHECK(digital_octave_step(0.1526f) == -2, "default step is %d", digital_octave_step(0.1526f));
    CHECK(digital_octave_step(0.5f) == 1, "step at 0.5 is %d", digital_octave_step(0.5f));
    CHECK(digital_octave_step(1.0f) == 4, "step at 1 is %d", digital_octave_step(1.0f));
    int prev = -99, rises = 0;
    for (int i = 0; i <= 1000; i++) {
        const int s = digital_octave_step((float)i / 1000.0f);
        CHECK(s >= prev, "octave step falls at %d", i);
        if (s > prev)
            rises++;
        prev = s;
    }
    CHECK(rises == 8, "octave step has %d positions, want 8", rises);

    CHECK(fabsf(digital_osc2_ratio(0.1526f, 0.0f) - 0.25f) < 1e-6f, "ratio at default knob without detune");
    CHECK(fabsf(digital_osc2_ratio(0.5f, 1.0f) - 2.0625f) < 1e-6f, "ratio 2 plus top detune");
    CHECK(fabsf(digital_osc2_ratio(0.5f, -1.0f) - 1.9375f) < 1e-6f, "ratio 2 minus top detune");
    CHECK(fabsf(digital_osc2_level(0.0f) - 32767.0f / 32768.0f) < 1e-6f, "level at 0");
    CHECK(digital_osc2_level(1.0f) == 0.0f, "level at 1 must be exactly 0");
    CHECK(digital_sync_active(0.2f) && !digital_sync_active(0.0f) && !digital_sync_active(-0.5f) &&
              !digital_sync_active(DIGITAL_DEFAULT_DETUNE),
          "sync threshold");
    CHECK(digital_crunch_depth(0.0f) == 0.0f && digital_crunch_depth(1.0f) == 1.0f &&
              fabsf(digital_crunch_depth(0.5f) - 0.25f) < 1e-6f,
          "crunch depth is squared");
    CHECK(fabsf(digital_feedback_radians(1.0f) - 3.14159265f) < 1e-5f, "feedback reaches pi");
}

static void test_idle_is_silent(void)
{
    digital_t d;
    const digital_params p = defaults();
    make(&d, 44100.0f, &p);
    render(&d, 4096, 0);
    int zeros = 1;
    for (int i = 0; i < 4096; i++)
        if (bufL[i] != 0.0f || bufR[i] != 0.0f)
            zeros = 0;
    CHECK(zeros, "idle synth is not exact zero");
    CHECK(digital_active_voices(&d) == 0, "idle synth reports voices");
}

static void test_pitch(void)
{
    static const float rates[] = {22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f};
    static const int notes[] = {24, 33, 45, 57, 60, 69, 81, 93, 96};
    double worst = 0.0;
    int count = 0;
    for (size_t r = 0; r < sizeof rates / sizeof rates[0]; r++)
        for (size_t k = 0; k < sizeof notes / sizeof notes[0]; k++) {
            digital_t d;
            const digital_params p = clean();
            make(&d, rates[r], &p);
            digital_note_on(&d, notes[k], 127);
            const int warm = (int)(rates[r] * 0.05f);
            const int len = (int)(rates[r] * (notes[k] < 40 ? 1.2f : 0.5f));
            render(&d, warm + len, 0);
            const double hz = zero_cross_hz(bufL + warm, len, rates[r]);
            const double err = cents(hz, midi_hz(notes[k]));
            if (fabs(err) > worst)
                worst = fabs(err);
            CHECK(fabs(err) < 5.0, "sr %.0f note %d: %.3f Hz, %.3f cents", (double)rates[r], notes[k], hz, err);
            count++;
        }
    printf("  pitch, clean sine, %d cases: worst %.4f cents\n", count, worst);

    /* the default voice: the strongest peak within a semitone of the note is at the note */
    double worst_default = 0.0;
    for (int note = 45; note <= 81; note += 12) {
        digital_t d;
        const digital_params p = defaults();
        make(&d, 44100.0f, &p);
        digital_note_on(&d, note, 127);
        render(&d, 44100 / 4, 0);
        render(&d, 44100, 0);
        const double f = midi_hz(note);
        double best = -1.0, bestc = 0.0;
        for (double c = -50.0; c <= 50.0; c += 0.5) {
            const double m = goertzel(bufL, 44100, 44100.0, f * pow(2.0, c / 1200.0));
            if (m > best) {
                best = m;
                bestc = c;
            }
        }
        if (fabs(bestc) > worst_default)
            worst_default = fabs(bestc);
        CHECK(fabs(bestc) <= 5.0, "default voice note %d peaks %.1f cents off", note, bestc);
    }
    printf("  pitch, default voice, peak within +-50 cents of the note: worst %.1f cents\n", worst_default);

    /* pitch bend: full bend with the default range is +200 cents, and back */
    {
        digital_t d;
        digital_params p = clean();
        make(&d, 44100.0f, &p);
        digital_note_on(&d, 69, 127);
        digital_pitch_bend(&d, 1.0f);
        render(&d, 44100 / 2, 0);
        const double up = cents(zero_cross_hz(bufL + 4410, 44100 / 2 - 4410, 44100.0), 440.0);
        digital_pitch_bend(&d, -1.0f);
        render(&d, 44100 / 2, 0);
        const double down = cents(zero_cross_hz(bufL + 4410, 44100 / 2 - 4410, 44100.0), 440.0);
        printf("  pitch bend: %.2f cents up, %.2f cents down\n", up, down);
        CHECK(fabs(up - 200.0) < 1.0 && fabs(down + 200.0) < 1.0, "bend %.2f / %.2f", up, down);
    }
}

static void test_polyphony_and_headroom(void)
{
    digital_params sets[5];
    sets[0] = defaults();
    sets[1] = defaults();
    sets[1].feedback = 1.0f; sets[1].ratio = 0.3f; sets[1].detune = 1.0f; sets[1].crunch = 1.0f;
    sets[2] = defaults();
    sets[2].feedback = 0.0f; sets[2].ratio = 0.0f; sets[2].detune = -1.0f; sets[2].crunch = 0.0f;
    sets[3] = defaults();
    sets[3].feedback = 0.7f; sets[3].ratio = 0.9f; sets[3].detune = 0.4f; sets[3].crunch = 0.5f;
    sets[4] = clean();
    float worst = 0.0f;
    for (int s = 0; s < 5; s++) {
        digital_t d;
        digital_params p = sets[s];
        p.level = 1.0f;
        p.sustain = 1.0f;
        make(&d, 44100.0f, &p);
        for (int n = 0; n < 8; n++)
            digital_note_on(&d, 48 + n * 3, 127);
        CHECK(digital_active_voices(&d) == 8, "8 notes give %d voices", digital_active_voices(&d));
        render(&d, 44100, 0);
        CHECK(all_finite(bufL, 44100) && all_finite(bufR, 44100), "set %d not finite", s);
        const float pk = fmaxf(peak_of(bufL, 44100), peak_of(bufR, 44100));
        CHECK(pk < 1.0f, "set %d peak %.4f", s, (double)pk);
        CHECK(pk > 0.05f, "set %d is nearly silent (%.4f)", s, (double)pk);
        if (pk > worst)
            worst = pk;
    }
    printf("  8 voices at velocity 127, level 1, five parameter sets: worst peak %.4f (%.2f dBFS)\n", (double)worst,
           20.0 * log10((double)worst));
}

static void test_release_and_pedal(void)
{
    digital_t d;
    digital_params p = defaults();
    p.release = 0.05f;
    p.sustain = 0.8f;
    make(&d, 44100.0f, &p);
    digital_note_on(&d, 60, 100);
    digital_note_on(&d, 64, 100);
    render(&d, 22050, 0);
    digital_note_off(&d, 60);
    digital_note_off(&d, 64);
    render(&d, 22050, 0);
    int zero = 1;
    for (int i = 11025; i < 22050; i++)
        if (bufL[i] != 0.0f || bufR[i] != 0.0f)
            zero = 0;
    CHECK(zero, "output after release is not exact zero");
    CHECK(digital_active_voices(&d) == 0, "voices left after release: %d", digital_active_voices(&d));

    /* sustain pedal */
    make(&d, 44100.0f, &p);
    digital_sustain_pedal(&d, 1);
    digital_note_on(&d, 60, 100);
    render(&d, 4410, 0);
    digital_note_off(&d, 60);
    render(&d, 22050, 0);
    CHECK(digital_active_voices(&d) == 1 && peak_of(bufL + 10000, 5000) > 0.02f, "pedal did not hold the note");
    digital_sustain_pedal(&d, 0);
    render(&d, 22050, 0);
    CHECK(digital_active_voices(&d) == 0, "lifting the pedal did not release");

    /* all notes off */
    make(&d, 44100.0f, &p);
    for (int n = 0; n < 6; n++)
        digital_note_on(&d, 50 + n, 90);
    digital_sustain_pedal(&d, 1);
    render(&d, 2000, 0);
    digital_all_off(&d);
    render(&d, 22050, 0);
    CHECK(digital_active_voices(&d) == 0, "all-off left %d voices", digital_active_voices(&d));
    /* velocity 0 is a note off */
    make(&d, 44100.0f, &p);
    digital_note_on(&d, 60, 100);
    render(&d, 1000, 0);
    digital_note_on(&d, 60, 0);
    render(&d, 22050, 0);
    CHECK(digital_active_voices(&d) == 0, "note on with velocity 0 did not release");
}

/* The biggest single-sample step of the left channel over a window. */
static float window_step(const float *x, int from, int to)
{
    float m = 0.0f;
    for (int i = from + 1; i < to; i++) {
        const float s = fabsf(x[i] - x[i - 1]);
        if (s > m)
            m = s;
    }
    return m;
}

/* Steady-state largest step of a chord, played on a fresh synth. */
static float chord_step(const digital_params *p, const int *notes, int count)
{
    digital_t d;
    make(&d, 44100.0f, p);
    for (int i = 0; i < count; i++)
        digital_note_on(&d, notes[i], 100);
    render(&d, 20000, 0);
    return window_step(bufL, 12000, 20000);
}

static void steal_case(const char *name, const digital_params *p, int attack_fast)
{
    digital_params q = *p;
    q.sustain = 1.0f;
    q.level = 1.0f;
    if (attack_fast)
        q.attack = 0.0005f;
    int held[8], final_chord[8];
    for (int n = 0; n < 8; n++)
        held[n] = 50 + n * 2;

    /* Steal: eight held notes, a ninth arrives and takes the oldest voice. The yardstick is the larger of the
     * steady steps of the chord before it and of the chord after it (the oldest note replaced by the new one):
     * a high new note has bigger steps than a low one with no click involved. */
    {
        memcpy(final_chord, held, sizeof held);
        final_chord[0] = 77;
        const float ref = fmaxf(chord_step(&q, held, 8), chord_step(&q, final_chord, 8));
        digital_t d;
        make(&d, 44100.0f, &q);
        for (int n = 0; n < 8; n++)
            digital_note_on(&d, held[n], 100);
        render(&d, 20000, 0);
        digital_note_on(&d, 77, 100);
        render(&d, 4000, 20000);
        const float after = window_step(bufL, 20000, 24000);
        printf("  %-20s steal:         steady %.4f, in the 90 ms after %.4f, ratio %.3f\n", name, (double)ref,
               (double)after, (double)(after / ref));
        CHECK(after <= ref * 1.2f, "%s: steal steps %.4f against steady %.4f", name, (double)after, (double)ref);
    }
    /* Retrigger: the same note again, harder, on the voice it already has. */
    {
        const int chord[2] = {57, 64};
        const float ref = chord_step(&q, chord, 2);
        digital_t d;
        make(&d, 44100.0f, &q);
        digital_note_on(&d, 57, 100);
        digital_note_on(&d, 64, 100);
        render(&d, 20000, 0);
        digital_note_on(&d, 57, 127);
        render(&d, 4000, 20000);
        const float after = window_step(bufL, 20000, 24000);
        printf("  %-20s retrigger:     steady %.4f, in the 90 ms after %.4f, ratio %.3f\n", name, (double)ref,
               (double)after, (double)(after / ref));
        CHECK(after <= ref * 1.2f, "%s: retrigger steps %.4f against steady %.4f", name, (double)after, (double)ref);
    }
    /* Steal a voice that is releasing: the quietest releasing voice goes first. */
    {
        memcpy(final_chord, held, sizeof held);
        final_chord[0] = 80;
        const float ref = fmaxf(chord_step(&q, held, 8), chord_step(&q, final_chord, 8));
        digital_t d;
        digital_params r = q;
        r.release = 1.0f;
        make(&d, 44100.0f, &r);
        for (int n = 0; n < 8; n++)
            digital_note_on(&d, held[n], 100);
        render(&d, 20000, 0);
        digital_note_off(&d, 50);
        render(&d, 4000, 20000);
        digital_note_on(&d, 80, 100);
        render(&d, 4000, 24000);
        const float after = window_step(bufL, 24000, 28000);
        printf("  %-20s steal release: steady %.4f, in the 90 ms after %.4f, ratio %.3f\n", name, (double)ref,
               (double)after, (double)(after / ref));
        CHECK(after <= ref * 1.2f, "%s: release-steal steps %.4f against %.4f", name, (double)after, (double)ref);
    }
}

static void test_steals(void)
{
    digital_params a = defaults();
    steal_case("default", &a, 0);
    digital_params b = defaults();
    b.feedback = 0.6f; b.ratio = 0.45f; b.detune = -0.4f; b.crunch = 0.3f;
    steal_case("feedback + crunch", &b, 1);
    digital_params c = defaults();
    c.feedback = 0.4f; c.ratio = 0.45f; c.detune = 0.5f; c.crunch = 0.2f;
    steal_case("hard sync on", &c, 1);
}

static void test_knobs_change_sound(void)
{
    static double lo[FFT_N / 2 + 1], hi[FFT_N / 2 + 1];
    const char *names[4] = {"FEEDBACK", "RATIO", "DETUNE", "CRUNCH"};
    for (int k = 0; k < 4; k++) {
        digital_params a = defaults(), b = defaults();
        a.sustain = b.sustain = 1.0f;
        float *fa[4] = {&a.feedback, &a.ratio, &a.detune, &a.crunch};
        float *fb[4] = {&b.feedback, &b.ratio, &b.detune, &b.crunch};
        *fa[k] = k == 2 ? -1.0f : 0.0f;
        *fb[k] = 1.0f;
        steady_spectrum(&a, lo, 57);
        steady_spectrum(&b, hi, 57);
        const double diff = mean_abs_diff_db(lo, hi);
        printf("  %-8s 0 to 1: mean spectrum change %.1f dB, centroid bin %.1f -> %.1f\n", names[k], diff, centroid(lo),
               centroid(hi));
        CHECK(diff > 3.0, "%s changes the spectrum by only %.2f dB", names[k], diff);
        if (k == 0 || k == 3)
            CHECK(centroid(hi) > centroid(lo), "%s does not raise the centroid", names[k]);
    }

    /* A finer check on each knob: a small move is a small change, a big move a bigger one. */
    for (int k = 0; k < 4; k++) {
        digital_params a = defaults(), b = defaults(), c = defaults();
        a.sustain = b.sustain = c.sustain = 1.0f;
        a.crunch = b.crunch = c.crunch = 0.3f;
        a.feedback = b.feedback = c.feedback = 0.3f;
        float *fa[4] = {&a.feedback, &a.ratio, &a.detune, &a.crunch};
        float *fb[4] = {&b.feedback, &b.ratio, &b.detune, &b.crunch};
        float *fc[4] = {&c.feedback, &c.ratio, &c.detune, &c.crunch};
        static const float base[4] = {0.3f, 0.45f, 0.0f, 0.3f};
        *fa[k] = base[k];
        *fb[k] = base[k] + (k == 1 ? 0.0f : 0.05f);
        *fc[k] = k == 1 ? 0.72f : (k == 2 ? 0.8f : 0.9f);
        static double sa[FFT_N / 2 + 1], sb[FFT_N / 2 + 1], sc[FFT_N / 2 + 1];
        steady_spectrum(&a, sa, 57);
        steady_spectrum(&b, sb, 57);
        steady_spectrum(&c, sc, 57);
        const double small = mean_abs_diff_db(sa, sb), big = mean_abs_diff_db(sa, sc);
        printf("  %-8s small move %.1f dB, large move %.1f dB\n", names[k], small, big);
        if (k != 1)
            CHECK(big > small, "%s: large move %.2f not above small move %.2f", names[k], big, small);
    }
}

static void test_sync(void)
{
    /* Note 69 at 44 kHz has a period of exactly 100 samples. With sync the second oscillator restarts with the
     * first every cycle, so the waveform repeats. Without it the two drift. */
    digital_params p = defaults();
    p.feedback = 0.0f;
    p.ratio = 0.4375f; /* step 0 */
    p.crunch = 0.0f;
    p.sustain = 1.0f;
    double err[2];
    const float dets[2] = {0.6f, -0.6f};
    for (int s = 0; s < 2; s++) {
        digital_t d;
        p.detune = dets[s];
        make(&d, 44000.0f, &p);
        digital_note_on(&d, 69, 127);
        render(&d, 8000, 0);
        double num = 0.0, den = 0.0;
        for (int i = 4000; i < 7900; i++) {
            const double e = (double)bufL[i] - (double)bufL[i - 100];
            num += e * e;
            den += (double)bufL[i] * bufL[i];
        }
        err[s] = sqrt(num / den);
    }
    printf("  sync: period-to-period error %.4f with sync, %.4f without\n", err[0], err[1]);
    CHECK(err[0] < 0.02, "synced waveform does not repeat (%.4f)", err[0]);
    CHECK(err[1] > 0.1, "unsynced waveform repeats too well (%.4f)", err[1]);
}

static void test_crunch_is_bit_flip(void)
{
    /* At CRUNCH 0 the XOR stage is a pass-through: a clean sine stays a sine (low harmonic distortion). At full
     * CRUNCH the same note is full of bit-flip products. */
    static double db0[FFT_N / 2 + 1], db1[FFT_N / 2 + 1];
    digital_params p = clean();
    steady_spectrum(&p, db0, 57);
    p.crunch = 1.0f;
    steady_spectrum(&p, db1, 57);
    /* energy outside the fundamental: bins above 2 x fundamental */
    const int fund = (int)lround(midi_hz(57) * FFT_N / 44100.0);
    double e0 = 0.0, e1 = 0.0;
    for (int k = fund * 3; k < 3000; k++) {
        e0 += pow(10.0, db0[k] / 10.0);
        e1 += pow(10.0, db1[k] / 10.0);
    }
    const double ratio_db = 10.0 * log10(e1 / (e0 + 1e-12));
    printf("  crunch: energy above the 3rd harmonic is %.1f dB higher at CRUNCH 1 than at 0\n", ratio_db);
    CHECK(ratio_db > 30.0, "crunch adds only %.1f dB of upper energy", ratio_db);
}

static float env_peak_window(const float *x, int from, int n)
{
    return peak_of(x + from, n);
}

static void test_envelope(void)
{
    /* attack: time for the 5 ms peak envelope to reach 90 % of its top */
    double t90[2];
    const float atk[2] = {0.05f, 0.5f};
    for (int i = 0; i < 2; i++) {
        digital_t d;
        digital_params p = clean();
        p.attack = atk[i];
        p.sustain = 1.0f;
        make(&d, 44100.0f, &p);
        digital_note_on(&d, 57, 127);
        render(&d, 44100 * 2, 0);
        const float top = peak_of(bufL + 44100, 4410);
        int at = 0;
        for (int w = 0; w < 44100 * 2 - 220; w += 220)
            if (env_peak_window(bufL, w, 220) >= 0.9f * top) {
                at = w;
                break;
            }
        t90[i] = at / 44100.0;
    }
    printf("  attack 50 ms and 500 ms: reaches 90%% at %.3f s and %.3f s\n", t90[0], t90[1]);
    CHECK(t90[1] > t90[0] * 6.0 && t90[0] > 0.03 && t90[0] < 0.06, "attack times %.3f, %.3f", t90[0], t90[1]);

    /* decay to sustain: the held level settles near the sustain share of the peak */
    {
        digital_t d;
        digital_params p = clean();
        p.decay = 0.1f;
        p.sustain = 0.3f;
        make(&d, 44100.0f, &p);
        digital_note_on(&d, 57, 127);
        render(&d, 44100 * 2, 0);
        const float top = peak_of(bufL, 2000);
        const float held = peak_of(bufL + 44100, 4410);
        printf("  sustain 0.3: held level is %.3f of the peak\n", (double)(held / top));
        CHECK(fabsf(held / top - 0.3f) < 0.03f, "sustain level %.3f", (double)(held / top));
    }
    /* release: 60 dB down in about the release time */
    {
        digital_t d;
        digital_params p = clean();
        p.release = 0.4f;
        p.sustain = 1.0f;
        make(&d, 44100.0f, &p);
        digital_note_on(&d, 57, 127);
        render(&d, 22050, 0);
        const float top = peak_of(bufL + 20000, 2000);
        digital_note_off(&d, 57);
        render(&d, 44100, 22050);
        int at = -1;
        for (int w = 22050; w < 22050 + 44100 - 220; w += 110)
            if (env_peak_window(bufL, w, 220) < top * 0.001f) {
                at = w - 22050;
                break;
            }
        printf("  release 0.4 s: -60 dB at %.3f s\n", at / 44100.0);
        CHECK(at > 0 && fabs(at / 44100.0 - 0.4) < 0.06, "release reaches -60 dB at %.3f s", at / 44100.0);
    }
    /* velocity scales the level */
    {
        digital_t d;
        digital_params p = clean();
        make(&d, 44100.0f, &p);
        digital_note_on(&d, 57, 127);
        render(&d, 4410, 0);
        const float loud = peak_of(bufL + 2000, 2000);
        make(&d, 44100.0f, &p);
        digital_note_on(&d, 57, 32);
        render(&d, 4410, 0);
        const float soft = peak_of(bufL + 2000, 2000);
        CHECK(fabsf(soft / loud - 32.0f / 127.0f) < 0.03f, "velocity 32 gives %.3f of velocity 127", (double)(soft / loud));
    }
    /* mod wheel adds feedback, so it changes the spectrum */
    {
        static double a[FFT_N / 2 + 1], b[FFT_N / 2 + 1];
        digital_params p = defaults();
        p.sustain = 1.0f;
        steady_spectrum(&p, a, 57);
        digital_t d;
        make(&d, 44100.0f, &p);
        digital_mod_wheel(&d, 1.0f);
        digital_note_on(&d, 57, 100);
        render(&d, 44100 / 2, 0);
        render(&d, FFT_N, 0);
        spectrum_db(bufL, b);
        const double diff = mean_abs_diff_db(a, b);
        printf("  mod wheel at 1: mean spectrum change %.1f dB\n", diff);
        CHECK(diff > 1.0, "mod wheel changes the spectrum by only %.2f dB", diff);
    }
}

typedef struct {
    int at;
    int kind; /* 0 note on, 1 note off, 2 bend, 3 params */
    int a, b;
} event;

static void run_script(digital_t *d, const event *ev, int nev, int total, int split, float *out)
{
    int pos = 0, e = 0;
    while (pos < total) {
        while (e < nev && ev[e].at <= pos) {
            if (ev[e].kind == 0)
                digital_note_on(d, ev[e].a, ev[e].b);
            else if (ev[e].kind == 1)
                digital_note_off(d, ev[e].a);
            else if (ev[e].kind == 2)
                digital_pitch_bend(d, (float)ev[e].a / 100.0f);
            else {
                digital_params p = defaults();
                p.feedback = (float)ev[e].a / 100.0f;
                p.ratio = (float)ev[e].b / 100.0f;
                p.detune = 0.5f;
                p.crunch = 0.4f;
                digital_set_params(d, &p);
            }
            e++;
        }
        int n = total - pos < split ? total - pos : split;
        if (e < nev && ev[e].at - pos < n)
            n = ev[e].at - pos;
        digital_render(d, bufL + pos, bufR + pos, n);
        pos += n;
    }
    memcpy(out, bufL, sizeof(float) * (size_t)total);
    memcpy(out + total, bufR, sizeof(float) * (size_t)total);
}

static void test_determinism_and_splits(void)
{
    static const event ev[] = {{0, 0, 60, 100}, {1000, 0, 64, 110}, {3001, 0, 67, 90}, {5000, 2, 40, 0},
                               {6000, 3, 80, 30}, {8000, 1, 60, 0}, {9000, 0, 72, 127}, {12000, 1, 64, 0},
                               {14000, 1, 67, 0}, {15000, 1, 72, 0}};
    const int nev = (int)(sizeof ev / sizeof ev[0]);
    const int total = 30000;
    static float ref[2 * 30000], got[2 * 30000];
    digital_t d;
    digital_params p = defaults();
    make(&d, 44100.0f, &p);
    run_script(&d, ev, nev, total, total, ref);
    CHECK(peak_of(ref, total) > 0.05f, "script is silent");

    static const int splits[] = {1, 7, 13, 64, 128, 1000, 4096};
    for (size_t s = 0; s < sizeof splits / sizeof splits[0]; s++) {
        make(&d, 44100.0f, &p);
        run_script(&d, ev, nev, total, splits[s], got);
        CHECK(memcmp(ref, got, sizeof ref) == 0, "output differs with block size %d", splits[s]);
    }
    /* a second instance, and the same instance after a reset */
    digital_t d2;
    make(&d2, 44100.0f, &p);
    run_script(&d2, ev, nev, total, 128, got);
    CHECK(memcmp(ref, got, sizeof ref) == 0, "a second instance differs");
    digital_set_params(&d2, &p);
    digital_reset(&d2);
    run_script(&d2, ev, nev, total, 333, got);
    CHECK(memcmp(ref, got, sizeof ref) == 0, "output differs after reset");
    printf("  bit-identical for block sizes 1, 7, 13, 64, 128, 1000, 4096, a second instance and after reset\n");
}

static void test_extremes(void)
{
    static const float rates[] = {22050.0f, 32000.0f, 44100.0f, 48000.0f, 88200.0f, 96000.0f, 192000.0f};
    float worst = 0.0f;
    for (size_t r = 0; r < sizeof rates / sizeof rates[0]; r++)
        for (int corner = 0; corner < 16; corner++) {
            digital_params p = defaults();
            p.feedback = (corner & 1) ? 1.0f : 0.0f;
            p.ratio = (corner & 2) ? 1.0f : 0.0f;
            p.detune = (corner & 4) ? 1.0f : -1.0f;
            p.crunch = (corner & 8) ? 1.0f : 0.0f;
            p.level = 1.0f;
            p.sustain = 1.0f;
            p.attack = 0.0005f;
            digital_t d;
            make(&d, rates[r], &p);
            static const int notes[10] = {0, 12, 36, 60, 84, 100, 110, 120, 126, 127};
            for (int n = 0; n < 8; n++)
                digital_note_on(&d, notes[(n * 3 + corner) % 10], 127);
            digital_mod_wheel(&d, 1.0f);
            digital_pitch_bend(&d, 1.0f);
            const int n = (int)(rates[r] * 0.25f);
            render(&d, n, 0);
            if (!all_finite(bufL, n) || !all_finite(bufR, n)) {
                CHECK(0, "sr %.0f corner %d not finite", (double)rates[r], corner);
                continue;
            }
            const float pk = fmaxf(peak_of(bufL, n), peak_of(bufR, n));
            if (pk > worst)
                worst = pk;
            CHECK(pk < 1.0f, "sr %.0f corner %d peak %.4f", (double)rates[r], corner, (double)pk);
        }
    printf("  7 sample rates 22.05 to 192 kHz, 16 knob corners, notes 0 to 127, bend and mod wheel up: all finite, worst peak %.4f\n",
           (double)worst);

    /* block size 1 end to end, mono-style use of one buffer pair */
    digital_t d;
    digital_params p = defaults();
    make(&d, 44100.0f, &p);
    digital_note_on(&d, 60, 100);
    float l, r;
    int nonzero = 0;
    for (int i = 0; i < 2000; i++) {
        digital_render(&d, &l, &r, 1);
        CHECK(isfinite(l) && isfinite(r), "block size 1 not finite");
        if (l != 0.0f)
            nonzero++;
    }
    CHECK(nonzero > 1000, "block size 1 is silent");
}

static void test_param_jumps(void)
{
    /* Each knob jumping across its range every 128 samples while a chord holds. The yardstick is the largest step
     * the chord makes by itself with that knob parked at any of nine positions. */
    const char *names[4] = {"FEEDBACK", "RATIO", "DETUNE", "CRUNCH"};
    for (int k = 0; k < 4; k++) {
        digital_params lo = defaults(), hi = defaults();
        lo.sustain = hi.sustain = 1.0f;
        float *fl[4] = {&lo.feedback, &lo.ratio, &lo.detune, &lo.crunch};
        float *fh[4] = {&hi.feedback, &hi.ratio, &hi.detune, &hi.crunch};
        *fl[k] = k == 2 ? -1.0f : 0.0f;
        *fh[k] = 1.0f;

        float stat = 0.0f;
        for (int s = 0; s < 9; s++) {
            digital_params q = defaults();
            q.sustain = 1.0f;
            float *fq[4] = {&q.feedback, &q.ratio, &q.detune, &q.crunch};
            *fq[k] = (k == 2 ? -1.0f : 0.0f) + (float)s / 8.0f * (k == 2 ? 2.0f : 1.0f);
            digital_t d;
            make(&d, 44100.0f, &q);
            digital_note_on(&d, 52, 100);
            digital_note_on(&d, 59, 100);
            digital_note_on(&d, 64, 100);
            render(&d, 20000, 0);
            const float m = max_step(bufL + 8000, 12000);
            if (m > stat)
                stat = m;
        }
        digital_t d;
        make(&d, 44100.0f, &lo);
        digital_note_on(&d, 52, 100);
        digital_note_on(&d, 59, 100);
        digital_note_on(&d, 64, 100);
        render(&d, 8000, 0);
        for (int blk = 0; blk < 150; blk++) {
            digital_set_params(&d, (blk & 1) ? &hi : &lo);
            render(&d, 128, 8000 + blk * 128);
        }
        const float jump = max_step(bufL + 8000, 150 * 128);
        printf("  %-8s jumping every 128 samples: largest step %.4f, static %.4f, ratio %.2f\n", names[k], (double)jump,
               (double)stat, (double)(jump / stat));
        CHECK(jump <= stat * 1.5f, "%s jumps step %.4f against static %.4f", names[k], (double)jump, (double)stat);
    }
}

/* ---- timing ---- */

static void bench(void) __attribute__((unused));
static void bench(void)
{
    static float l[128], r[128];
    static const float rates[] = {44100.0f};
    (void)rates;
    for (int set = 0; set < 2; set++) {
        digital_t d;
        digital_params p = defaults();
        if (set == 1) {
            p.feedback = 0.8f;
            p.ratio = 0.4f;
            p.detune = 0.5f;
            p.crunch = 0.8f;
        }
        p.sustain = 1.0f;
        make(&d, 44100.0f, &p);
        for (int n = 0; n < 8; n++)
            digital_note_on(&d, 48 + n * 4, 127);
        for (int i = 0; i < 200; i++)
            digital_render(&d, l, r, 128);
        const int blocks = 20000;
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        float acc = 0.0f;
        for (int i = 0; i < blocks; i++) {
            digital_render(&d, l, r, 128);
            acc += l[0];
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        const double us = ((double)(t1.tv_sec - t0.tv_sec) * 1e9 + (double)(t1.tv_nsec - t0.tv_nsec)) / 1000.0 / blocks;
        printf("  bench, 8 voices, 128-frame block, %s params: %.2f us per block = %.2f%% of the 2.9 ms budget (%g)\n",
               set ? "heavy" : "default", us, us / 2902.0 * 100.0, (double)acc);
    }
}

int main(void)
{
#ifndef DIGITAL_TIMING_ONLY
    printf("digital core tests\n");
    test_defaults_and_mappings();
    test_idle_is_silent();
    test_pitch();
    test_polyphony_and_headroom();
    test_release_and_pedal();
    test_steals();
    test_knobs_change_sound();
    test_sync();
    test_crunch_is_bit_flip();
    test_envelope();
    test_determinism_and_splits();
    test_extremes();
    test_param_jumps();
    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
#else
    bench();
    return 0;
#endif
}
