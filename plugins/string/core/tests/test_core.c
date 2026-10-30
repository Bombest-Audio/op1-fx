/* Behavioral tests for the String core. Build and run with core/tests/run.sh */
#include "../string_core.h"

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

/* The instance is about 0.5 MB, so keep it off the stack. */
static opst_t g_core;
static opst_t g_core2;
enum { MAXN = 192000 * 3 };
static float g_l[MAXN], g_r[MAXN], g_l2[MAXN], g_r2[MAXN];

static opst_params defaults(void)
{
    opst_params p = {OPST_DEFAULT_PICK,    OPST_DEFAULT_DAMP,    OPST_DEFAULT_STIFF,   OPST_DEFAULT_DECAY, OPST_DEFAULT_ATTACK,
                     OPST_DEFAULT_ENV_DECAY, OPST_DEFAULT_SUSTAIN, OPST_DEFAULT_RELEASE, OPST_DEFAULT_LEVEL};
    return p;
}

static float note_hz(int note)
{
    return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f);
}

static void fresh(opst_t *s, float sr, const opst_params *p)
{
    opst_init(s, sr);
    opst_set_params(s, p);
    opst_reset(s);
}

static void render_blocks(opst_t *s, float *l, float *r, int n, int blk)
{
    for (int i = 0; i < n; i += blk) {
        int c = n - i < blk ? n - i : blk;
        opst_render(s, l + i, r + i, c);
    }
}

typedef struct {
    int at;
    int type; /* 0 note on, 1 note off, 2 bend (a / 1000 semitones), 3 mod (a / 1000), 4 pedal, 5 all off */
    int a, b;
} ev_t;

