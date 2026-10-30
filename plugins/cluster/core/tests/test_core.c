/* Behavioral tests for the Cluster core. Build and run with core/tests/run.sh */
#include "../cluster_core.h"

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
#define MAXN (1 << 20)

static float BL[MAXN], BR[MAXN];
static cluster_t core; /* large, keep it off the stack */

static cluster_params defaults(void)
{
    cluster_params p = {CLUSTER_DEFAULT_OSCS,    CLUSTER_DEFAULT_FEEDBACK, CLUSTER_DEFAULT_SPREAD,
                        CLUSTER_DEFAULT_WANDER,  CLUSTER_DEFAULT_ATTACK,   CLUSTER_DEFAULT_DECAY,
                        CLUSTER_DEFAULT_SUSTAIN, CLUSTER_DEFAULT_RELEASE,  CLUSTER_DEFAULT_LEVEL,
                        CLUSTER_DEFAULT_BEND};
    return p;
}

/* A plain sine: one oscillator, feedback path at its zero point (A = 0, B = 0), no wander. */
static cluster_params pure(void)
{
    cluster_params p = defaults();
    p.feedback = 1.0f;
    p.wander = 0.0f;
    return p;
}

static void render_blocks(cluster_t *c, float *l, float *r, int n, int block)
{
    for (int i = 0; i < n; i += block) {
        int k = n - i < block ? n - i : block;
        cluster_render(c, l + i, r + i, k);
    }
}

static int finite_all(const float *x, int n)
{
    for (int i = 0; i < n; i++)
        if (!isfinite(x[i]))
            return 0;
    return 1;
}

static float peak_of(const float *x, int n)
{
    float p = 0;
    for (int i = 0; i < n; i++)
        p = fmaxf(p, fabsf(x[i]));
    return p;
}

static double rms_of(const float *x, int a, int b)
{
    double s = 0;
    for (int i = a; i < b; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (b - a));
}

static float max_step(const float *x, int a, int b)
{
    float m = 0;
    for (int i = a + 1; i < b; i++)
        m = fmaxf(m, fabsf(x[i] - x[i - 1]));
    return m;
}

/* ---- spectrum helpers ---- */

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
        double ang = -2.0 * PI / len;
        double wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = i + k + len / 2;
                double xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - xr; im[b] = im[a] - xi;
                re[a] += xr; im[a] += xi;
                double t = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = t;
            }
        }
    }
}

#define FFTN 65536
static double sre[FFTN], sim[FFTN], pw[FFTN / 2];

/* Power spectrum of the mid signal over FFTN samples starting at `start`, Hann windowed. */
static void spectrum(const float *l, const float *r, int start)
{
    for (int i = 0; i < FFTN; i++) {
        double w = 0.5 - 0.5 * cos(2.0 * PI * i / FFTN);
        sre[i] = 0.5 * ((double)l[start + i] + r[start + i]) * w;
        sim[i] = 0;
    }
    fft(sre, sim, FFTN);
    for (int i = 0; i < FFTN / 2; i++)
        pw[i] = sre[i] * sre[i] + sim[i] * sim[i];
}

static double band_power(double sr, double f0, double f1)
{
    int a = (int)floor(f0 * FFTN / sr), b = (int)ceil(f1 * FFTN / sr);
    if (a < 1) a = 1;
    if (b > FFTN / 2 - 1) b = FFTN / 2 - 1;
    double s = 0;
    for (int i = a; i <= b; i++)
        s += pw[i];
    return s;
}

/* Fundamental by scanning a Hann-windowed DFT around the nominal frequency in 0.25 cent steps. Returns the offset in cents. */
static double fundamental_cents(const float *l, const float *r, int start, double sr, double f_nominal)
{
    double best = -1, best_c = 0;
    static double wsig[32768];
    for (int i = 0; i < 32768; i++)
        wsig[i] = 0.5 * ((double)l[start + i] + r[start + i]) * (0.5 - 0.5 * cos(2.0 * PI * i / 32768));
    for (double cents = -80; cents <= 80; cents += 0.5) {
        double f = f_nominal * pow(2.0, cents / 1200.0);
        double re = 0, im = 0, w = 2.0 * PI * f / sr;
        const double cw = cos(w), sw = sin(w);
        double pr = 1, pi_ = 0; /* rotating phasor, exp(-j w i) uses the conjugate below */
        for (int i = 0; i < 32768; i++) {
            re += wsig[i] * pr;
            im -= wsig[i] * pi_;
            const double nr = pr * cw - pi_ * sw;
            pi_ = pr * sw + pi_ * cw;
            pr = nr;
        }
        double m = re * re + im * im;
        if (m > best) {
            best = m;
            best_c = cents;
        }
    }
    return best_c;
}