/* Render n frames, applying the events at their exact sample positions (events must be sorted). */
static void run_script(opst_t *s, const ev_t *ev, int nev, float *l, float *r, int n, int blk)
{
    int pos = 0, e = 0;
    while (pos < n) {
        while (e < nev && ev[e].at <= pos) {
            switch (ev[e].type) {
            case 0: opst_note_on(s, ev[e].a, ev[e].b); break;
            case 1: opst_note_off(s, ev[e].a); break;
            case 2: opst_pitch_bend(s, (float)ev[e].a / 1000.0f); break;
            case 3: opst_mod_wheel(s, (float)ev[e].a / 1000.0f); break;
            case 4: opst_sustain(s, ev[e].a); break;
            case 5: opst_all_off(s); break;
            }
            e++;
        }
        int end = n;
        if (e < nev && ev[e].at < end)
            end = ev[e].at;
        if (end - pos > blk)
            end = pos + blk;
        opst_render(s, l + pos, r + pos, end - pos);
        pos = end;
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

static int all_finite(const float *x, int n)
{
    for (int i = 0; i < n; i++)
        if (!isfinite(x[i]))
            return 0;
    return 1;
}

static float max_step(const float *x, int from, int to)
{
    float m = 0;
    for (int i = from < 1 ? 1 : from; i < to; i++)
        m = fmaxf(m, fabsf(x[i] - x[i - 1]));
    return m;
}

/* Hann-windowed DFT magnitude of x[from .. from+n) at one frequency. */
static double dft_mag(const float *x, int from, int n, double freq, double sr)
{
    double re = 0, im = 0;
    const double w = 2.0 * PI * freq / sr;
    for (int i = 0; i < n; i++) {
        double win = 0.5 - 0.5 * cos(2.0 * PI * (double)i / (double)n);
        double ph = w * (double)i;
        re += win * x[from + i] * cos(ph);
        im -= win * x[from + i] * sin(ph);
    }
    return sqrt(re * re + im * im) / n;
}

/* Frequency of the strongest component within +-60 cents of the expected pitch, found by a 1 cent scan
 * and a parabolic refinement. */
static double measure_f0(const float *x, int from, int n, double expected, double sr)
{
    enum { SPAN = 60 };
    double mag[2 * SPAN + 1];
    int best = 0;
    for (int c = -SPAN; c <= SPAN; c++) {
        mag[c + SPAN] = dft_mag(x, from, n, expected * pow(2.0, c / 1200.0), sr);
        if (mag[c + SPAN] > mag[best])
            best = c + SPAN;
    }
    double cents = best - SPAN;
    if (best > 0 && best < 2 * SPAN) {
        double a = mag[best - 1], b = mag[best], c = mag[best + 1];
        double den = a - 2 * b + c;
        if (den != 0)
            cents += 0.5 * (a - c) / den;
    }
    return expected * pow(2.0, cents / 1200.0);
}

static double cents_between(double f, double expected)
{
    return 1200.0 * log2(f / expected);
}

/* ---------------------------------------------------------------------------------------------------------- */

static void test_pitch_accuracy(void)
{
    const float rates[] = {22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f};
    const int notes[] = {24, 36, 48, 60, 72, 84, 96, 108};
    double worst = 0;
    int count = 0;
    for (int ri = 0; ri < 5; ri++) {
        const float sr = rates[ri];
        for (int ni = 0; ni < 8; ni++) {
            const float f = note_hz(notes[ni]);
            if (f > sr / 8.0f * 0.98f)
                continue; /* above the highest tuned pitch, documented in docs/DSP.md */
            opst_params p = defaults();
            fresh(&g_core, sr, &p);
            opst_note_on(&g_core, notes[ni], 100);
            const int n = (int)(sr * 0.9f);
            render_blocks(&g_core, g_l, g_r, n, 128);
            const int from = (int)(sr * 0.15f), len = (int)(sr * 0.5f);
            const double m = measure_f0(g_l, from, len, f, sr);
            const double err = cents_between(m, f);
            worst = fmax(worst, fabs(err));
            count++;
            CHECK(fabs(err) < 5.0, "note %d at %.0f Hz sr: measured %.3f Hz for %.3f Hz, %.2f cents off", notes[ni], sr, m, f, err);
        }
    }
    printf("  pitch: %d note/rate cases, worst error %.2f cents\n", count, worst);
}

static void test_pitch_with_other_settings(void)
{
    /* The tuning has to hold when DAMP and STIFF change the loop filters, and at both extremes of DECAY. */
    const float sr = 44100.0f;
    const int notes[] = {36, 60, 84};
    double worst = 0;
    for (int ni = 0; ni < 3; ni++)
        for (int k = 0; k < 6; k++) {
            opst_params p = defaults();
            p.damp = (k == 0 || k == 3) ? 0.15f : (k == 1 || k == 4) ? 1.0f : 0.5f;
            p.stiff = k < 3 ? 0.0f : 1.0f;
            p.decay = (k == 2) ? 1.0f : 0.8f;
            fresh(&g_core, sr, &p);
            opst_note_on(&g_core, notes[ni], 110);
            render_blocks(&g_core, g_l, g_r, (int)(sr * 1.0f), 100);
            const float f = note_hz(notes[ni]);
            /* with STIFF the fundamental is still the lowest partial and is tuned like any other note */
            /* a dark high note is short, so measure early */
            const double m = measure_f0(g_l, (int)(sr * 0.03f), (int)(sr * 0.12f), f, sr);
            const double err = cents_between(m, f);
            worst = fmax(worst, fabs(err));
            CHECK(fabs(err) < 5.0, "note %d damp %.1f stiff %.1f: %.2f cents off", notes[ni], p.damp, p.stiff, err);
        }
    printf("  pitch with damp/stiff/decay changed: worst error %.2f cents\n", worst);
}

static void test_polyphony_and_headroom(void)
{
    const float rates[] = {44100.0f, 48000.0f, 96000.0f};
    float worst = 0;
    for (int ri = 0; ri < 3; ri++) {
        const float sr = rates[ri];
        opst_params p = defaults();
        p.damp = 1.0f;
        p.decay = 1.0f;
        p.level = 1.0f;
        fresh(&g_core, sr, &p);
        /* eight notes at full velocity at once, then a second set that steals every voice */
        for (int i = 0; i < 8; i++)
            opst_note_on(&g_core, 40 + 3 * i, 127);
        CHECK(opst_active_voices(&g_core) == 8, "8 notes should use 8 voices (%d)", opst_active_voices(&g_core));
        const int n = (int)(sr * 1.5f);
        render_blocks(&g_core, g_l, g_r, n / 2, 128);
        for (int i = 0; i < 8; i++)
            opst_note_on(&g_core, 52 + 2 * i, 127);
        render_blocks(&g_core, g_l + n / 2, g_r + n / 2, n - n / 2, 128);
        const float pk = fmaxf(peak(g_l, n), peak(g_r, n));
        worst = fmaxf(worst, pk);
        CHECK(all_finite(g_l, n) && all_finite(g_r, n), "NaN or Inf with 8 voices at %.0f Hz", sr);
        CHECK(pk < 1.0f, "peak %.4f reaches 0 dBFS (sr %.0f)", pk, sr);
        CHECK(pk > 0.3f, "eight full-velocity notes are too quiet: peak %.4f", pk);
    }
    printf("  8 voices at velocity 127, DAMP and DECAY at maximum: worst peak %.4f (%.1f dBFS)\n", worst,
           20.0 * log10(worst));

    /* a single note at mid velocity should sit at a usable level */
    opst_params p = defaults();
    fresh(&g_core, 44100.0f, &p);
    opst_note_on(&g_core, 60, 100);
    render_blocks(&g_core, g_l, g_r, 44100, 128);
    const float pk1 = peak(g_l, 44100);
    printf("  single note 60 velocity 100: peak %.3f\n", pk1);
    CHECK(pk1 > 0.1f && pk1 < 0.95f, "single note level %.3f outside 0.1..0.95", pk1);
}

static void test_note_off_releases_to_silence(void)
{
    const float sr = 44100.0f;
    opst_params p = defaults();
    p.env_release = 0.2f;
    p.decay = 1.0f; /* the string alone would ring for 24 s, so silence can only come from the release */
    fresh(&g_core, sr, &p);
    ev_t ev[] = {{0, 0, 60, 110}, {0, 0, 67, 110}, {22050, 1, 60, 0}, {22050, 1, 67, 0}};
    const int n = 44100 * 2;
    run_script(&g_core, ev, 4, g_l, g_r, n, 128);
    const float before = rms(g_l + 4000, 2000);
    const int t = 22050 + (int)(sr * 0.5f);
    const float tail = peak(g_l + t, n - t);
    printf("  note-off: rms before %.4f, peak from 0.5 s after note-off %.2e, active voices %d\n", before, tail,
           opst_active_voices(&g_core));
    CHECK(before > 0.01f, "the held notes should be audible (%.4f)", before);
    CHECK(tail < 1e-4f, "output after the release is %.2e, not silent", tail);
    CHECK(opst_active_voices(&g_core) == 0, "voices should be free after the release");
}

static void test_string_dies_by_itself(void)
{
    /* With a short DECAY and a held key the voice must free itself. */
    const float sr = 44100.0f;
    opst_params p = defaults();
    p.decay = 0.0f;
    fresh(&g_core, sr, &p);
    opst_note_on(&g_core, 64, 100);
    render_blocks(&g_core, g_l, g_r, (int)(sr * 3.0f), 128);
    CHECK(opst_active_voices(&g_core) == 0, "a decayed string should release its voice (%d active)", opst_active_voices(&g_core));
    CHECK(peak(g_l + (int)(sr * 2.5f), (int)(sr * 0.5f)) < 1e-4f, "decayed string not silent");
}

static void test_sustain_pedal_and_all_off(void)
{
    const float sr = 44100.0f;
    opst_params p = defaults();
    p.env_release = 0.05f;
    p.decay = 0.9f;
    fresh(&g_core, sr, &p);
    ev_t ev[] = {{0, 4, 1, 0}, {0, 0, 60, 100}, {4410, 1, 60, 0}, {44100, 4, 0, 0}};
    const int n = 44100 * 2;
    run_script(&g_core, ev, 4, g_l, g_r, n, 128);
    const float held = rms(g_l + 30000, 4000);        /* pedal down, key released at 0.1 s */
    const float after = peak(g_l + 44100 + 8000, 8000); /* 0.18 s after the pedal comes up */
    printf("  pedal: rms while held by pedal %.4f, peak after pedal up %.2e\n", held, after);
    CHECK(held > 0.002f, "pedal should keep the note sounding (%.4f)", held);
    CHECK(after < 1e-3f, "note should release when the pedal comes up (%.2e)", after);

    fresh(&g_core, sr, &p);
    ev_t ev2[] = {{0, 0, 60, 100}, {0, 0, 64, 100}, {0, 0, 67, 100}, {8820, 5, 0, 0}};
    run_script(&g_core, ev2, 4, g_l, g_r, n, 128);
    CHECK(opst_active_voices(&g_core) == 0, "all notes off should end every voice (%d)", opst_active_voices(&g_core));
    CHECK(peak(g_l + 8820 + 8000, 8000) < 1e-3f, "all notes off not silent");
}

static void test_steal_and_retrigger_without_clicks(void)
{
    const float sr = 44100.0f;
    /* Dark and smooth notes so a click would stand out: a low DAMP leaves nearly a sine after a few milliseconds. */
    opst_params p = defaults();
    p.damp = 0.1f;
    p.decay = 0.9f;
    p.env_sustain = 1.0f;
    fresh(&g_core, sr, &p);
    ev_t ev[10];
    int ne = 0;
    for (int i = 0; i < 8; i++) {
        ev[ne].at = 0; ev[ne].type = 0; ev[ne].a = 36 + 4 * i; ev[ne].b = 127; ne++;
    }
    /* Steal at ten different phases of the oldest (lowest) voice, which has a period of 535 samples. */
    float worst_ratio = 0, worst_during = 0, worst_steady = 0, worst_peak = 1;
    for (int k = 0; k < 10; k++) {
        const int steal_at = 6000 + k * 55;
        ev_t evk[10];
        memcpy(evk, ev, sizeof(ev_t) * 8);
        /* velocity 1 so the new note is nearly silent and the only thing that could click is the old voice being cut */
        evk[8].at = steal_at; evk[8].type = 0; evk[8].a = 90; evk[8].b = 1;
        fresh(&g_core, sr, &p);
        run_script(&g_core, evk, 9, g_l, g_r, steal_at + 400, 128);
        if (k == 0) {
            CHECK(opst_active_voices(&g_core) == 8, "still 8 voices once the steal has finished (%d)", opst_active_voices(&g_core));
            int started = 0;
            for (int i = 0; i < OPST_VOICES; i++) {
                int note;
                float lvl;
                if (opst_voice_info(&g_core, i, &note, &lvl) && note == 90)
                    started = 1;
            }
            CHECK(started, "the stealing note should own a voice after the fade");
        }
        const float steady = max_step(g_l, steal_at - 1000, steal_at);
        const float during = max_step(g_l, steal_at, steal_at + 400);
        CHECK(during < steady * 1.5f + 1e-4f, "steal at %d made a step of %.5f (normal %.5f)", steal_at, during, steady);
        worst_during = fmaxf(worst_during, during);
        worst_steady = fmaxf(worst_steady, steady);

        /* Isolate the stolen voice: render the same eight notes with no steal and subtract. The difference is the
         * stolen voice's own signal times the fade. A hard cut would make it jump to the voice's instantaneous level in
         * one sample; a 2.5 ms fade moves it by about 1/110 of its level per sample. */
        fresh(&g_core2, sr, &p);
        run_script(&g_core2, evk, 8, g_l2, g_r2, steal_at + 400, 128);
        float diff_peak = 0, diff_step = 0;
        for (int i = steal_at; i < steal_at + 400; i++) {
            const float d = g_l[i] - g_l2[i];
            const float dp = g_l[i - 1] - g_l2[i - 1];
            diff_peak = fmaxf(diff_peak, fabsf(d));
            diff_step = fmaxf(diff_step, fabsf(d - dp));
        }
        CHECK(diff_peak > 0.01f, "stolen voice should be audible when it is taken (%.4f)", diff_peak);
        worst_ratio = fmaxf(worst_ratio, diff_step / diff_peak);
        worst_peak = fminf(worst_peak, diff_peak);
        CHECK(diff_step < 0.1f * diff_peak, "steal at %d cut the voice instead of fading it: step %.5f of %.5f", steal_at, diff_step, diff_peak);
    }
    printf("  steal at 10 phases: largest mix step %.5f against %.5f normally; the stolen voice moves by at most %.1f%% of its level per sample\n",
           worst_during, worst_steady, 100.0 * worst_ratio);

    /* Same note again while it is held. The old voice keeps ringing out through its release. */
    fresh(&g_core, sr, &p);
    ev_t ev2[] = {{0, 0, 48, 127}, {6000, 0, 48, 1}};
    run_script(&g_core, ev2, 2, g_l, g_r, 12000, 128);
    const float s2 = max_step(g_l, 5000, 6000);
    const float d2 = max_step(g_l, 6000, 7500);
    const float lvl2 = peak(g_l + 5500, 500);
    printf("  retrigger of a held note: step %.5f before, %.5f across; the held note itself peaks at %.4f\n", s2, d2, lvl2);
    CHECK(d2 < fmaxf(s2 * 1.5f, 0.05f * lvl2) + 1e-4f, "retrigger clicked: %.5f against %.5f", d2, s2);

    /* The onset of a pluck from silence starts at zero and ramps over the attack, it does not jump. */
    fresh(&g_core, sr, &p);
    opst_note_on(&g_core, 60, 127);
    render_blocks(&g_core, g_l, g_r, 4096, 128);
    printf("  pluck onset: first sample %.2e, first 4 samples peak %.2e, overall peak %.3f\n", fabsf(g_l[0]), peak(g_l, 4), peak(g_l, 4096));
    CHECK(fabsf(g_l[0]) < 0.01f, "first sample %.4f should start near zero", g_l[0]);
}

static void test_knobs_change_the_sound(void)
{
    const float sr = 44100.0f;
    const int note = 48;
    const float f0 = note_hz(note);
    const int from = 2205, len = 8820; /* 50 ms in, 200 ms long */

    /* PICK at the middle of the string removes the even partials of the pluck */
    double ratio[2];
    for (int k = 0; k < 2; k++) {
        opst_params p = defaults();
        p.damp = 0.8f;
        p.pick = k == 0 ? 0.0f : 1.0f;
        fresh(&g_core, sr, &p);
        opst_note_on(&g_core, note, 110);
        render_blocks(&g_core, g_l, g_r, 22050, 128);
        const double m1 = dft_mag(g_l, from, len, f0, sr);
        const double m2 = dft_mag(g_l, from, len, 2.0 * f0, sr);
        ratio[k] = m2 / m1;
    }
    printf("  PICK: 2nd partial relative to the fundamental %.3f near the bridge, %.3f at the middle (%.1f dB lower)\n", ratio[0],
           ratio[1], 20.0 * log10(ratio[0] / ratio[1]));
    CHECK(ratio[0] > ratio[1] * 3.0, "PICK at the middle should cut the 2nd partial by at least 10 dB");

    /* DAMP: brightness. Compare the energy of the first difference to the energy of the signal. */
    double bright[2];
    for (int k = 0; k < 2; k++) {
        opst_params p = defaults();
        p.damp = k == 0 ? 0.1f : 0.9f;
        fresh(&g_core, sr, &p);
        opst_note_on(&g_core, note, 110);
        render_blocks(&g_core, g_l, g_r, 22050, 128);
        double num = 0, den = 0;
        for (int i = from + 1; i < from + len; i++) {
            double d = g_l[i] - g_l[i - 1];
            num += d * d;
            den += (double)g_l[i] * g_l[i];
        }
        bright[k] = sqrt(num / den);
    }
    printf("  DAMP: high-frequency content (rms of the difference over rms) %.3f at 0.1, %.3f at 0.9\n", bright[0], bright[1]);
    CHECK(bright[1] > bright[0] * 2.0, "DAMP up should make the tone at least twice as bright");

    /* STIFF: the 12th partial moves away from 12 x the fundamental */
    double dev[2];
    for (int k = 0; k < 2; k++) {
        opst_params p = defaults();
        p.damp = 1.0f;
        p.decay = 0.8f;
        p.pick = 0.1f;
        p.stiff = k == 0 ? 0.0f : 1.0f;
        fresh(&g_core, sr, &p);
        opst_note_on(&g_core, note, 110);
        render_blocks(&g_core, g_l, g_r, 44100, 128);
        const double fm = measure_f0(g_l, 4410, 22050, 12.0 * f0, sr);
        dev[k] = cents_between(fm, 12.0 * f0);
    }
    printf("  STIFF: 12th partial %.1f cents from a harmonic at 0, %.1f cents at 1\n", dev[0], dev[1]);
    CHECK(fabs(dev[0]) < 6.0, "with STIFF 0 the partials should be harmonic (%.1f cents)", dev[0]);
    CHECK(fabs(dev[1]) > 15.0, "STIFF 1 should move the 12th partial by at least 15 cents (%.1f)", dev[1]);

    /* DECAY: ring time of the fundamental. Measure the level drop between two windows and compare with the formula. */
    for (int k = 0; k < 3; k++) {
        const float dn = k == 0 ? 0.25f : (k == 1 ? 0.5f : 0.75f);
        opst_params p = defaults();
        p.damp = 0.7f;
        p.decay = dn;
        fresh(&g_core, sr, &p);
        opst_note_on(&g_core, 40, 110);
        const int n = (int)(sr * 4.0f);
        render_blocks(&g_core, g_l, g_r, n, 128);
        const float t60 = opst_decay_seconds(dn);
        const double fq = note_hz(40);
        const double dt = fmin(0.5, 0.45 * t60);
        const int w = (int)(sr * 0.08f);
        const int t1 = (int)(sr * 0.03f), t2 = t1 + (int)(sr * dt);
        const double m1 = dft_mag(g_l, t1, w, fq, sr), m2 = dft_mag(g_l, t2, w, fq, sr);
        const double measured_db = 20.0 * log10(m2 / m1);
        const double expected_db = -60.0 * dt / t60;
        printf("  DECAY %.2f: ring time %.2f s, fundamental drops %.1f dB in %.2f s (formula %.1f dB)\n", dn, t60, measured_db, dt, expected_db);
        CHECK(fabs(measured_db - expected_db) < 0.25 * fabs(expected_db) + 1.0, "DECAY %.2f: %.1f dB against %.1f dB", dn, measured_db, expected_db);
    }
}

static void test_envelope_and_level(void)
{
    const float sr = 44100.0f;
    /* attack: a 100 ms attack on a sustained dark note ramps up */
    opst_params p = defaults();
    p.damp = 0.2f;
    p.decay = 1.0f;
    p.env_attack = 0.1f;
    fresh(&g_core, sr, &p);
    opst_note_on(&g_core, 55, 110);
    render_blocks(&g_core, g_l, g_r, (int)sr, 128);
    /* the string loses energy while the envelope rises, so compare the envelope shape to the free string */
    p.env_attack = 0.001f;
    fresh(&g_core2, sr, &p);
    opst_note_on(&g_core2, 55, 110);
    render_blocks(&g_core2, g_l2, g_r2, (int)sr, 128);
    const float slow = rms(g_l + 100, 300), fast = rms(g_l2 + 100, 300);
    printf("  ATTACK: rms in the first 7 ms after a note-on %.4f with 100 ms attack, %.4f with 1 ms\n", slow, fast);
    CHECK(slow < fast * 0.25f, "a long attack should start much quieter");

    /* sustain 0 + short decay: the note fades by the envelope while the string still rings */
    p = defaults();
    p.decay = 1.0f;
    p.env_decay = 0.2f;
    p.env_sustain = 0.0f;
    fresh(&g_core, sr, &p);
    opst_note_on(&g_core, 55, 110);
    render_blocks(&g_core, g_l, g_r, (int)(sr * 2.0f), 128);
    const float late = peak(g_l + (int)(sr * 1.5f), (int)(sr * 0.5f));
    CHECK(late < 1e-3f, "sustain 0 should end the note (%.2e)", late);

    /* level scales linearly below the knee */
    p = defaults();
    p.damp = 0.2f;
    p.level = 0.5f;
    fresh(&g_core, sr, &p);
    opst_note_on(&g_core, 55, 60);
    render_blocks(&g_core, g_l, g_r, 8820, 128);
    p.level = 0.25f;
    fresh(&g_core2, sr, &p);
    opst_note_on(&g_core2, 55, 60);
    render_blocks(&g_core2, g_l2, g_r2, 8820, 128);
    const double ratio = (double)rms(g_l, 8820) / rms(g_l2, 8820);
    printf("  LEVEL: halving the level changes the rms by a factor of %.3f\n", ratio);
    CHECK(fabs(ratio - 2.0) < 0.02, "level should scale the output linearly (%.3f)", ratio);
}

static void test_velocity(void)
{
    const float sr = 44100.0f;
    float rms_lo, rms_hi, dark_lo, dark_hi;
    for (int k = 0; k < 2; k++) {
        opst_params p = defaults();
        fresh(&g_core, sr, &p);
        opst_note_on(&g_core, 60, k == 0 ? 30 : 120);
        render_blocks(&g_core, g_l, g_r, 8820, 128);
        double num = 0, den = 0;
        for (int i = 1; i < 2000; i++) {
            double d = g_l[i] - g_l[i - 1];
            num += d * d;
            den += (double)g_l[i] * g_l[i];
        }
        if (k == 0) {
            rms_lo = rms(g_l, 4000);
            dark_lo = (float)sqrt(num / den);
        } else {
            rms_hi = rms(g_l, 4000);
            dark_hi = (float)sqrt(num / den);
        }
    }
    printf("  velocity: rms %.4f at 30, %.4f at 120; brightness %.3f at 30, %.3f at 120\n", rms_lo, rms_hi, dark_lo, dark_hi);
    CHECK(rms_hi > rms_lo * 3.0f, "harder hits should be much louder");
    CHECK(dark_hi > dark_lo, "harder hits should be brighter");
}

static void test_pitch_bend_and_mod(void)
{
    const float sr = 44100.0f;
    opst_params p = defaults();
    p.decay = 0.9f;
    p.damp = 0.7f;
    fresh(&g_core, sr, &p);
    ev_t ev[] = {{0, 0, 57, 110}, {22050, 2, 2000, 0}};
    const int n = 44100 * 2;
    run_script(&g_core, ev, 2, g_l, g_r, n, 128);
    const double f0 = note_hz(57);
    const double before = measure_f0(g_l, 4410, 11025, f0, sr);
    /* the bend glides over ~50 ms; measure well after it. +2 semitones is +200 cents, so scan around the bent pitch */
    const double bent = measure_f0(g_l, 22050 + 8820, 22050, f0 * pow(2.0, 2.0 / 12.0), sr);
    printf("  bend: %.2f cents before, %.2f cents from the +2 semitone target after\n", cents_between(before, f0),
           cents_between(bent, f0 * pow(2.0, 2.0 / 12.0)));
    CHECK(fabs(cents_between(before, f0)) < 5.0, "unbent pitch off");
    CHECK(fabs(cents_between(bent, f0 * pow(2.0, 2.0 / 12.0))) < 5.0, "bent pitch should be +2 semitones within 5 cents");
    const float steady = max_step(g_l, 16000, 22050);
    const float across = max_step(g_l, 22050, 22050 + 6000);
    CHECK(across < steady * 1.6f + 1e-4f, "bend produced a step: %.5f against %.5f", across, steady);

    /* mod wheel: vibrato moves the pitch around. Measure two short windows half a vibrato cycle apart. */
    fresh(&g_core, sr, &p);
    ev_t ev2[] = {{0, 3, 1000, 0}, {1, 0, 57, 110}};
    run_script(&g_core, ev2, 2, g_l, g_r, n, 128);
    double lo = 1e9, hi = -1e9;
    for (int k = 0; k < 6; k++) {
        const double m = measure_f0(g_l, 8000 + k * 2205, 4410, f0, sr);
        lo = fmin(lo, cents_between(m, f0));
        hi = fmax(hi, cents_between(m, f0));
    }
    printf("  mod wheel: pitch swings between %.1f and %.1f cents\n", lo, hi);
    CHECK(hi - lo > 8.0, "full mod wheel should add audible vibrato (%.1f cents of swing)", hi - lo);
    CHECK(all_finite(g_l, n), "NaN with vibrato");
}

static void test_deterministic_and_block_size_independent(void)
{
    const float sr = 44100.0f;
    opst_params p = defaults();
    p.stiff = 0.4f;
    const int n = 30000;
    ev_t ev[] = {{0, 0, 48, 100},   {0, 0, 55, 90},   {3001, 0, 60, 127}, {7777, 2, 1500, 0}, {9001, 1, 48, 0},
                 {12345, 0, 72, 70}, {15000, 3, 800, 0}, {20000, 4, 1, 0},  {21000, 1, 55, 0},  {26000, 4, 0, 0}};
    fresh(&g_core, sr, &p);
    run_script(&g_core, ev, 10, g_l, g_r, n, 128);
    fresh(&g_core2, sr, &p);
    run_script(&g_core2, ev, 10, g_l2, g_r2, n, 128);
    CHECK(memcmp(g_l, g_l2, sizeof(float) * n) == 0 && memcmp(g_r, g_r2, sizeof(float) * n) == 0, "same events and seed must give identical output");

    const int blks[] = {1, 7, 32, 33, 64, 500, 4096};
    for (int b = 0; b < 7; b++) {
        fresh(&g_core2, sr, &p);
        run_script(&g_core2, ev, 10, g_l2, g_r2, n, blks[b]);
        CHECK(memcmp(g_l, g_l2, sizeof(float) * n) == 0 && memcmp(g_r, g_r2, sizeof(float) * n) == 0,
              "block size %d changed the output", blks[b]);
    }
    printf("  identical bits across blocks of 1, 7, 32, 33, 64, 128, 500 and 4096 frames\n");

    opst_set_seed(&g_core2, 99);
    opst_reset(&g_core2);
    opst_set_params(&g_core2, &p);
    run_script(&g_core2, ev, 10, g_l2, g_r2, n, 128);
    CHECK(memcmp(g_l, g_l2, sizeof(float) * n) != 0, "a different seed should give a different pluck");
}

static void test_sample_rates_and_block_size_one(void)
{
    const float rates[] = {22050.0f, 32000.0f, 44100.0f, 48000.0f, 88200.0f, 96000.0f, 192000.0f};
    opst_params p = defaults();
    for (int ri = 0; ri < 7; ri++) {
        fresh(&g_core, rates[ri], &p);
        opst_note_on(&g_core, 45, 100);
        opst_note_on(&g_core, 62, 100);
        opst_note_on(&g_core, 81, 100);
        const int n = (int)(rates[ri] * 0.5f);
        render_blocks(&g_core, g_l, g_r, n, ri % 2 ? 1 : 64);
        CHECK(all_finite(g_l, n) && all_finite(g_r, n), "non-finite output at %.0f Hz", rates[ri]);
        CHECK(peak(g_l, n) < 1.0f && peak(g_l, n) > 0.01f, "level at %.0f Hz: %.4f", rates[ri], peak(g_l, n));
    }
    printf("  sample rates 22050 to 192000, block size 1 and 64: finite, bounded, audible\n");
}

static unsigned rs = 777u;
static float urand(void)
{
    rs = rs * 1664525u + 1013904223u;
    return (float)(rs >> 8) / (float)(1u << 24);
}

static void test_stress_random_events_and_params(void)
{
    const float rates[] = {44100.0f, 96000.0f};
    for (int ri = 0; ri < 2; ri++) {
        const float sr = rates[ri];
        opst_params p = defaults();
        fresh(&g_core, sr, &p);
        const int n = (int)(sr * 6.0f);
        float pk = 0;
        int ok = 1;
        for (int i = 0; i < n; i += 64) {
            p.pick = urand();
            p.damp = urand();
            p.stiff = urand();
            p.decay = urand();
            p.env_attack = urand() * urand() * 2.0f;
            p.env_decay = urand() * 2.0f;
            p.env_sustain = urand();
            p.env_release = urand() * 2.0f;
            p.level = urand() * 2.0f;
            opst_set_params(&g_core, &p);
            const float r = urand();
            if (r < 0.25f)
                opst_note_on(&g_core, (int)(urand() * 127.0f), 1 + (int)(urand() * 126.0f));
            else if (r < 0.40f)
                opst_note_off(&g_core, (int)(urand() * 127.0f));
            else if (r < 0.45f)
                opst_pitch_bend(&g_core, (urand() - 0.5f) * 8.0f);
            else if (r < 0.50f)
                opst_mod_wheel(&g_core, urand());
            else if (r < 0.52f)
                opst_sustain(&g_core, urand() > 0.5f);
            else if (r < 0.53f)
                opst_all_off(&g_core);
            opst_render(&g_core, g_l, g_r, 64);
            if (!all_finite(g_l, 64) || !all_finite(g_r, 64))
                ok = 0;
            pk = fmaxf(pk, fmaxf(peak(g_l, 64), peak(g_r, 64)));
        }
        CHECK(ok, "non-finite sample during random stress at %.0f Hz", sr);
        CHECK(pk < 1.0f, "peak %.4f during random stress at %.0f Hz", pk, sr);
        printf("  stress (random notes, bends, pedal and every parameter each 64 frames, 6 s at %.0f Hz): peak %.4f\n", sr, pk);
    }
}

static void test_parameter_sweep_no_clicks(void)
{
    const float sr = 44100.0f;
    opst_params p = defaults();
    p.damp = 0.3f;
    p.decay = 1.0f;
    fresh(&g_core, sr, &p);
    opst_note_on(&g_core, 43, 120);
    const int n = 44100 * 2;
    render_blocks(&g_core, g_l, g_r, 22050, 128);
    const float steady = max_step(g_l, 20000, 22050);
    for (int i = 22050; i < n; i += 128) {
        const int k = (i - 22050) / 128;
        p.damp = (float)(k % 9) / 8.0f;
        p.stiff = (float)(k % 5) / 4.0f;
        p.decay = 0.5f + 0.5f * (float)(k % 3) / 2.0f;
        opst_set_params(&g_core, &p);
        opst_render(&g_core, g_l + i, g_r + i, n - i < 128 ? n - i : 128);
    }
    const float swept = max_step(g_l, 22050, n);
    printf("  sweep: largest step %.5f while DAMP, STIFF and DECAY change every block, %.5f before\n", swept, steady);
    CHECK(swept < steady * 2.0f + 0.01f, "parameter changes caused a step of %.5f", swept);
    CHECK(all_finite(g_l, n), "NaN during sweep");
}

static void test_dc_and_meters(void)
{
    const float sr = 44100.0f;
    opst_params p = defaults();
    p.damp = 0.8f;
    fresh(&g_core, sr, &p);
    opst_note_on(&g_core, 50, 120);
    render_blocks(&g_core, g_l, g_r, 22050, 128);
    double mean = 0;
    for (int i = 4000; i < 22050; i++)
        mean += g_l[i];
    mean /= (22050 - 4000);
    printf("  DC: mean of the output %.2e\n", mean);
    CHECK(fabs(mean) < 1e-3, "the output should carry no DC (%.2e)", mean);

    const float lvl = opst_take_level(&g_core);
    CHECK(lvl > 0.1f, "level meter should show the note (%.3f)", lvl);
    CHECK(opst_take_level(&g_core) == 0.0f, "taking the level clears it");
    int note;
    float vl;
    int on = opst_voice_info(&g_core, 0, &note, &vl);
    CHECK(on == 1 && note == 50 && vl > 0.01f, "voice info should report the note (%d %d %.3f)", on, note, vl);
    on = opst_voice_info(&g_core, 5, &note, &vl);
    CHECK(on == 0, "unused voice should report idle");
}

int main(void)
{
    printf("String core tests\n");
    test_pitch_accuracy();
    test_pitch_with_other_settings();
    test_polyphony_and_headroom();
    test_note_off_releases_to_silence();
    test_string_dies_by_itself();
    test_sustain_pedal_and_all_off();
    test_steal_and_retrigger_without_clicks();
    test_knobs_change_the_sound();
    test_envelope_and_level();
    test_velocity();
    test_pitch_bend_and_mod();
    test_deterministic_and_block_size_independent();
    test_sample_rates_and_block_size_one();
    test_stress_random_events_and_params();
    test_parameter_sweep_no_clicks();
    test_dc_and_meters();
    CHECK(fabsf(opst_decay_seconds(0.0f) - 0.08f) < 1e-4f && fabsf(opst_decay_seconds(1.0f) - 24.0f) < 1e-2f, "decay range");
    if (failures) {
        printf("%d FAILED\n", failures);
        return 1;
    }
    printf("PASSED\n");
    return 0;
}