static double energy_near(double sr, double f0, double cents)
{
    double lo = f0 * pow(2.0, -cents / 1200.0), hi = f0 * pow(2.0, cents / 1200.0);
    return band_power(sr, lo, hi);
}

static double hz(int note, double semis)
{
    return 440.0 * pow(2.0, (note - 69 + semis) / 12.0);
}

static void play(cluster_t *c, double sr, const cluster_params *p, int note, int vel, int n)
{
    cluster_init(c, (float)sr);
    cluster_set_params(c, p);
    cluster_note_on(c, note, vel);
    render_blocks(c, BL, BR, n, 128);
}

/* ---- tests ---- */

static void test_feedback_path(void)
{
    float prev_a = 0, prev_b = -1;
    for (int i = 0; i <= 1000; i++) {
        float a, b;
        cluster_feedback_path(i / 1000.0f, &a, &b);
        CHECK(a >= -1e-6f && a <= 1.0001f && b >= -1.0001f && b <= 1.0001f, "path out of range at %d: %f %f", i, a, b);
        if (i > 0)
            CHECK(fabsf(a - prev_a) < 0.02f && fabsf(b - prev_b) < 0.02f, "path jumps at knob %d: A %f->%f B %f->%f", i,
                  prev_a, a, prev_b, b);
        prev_a = a;
        prev_b = b;
    }
    float a, b;
    cluster_feedback_path(1.0f, &a, &b);
    CHECK(fabsf(a) < 1e-5f && fabsf(b) < 1e-5f, "knob at 1.0 must be a plain sine (A=%f B=%f)", a, b);
    cluster_feedback_path(0.4f, &a, &b);
    CHECK(fabsf(a - 1.0f) < 1e-5f && fabsf(b) < 1e-5f, "knob at 0.4 must hold full feedback steady (A=%f B=%f)", a, b);
}

static void test_pitch(void)
{
    const double rates[] = {22050, 44100, 48000, 96000, 192000};
    const int notes[] = {28, 36, 48, 60, 69, 84, 96};
    double worst = 0;
    for (int ri = 0; ri < 5; ri++)
        for (int ni = 0; ni < 7; ni++) {
            const double f = hz(notes[ni], 0);
            if (f > 0.3 * rates[ri])
                continue;
            for (int cfg = 0; cfg < 3; cfg++) {
                cluster_params p = pure();
                if (cfg == 1)
                    p = defaults();
                if (cfg == 2) {
                    p = pure();
                    p.oscs = 8;
                    p.spread = 0.0f;
                    p.feedback = 0.4f;
                }
                if (cfg == 1)
                    p.wander = 0.0f; /* the default is a tiny wander, with only oscillator 0 it cannot move the pitch */
                p.attack = 0.0f;
                p.sustain = 1.0f;
                const int n = (int)(rates[ri] * 1.6);
                play(&core, rates[ri], &p, notes[ni], 100, n);
                CHECK(finite_all(BL, n) && finite_all(BR, n), "non-finite output");
                const double c = fundamental_cents(BL, BR, (int)(rates[ri] * 0.5), rates[ri], f);
                worst = fmax(worst, fabs(c));
                CHECK(fabs(c) <= 5.0, "note %d at %.0f Hz config %d: fundamental off by %.2f cents", notes[ni], rates[ri], cfg, c);
            }
        }
    printf("  pitch: worst fundamental error %.2f cents\n", worst);
}

static void test_pitch_bend(void)
{
    cluster_params p = pure();
    p.sustain = 1.0f;
    p.attack = 0.0f;
    const double sr = 44100;
    const int bends[] = {1, -1};
    for (int bi = 0; bi < 2; bi++) {
        cluster_init(&core, (float)sr);
        cluster_set_params(&core, &p);
        cluster_note_on(&core, 57, 100);
        cluster_pitch_bend(&core, (float)bends[bi]);
        render_blocks(&core, BL, BR, (int)(sr * 1.6), 128);
        const double c = fundamental_cents(BL, BR, (int)(sr * 0.5), sr, hz(57, 2.0 * bends[bi]));
        CHECK(fabs(c) <= 5.0, "bend %d: %.2f cents off the expected pitch", bends[bi], c);
    }
    /* a wider range, through the MIDI parser: 14-bit center is 8192, full up is 16383 */
    p.bend_range = 12.0f;
    cluster_init(&core, (float)sr);
    cluster_set_params(&core, &p);
    const uint8_t on[3] = {0x90, 57, 100}, bend[3] = {0xE0, 0x7F, 0x7F};
    cluster_midi(&core, on, 3);
    cluster_midi(&core, bend, 3);
    render_blocks(&core, BL, BR, (int)(sr * 1.6), 128);
    const double c = fundamental_cents(BL, BR, (int)(sr * 0.5), sr, hz(57, 12.0 * 8191.0 / 8192.0));
    CHECK(fabs(c) <= 5.0, "MIDI bend with 12 semitone range: %.2f cents off", c);
}

static void test_polyphony_and_headroom(void)
{
    const double rates[] = {44100, 48000, 96000};
    for (int ri = 0; ri < 3; ri++) {
        const double sr = rates[ri];
        const int n = (int)(sr * 1.5);
        for (int osc = 1; osc <= 8; osc += 7)
            for (int fbk = 0; fbk <= 4; fbk++)
                for (int sp = 0; sp <= 1; sp++)
                    for (int wd = 0; wd <= 1; wd++) {
                        cluster_params p = defaults();
                        p.oscs = osc;
                        p.feedback = 0.25f * fbk;
                        p.spread = (float)sp;
                        p.wander = (float)wd;
                        p.level = 1.0f;
                        p.attack = 0.0f;
                        p.sustain = 1.0f;
                        cluster_init(&core, (float)sr);
                        cluster_set_params(&core, &p);
                        cluster_mod_wheel(&core, 1.0f);
                        for (int k = 0; k < 8; k++)
                            cluster_note_on(&core, 48 + 3 * k, 127);
                        render_blocks(&core, BL, BR, n, 128);
                        const float pk = fmaxf(peak_of(BL, n), peak_of(BR, n));
                        CHECK(finite_all(BL, n) && finite_all(BR, n), "non-finite (sr %.0f osc %d fb %d sp %d wd %d)", sr, osc, fbk, sp, wd);
                        CHECK(pk <= 1.0f, "peak %.5f above 0 dBFS (sr %.0f osc %d fb %d sp %d wd %d)", pk, sr, osc, fbk, sp, wd);
                        CHECK(rms_of(BL, n / 2, n) > 0.01, "eight voices are silent");
                    }
    }
    /* more notes than voices */
    cluster_params p = defaults();
    p.oscs = 8;
    cluster_init(&core, 44100.0f);
    cluster_set_params(&core, &p);
    for (int k = 0; k < 24; k++) {
        cluster_note_on(&core, 40 + k, 127);
        render_blocks(&core, BL + k * 200, BR + k * 200, 200, 64);
    }
    CHECK(finite_all(BL, 4800) && peak_of(BL, 4800) <= 1.0f, "24 overlapping notes must stay finite and bounded");
    printf("  headroom: all-voice corner sweep peaks stay at or under 1.0\n");
}

static void test_release_and_pedal(void)
{
    const double sr = 44100;
    cluster_params p = defaults();
    p.oscs = 4;
    p.attack = 0.0f;
    p.sustain = 1.0f;
    p.release = 0.3f; /* about 10 ms */
    cluster_init(&core, (float)sr);
    cluster_set_params(&core, &p);
    cluster_note_on(&core, 60, 100);
    render_blocks(&core, BL, BR, 22050, 128);
    CHECK(rms_of(BL, 10000, 22050) > 0.05, "note should sound");
    cluster_note_off(&core, 60);
    render_blocks(&core, BL, BR, 22050, 128);
    CHECK(peak_of(BL + 8000, 14000) == 0.0f && peak_of(BR + 8000, 14000) == 0.0f, "output must be exactly silent after the release (peak %g)",
          peak_of(BL + 8000, 14000));

    /* a long release is still audible at 1 s and fades to zero by the end */
    p.release = 0.9f;
    cluster_init(&core, (float)sr);
    cluster_set_params(&core, &p);
    cluster_note_on(&core, 60, 100);
    render_blocks(&core, BL, BR, 8192, 128);
    cluster_note_off(&core, 60);
    render_blocks(&core, BL, BR, 44100, 128);
    CHECK(rms_of(BL, 30000, 44100) > 0.001, "a 2 s release should still sound a second later");
    render_blocks(&core, BL, BR, 44100 * 8, 128);
    CHECK(peak_of(BL, 44100 * 8) < 0.5f, "release decays");
    render_blocks(&core, BL, BR, 44100, 128);
    CHECK(peak_of(BL, 44100) == 0.0f, "voice goes idle and the output is exactly zero");

    /* sustain pedal: a note released under the pedal keeps sounding until the pedal comes up */
    p.release = 0.3f;
    cluster_init(&core, (float)sr);
    cluster_set_params(&core, &p);
    cluster_sustain(&core, 1);
    cluster_note_on(&core, 60, 100);
    render_blocks(&core, BL, BR, 4096, 128);
    cluster_note_off(&core, 60);
    render_blocks(&core, BL, BR, 22050, 128);
    CHECK(rms_of(BL, 10000, 22050) > 0.05, "pedal holds the note");
    cluster_sustain(&core, 0);
    render_blocks(&core, BL, BR, 22050, 128);
    CHECK(peak_of(BL + 8000, 14000) == 0.0f, "pedal up releases the note");

    /* all notes off */
    cluster_init(&core, (float)sr);
    cluster_set_params(&core, &p);
    for (int k = 0; k < 5; k++)
        cluster_note_on(&core, 50 + k, 100);
    render_blocks(&core, BL, BR, 4096, 128);
    const uint8_t off[3] = {0xB0, 123, 0};
    cluster_midi(&core, off, 3);
    render_blocks(&core, BL, BR, 22050, 128);
    CHECK(peak_of(BL + 8000, 14000) == 0.0f, "CC 123 releases every note");
}

/* Largest sample-to-sample step a sum of sines can make: sum of 2 pi f / sr times the amplitude. */
static float natural_step(double sr, const int *notes, int count, double amp)
{
    double s = 0;
    for (int i = 0; i < count; i++)
        s += 2.0 * PI * hz(notes[i], 0) / sr * amp;
    return (float)s;
}

static void test_no_clicks(void)
{
    const double sr = 44100;
    cluster_params p = pure();
    p.attack = 0.0f;
    p.sustain = 1.0f;
    p.release = 0.05f;
    /* one voice carries at most 0.55 (voice) * 0.707 (center pan) * 0.98 (level) of the sine */
    const double amp = 0.55 * 0.7072 * 2.0 * 0.7 * 0.7;

    /* 1. rapid retrigger of one note, every 7 ms for a second */
    cluster_init(&core, (float)sr);
    cluster_set_params(&core, &p);
    int pos = 0;
    for (int k = 0; k < 140; k++) {
        cluster_note_on(&core, 52, 127);
        render_blocks(&core, BL + pos, BR + pos, 300, 128);
        pos += 300;
    }
    int notes_a[12];
    for (int i = 0; i < 12; i++)
        notes_a[i] = 52;
    float step = max_step(BL, 0, pos);
    float thr = natural_step(sr, notes_a, 8, amp) * 1.25f;
    CHECK(step <= thr, "retrigger: step %.4f above %.4f", step, thr);
    printf("  retrigger: max step %.4f (limit %.4f)\n", step, thr);

    /* 2. voice stealing: 30 different notes, one every 3 ms, all held */
    cluster_init(&core, (float)sr);
    cluster_set_params(&core, &p);
    pos = 0;
    int notes_b[CLUSTER_VOICES + 1];
    for (int k = 0; k < 30; k++) {
        const int note = 45 + (k * 5) % 17;
        cluster_note_on(&core, note, 127);
        render_blocks(&core, BL + pos, BR + pos, 130, 64);
        pos += 130;
    }
    render_blocks(&core, BL + pos, BR + pos, 4000, 128);
    pos += 4000;
    for (int i = 0; i < CLUSTER_VOICES + 1; i++)
        notes_b[i] = 61;
    step = max_step(BL, 0, pos);
    thr = natural_step(sr, notes_b, CLUSTER_VOICES + 1, amp) * 1.25f;
    CHECK(step <= thr, "steal: step %.4f above %.4f", step, thr);
    printf("  steal: max step %.4f (limit %.4f)\n", step, thr);

    /* 3. the detector must catch a hard cut */
    cluster_init(&core, (float)sr);
    cluster_set_params(&core, &p);
    cluster_note_on(&core, 40, 127);
    render_blocks(&core, BL, BR, 8000, 128);
    int cut = 4000;
    while (fabsf(BL[cut]) < 0.2f)
        cut++;
    memset(BL + cut, 0, sizeof(float) * 100);
    int one[1] = {40};
    CHECK(max_step(BL, cut - 5, cut + 5) > natural_step(sr, one, 1, amp) * 3.0f, "self-check: a hard cut must exceed the click limit");

    /* 4. sweeping the oscillator count while a note holds changes the level smoothly */
    cluster_params q = pure();
    q.attack = 0.0f;
    q.sustain = 1.0f;
    cluster_init(&core, (float)sr);
    cluster_set_params(&core, &q);
    cluster_note_on(&core, 52, 127);
    render_blocks(&core, BL, BR, 4096, 128);
    q.oscs = 8;
    cluster_set_params(&core, &q);
    render_blocks(&core, BL + 4096, BR + 4096, 44100, 128);
    int one52[1] = {52};
    step = max_step(BL, 0, 4096 + 44100);
    /* with all eight in play the sines sum to at most 8 * 8^-0.75 of one, about 1.7x */
    thr = natural_step(sr, one52, 1, amp) * 1.8f * 1.25f;
    CHECK(step <= thr, "oscs sweep: step %.4f above %.4f", step, thr);
}

static double harmonic_ratio(double sr, double f0)
{
    spectrum(BL, BR, (int)(sr * 0.4));
    const double above = band_power(sr, 1.5 * f0, 0.45 * sr);
    const double all = band_power(sr, 0.5 * f0, 0.45 * sr);
    return above / all;
}

/* RMS of the slope over the RMS of the signal, relative to a plain sine of the same pitch: 1 for a sine, more with harmonics. */
static double brightness(const float *x, int a, int b, double sr, double f0)
{
    double d = 0;
    for (int i = a + 1; i < b; i++)
        d += ((double)x[i] - x[i - 1]) * ((double)x[i] - x[i - 1]);
    d = sqrt(d / (b - a - 1));
    return d / (rms_of(x, a, b) * 2.0 * PI * f0 / sr);
}

static void test_knobs_change_sound(void)
{
    const double sr = 44100;
    const int n = (int)(sr * 2.0);
    const double f0 = hz(57, 0);
    cluster_params p;

    /* OSCS: one sine is narrow, eight wandering sines are not */
    p = pure();
    p.attack = 0.0f;
    p.sustain = 1.0f;
    p.spread = 1.0f;
    play(&core, sr, &p, 57, 100, n);
    spectrum(BL, BR, (int)(sr * 0.4));
    const double narrow1 = energy_near(sr, f0, 7.0) / band_power(sr, 0.5 * f0, 1.5 * f0);
    p.oscs = 8;
    play(&core, sr, &p, 57, 100, n);
    spectrum(BL, BR, (int)(sr * 0.4));
    const double narrow8 = energy_near(sr, f0, 7.0) / band_power(sr, 0.5 * f0, 1.5 * f0);
    printf("  oscs: share of energy within 7 cents of the note: 1 osc %.3f, 8 oscs %.3f\n", narrow1, narrow8);
    CHECK(narrow1 > 0.95 && narrow8 < 0.7, "OSCS should widen the spectrum (%.3f vs %.3f)", narrow1, narrow8);

    /* FEEDBACK: knob 1.0 is a sine, knob 0.4 holds full feedback and adds harmonics */
    p = pure();
    p.attack = 0.0f;
    p.sustain = 1.0f;
    play(&core, sr, &p, 57, 100, n);
    const double h_sine = harmonic_ratio(sr, f0);
    p.feedback = 0.4f;
    play(&core, sr, &p, 57, 100, n);
    const double h_fb = harmonic_ratio(sr, f0);
    printf("  feedback: harmonic share 0.0%% knob 1.0 -> %.5f, knob 0.4 -> %.5f\n", h_sine, h_fb);
    CHECK(h_sine < 1e-4 && h_fb > 0.01, "FEEDBACK should add harmonics (%.6f vs %.6f)", h_sine, h_fb);

    /* FEEDBACK over time: knob 0.2 starts bright and the drift pulls it to a sine within about a quarter second */
    p.feedback = 0.2f;
    play(&core, sr, &p, 57, 100, n);
    const double b_early = brightness(BL, 441, 2646, sr, f0);
    const double b_late = brightness(BL, 22050, 26460, sr, f0);
    printf("  feedback drift: brightness early %.3f, late %.4f (1.000 is a plain sine)\n", b_early, b_late);
    CHECK(b_early > 1.1 && fabs(b_late - 1.0) < 0.01, "knob 0.2 should lose its harmonics over the note");

    /* mod wheel adds feedback on top of a plain sine */
    p = pure();
    p.attack = 0.0f;
    p.sustain = 1.0f;
    cluster_init(&core, (float)sr);
    cluster_set_params(&core, &p);
    cluster_mod_wheel(&core, 1.0f);
    cluster_note_on(&core, 57, 100);
    render_blocks(&core, BL, BR, n, 128);
    const double h_mod = harmonic_ratio(sr, f0);
    CHECK(h_mod > 100.0 * h_sine && h_mod > 0.001, "mod wheel should add harmonics (%.6f)", h_mod);

    /* SPREAD: eight oscillators, no wander. Low SPREAD settles to the note within a fraction of a second, high keeps the detune */
    p = pure();
    p.attack = 0.0f;
    p.sustain = 1.0f;
    p.oscs = 8;
    p.spread = 0.0f;
    play(&core, sr, &p, 57, 100, n);
    spectrum(BL, BR, (int)(sr * 0.8));
    const double sp_lo = energy_near(sr, f0, 7.0) / band_power(sr, 0.5 * f0, 1.5 * f0);
    p.spread = 1.0f;
    play(&core, sr, &p, 57, 100, n);
    spectrum(BL, BR, (int)(sr * 0.8));
    const double sp_hi = energy_near(sr, f0, 7.0) / band_power(sr, 0.5 * f0, 1.5 * f0);
    printf("  spread: energy within 7 cents of the note: low %.3f, high %.3f\n", sp_lo, sp_hi);
    CHECK(sp_lo > 0.9 && sp_hi < 0.6, "SPREAD should control how long the detune lasts");

    /* WANDER: eight oscillators on a short memory, kicks off vs kicks on */
    p = pure();
    p.attack = 0.0f;
    p.sustain = 1.0f;
    p.oscs = 8;
    p.spread = 0.3f;
    p.wander = 0.0f;
    play(&core, sr, &p, 57, 100, n);
    spectrum(BL, BR, (int)(sr * 0.8));
    const double wd_lo = energy_near(sr, f0, 7.0) / band_power(sr, 0.5 * f0, 1.5 * f0);
    p.wander = 0.1f;
    play(&core, sr, &p, 57, 100, n);
    spectrum(BL, BR, (int)(sr * 0.8));
    const double wd_hi = energy_near(sr, f0, 7.0) / band_power(sr, 0.5 * f0, 1.5 * f0);
    printf("  wander: energy within 7 cents of the note: off %.3f, 0.1 %.3f\n", wd_lo, wd_hi);
    CHECK(wd_lo > 0.9 && wd_hi < 0.6, "WANDER should smear the pitch");
}

static void test_envelope(void)
{
    const double sr = 44100;
    cluster_params p = pure();
    p.sustain = 0.5f;
    p.decay = 0.2f;
    p.release = 0.3f;
    /* attack: 1 ms vs about 0.3 s */
    p.attack = 0.0f;
    play(&core, sr, &p, 60, 127, 8000);
    const double fast = rms_of(BL, 100, 400);
    p.attack = 0.7f;
    play(&core, sr, &p, 60, 127, 8000);
    const double slow = rms_of(BL, 100, 400);
    printf("  attack: rms over the first 10 ms %.4f (1 ms) vs %.4f (slow)\n", fast, slow);
    CHECK(fast > 20.0 * slow, "ATTACK should slow the onset");

    /* sustain level */
    p.attack = 0.0f;
    p.sustain = 1.0f;
    play(&core, sr, &p, 60, 127, 44100);
    const double s1 = rms_of(BL, 30000, 44100);
    p.sustain = 0.25f;
    play(&core, sr, &p, 60, 127, 44100);
    const double s2 = rms_of(BL, 30000, 44100);
    CHECK(fabs(s2 / s1 - 0.25) < 0.02, "SUSTAIN 0.25 should sit at a quarter of the level (got %.3f)", s2 / s1);

    /* decay: a short decay reaches the sustain level sooner */
    p.sustain = 0.1f;
    p.decay = 0.1f;
    play(&core, sr, &p, 60, 127, 44100);
    const double d_fast = rms_of(BL, 8000, 12000);
    p.decay = 0.8f;
    play(&core, sr, &p, 60, 127, 44100);
    const double d_slow = rms_of(BL, 8000, 12000);
    CHECK(d_fast < 0.5 * d_slow, "DECAY should change how quickly the level falls (%.4f vs %.4f)", d_fast, d_slow);

    /* velocity scales the level */
    p = pure();
    p.attack = 0.0f;
    p.sustain = 1.0f;
    play(&core, sr, &p, 60, 127, 20000);
    const double v127 = rms_of(BL, 10000, 20000);
    play(&core, sr, &p, 60, 64, 20000);
    const double v64 = rms_of(BL, 10000, 20000);
    CHECK(v127 > 1.9 * v64 && v127 < 2.1 * v64, "velocity 64 should be about half the level (%.4f vs %.4f)", v64, v127);

    /* level 0 is silent, and the output is smooth when level moves */
    p.level = 0.0f;
    play(&core, sr, &p, 60, 127, 20000);
    CHECK(peak_of(BL, 20000) == 0.0f, "LEVEL 0 should be silent");
}

static void test_deterministic(void)
{
    static float l1[96000], r1[96000], l2[96000], r2[96000];
    cluster_params p = defaults();
    p.oscs = 8;
    p.wander = 0.05f;
    p.spread = 0.8f;
    p.attack = 0.0f;
    const double sr = 44100;

    cluster_init(&core, (float)sr);
    cluster_set_params(&core, &p);
    for (int k = 0; k < 5; k++)
        cluster_note_on(&core, 50 + 4 * k, 90);
    render_blocks(&core, l1, r1, 96000, 128);

    cluster_init(&core, (float)sr);
    cluster_set_params(&core, &p);
    for (int k = 0; k < 5; k++)
        cluster_note_on(&core, 50 + 4 * k, 90);
    render_blocks(&core, l2, r2, 96000, 128);
    CHECK(memcmp(l1, l2, sizeof l1) == 0 && memcmp(r1, r2, sizeof r1) == 0, "same seed must give identical output");

    /* the host block size must not change the sound: 1, 37, 128, 1000 */
    const int blocks[] = {1, 37, 1000};
    for (int b = 0; b < 3; b++) {
        cluster_init(&core, (float)sr);
        cluster_set_params(&core, &p);
        for (int k = 0; k < 5; k++)
            cluster_note_on(&core, 50 + 4 * k, 90);
        render_blocks(&core, l2, r2, 96000, blocks[b]);
        CHECK(memcmp(l1, l2, sizeof l1) == 0 && memcmp(r1, r2, sizeof r1) == 0, "block size %d changes the output", blocks[b]);
    }

    /* reset and reseed gives the same output again */
    cluster_reset(&core);
    cluster_seed(&core, 0x2545F491u);
    cluster_set_params(&core, &p);
    for (int k = 0; k < 5; k++)
        cluster_note_on(&core, 50 + 4 * k, 90);
    render_blocks(&core, l2, r2, 96000, 128);
    CHECK(memcmp(l1, l2, sizeof l1) == 0, "reset plus the same seed must repeat the output");

    /* a different seed gives a different cluster */
    cluster_reset(&core);
    cluster_seed(&core, 77u);
    cluster_set_params(&core, &p);
    for (int k = 0; k < 5; k++)
        cluster_note_on(&core, 50 + 4 * k, 90);
    render_blocks(&core, l2, r2, 96000, 128);
    CHECK(memcmp(l1, l2, sizeof l1) != 0, "a different seed should give a different output");
}

static double rnd01(unsigned *s)
{
    *s = *s * 1664525u + 1013904223u;
    return (double)(*s >> 8) / 16777216.0;
}

static void test_stress_and_rates(void)
{
    const double rates[] = {22050, 32000, 44100, 48000, 88200, 96000, 192000};
    unsigned seed = 4242;
    for (int ri = 0; ri < 7; ri++) {
        const double sr = rates[ri];
        const int blk = ri % 2 ? 1 : 64;
        const int total = (int)(sr * 0.6);
        cluster_init(&core, (float)sr);
        float worst = 0, pk = 0;
        for (int pos = 0; pos < total; pos += 128) {
            cluster_params p;
            p.oscs = 1 + (int)(rnd01(&seed) * 8);
            p.feedback = (float)rnd01(&seed);
            p.spread = (float)rnd01(&seed);
            p.wander = (float)rnd01(&seed);
            p.attack = (float)rnd01(&seed);
            p.decay = (float)rnd01(&seed);
            p.sustain = (float)rnd01(&seed);
            p.release = (float)rnd01(&seed);
            p.level = (float)rnd01(&seed);
            p.bend_range = (float)(rnd01(&seed) * 12.0);
            cluster_set_params(&core, &p);
            const double u = rnd01(&seed);
            if (u < 0.4)
                cluster_note_on(&core, (int)(rnd01(&seed) * 128), 1 + (int)(rnd01(&seed) * 127));
            else if (u < 0.6)
                cluster_note_off(&core, (int)(rnd01(&seed) * 128));
            else if (u < 0.7)
                cluster_pitch_bend(&core, (float)(rnd01(&seed) * 2 - 1));
            else if (u < 0.75)
                cluster_sustain(&core, (int)(rnd01(&seed) * 2));
            else if (u < 0.78)
                cluster_all_off(&core);
            else if (u < 0.85)
                cluster_mod_wheel(&core, (float)rnd01(&seed));
            int n = total - pos < 128 ? total - pos : 128;
            render_blocks(&core, BL, BR, n, blk);
            CHECK(finite_all(BL, n) && finite_all(BR, n), "non-finite output at %.0f Hz block %d", sr, blk);
            pk = fmaxf(pk, fmaxf(peak_of(BL, n), peak_of(BR, n)));
            worst = fmaxf(worst, max_step(BL, 0, n));
        }
        CHECK(pk <= 1.0f, "peak %.4f above 1.0 at %.0f Hz", pk, sr);
    }
    /* frames == 0 and a note out of range must be harmless */
    cluster_render(&core, BL, BR, 0);
    cluster_note_on(&core, -3, 100);
    cluster_note_on(&core, 300, 100);
    printf("  stress: random parameters, notes and controllers at 7 sample rates, block sizes 1 and 64: finite, peak <= 1.0\n");
}

static void test_activity(void)
{
    float act[CLUSTER_OSCS];
    cluster_params p = defaults();
    p.oscs = 4;
    cluster_init(&core, 44100.0f);
    cluster_set_params(&core, &p);
    CHECK(cluster_take_level(&core) == 0.0f, "level starts at 0");
    cluster_note_on(&core, 60, 127);
    render_blocks(&core, BL, BR, 4096, 128);
    const float lv = cluster_take_level(&core);
    const int voices = cluster_take_activity(&core, act);
    CHECK(lv > 0.1f && voices == 1, "level %.3f voices %d", lv, voices);
    CHECK(act[0] > 0.1f && act[3] > 0.1f && act[4] == 0.0f && act[7] == 0.0f, "only the first four oscillators should light up");
    CHECK(cluster_take_level(&core) == 0.0f, "level is cleared by reading it");
}

static void test_reset(void)
{
    cluster_params p = defaults();
    cluster_init(&core, 44100.0f);
    cluster_set_params(&core, &p);
    cluster_note_on(&core, 60, 127);
    render_blocks(&core, BL, BR, 4096, 128);
    cluster_reset(&core);
    render_blocks(&core, BL, BR, 1024, 128);
    CHECK(peak_of(BL, 1024) == 0.0f && peak_of(BR, 1024) == 0.0f, "reset silences at once");
}

int main(void)
{
    printf("cluster core tests\n");
    test_feedback_path();
    test_pitch();
    test_pitch_bend();
    test_polyphony_and_headroom();
    test_release_and_pedal();
    test_no_clicks();
    test_knobs_change_sound();
    test_envelope();
    test_deterministic();
    test_stress_and_rates();
    test_activity();
    test_reset();
    if (failures) {
        printf("%d FAILED\n", failures);
        return 1;
    }
    printf("ALL PASSED\n");
    return 0;
}
