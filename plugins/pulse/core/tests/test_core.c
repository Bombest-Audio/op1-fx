/* Behavioral tests for the Pulse core. Build and run with core/tests/run.sh */
#include "../pulse_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PI 3.14159265358979323846

static int failures = 0;
static int checks = 0;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        checks++;                                         \
        if (!(cond)) {                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
            failures++;                                   \
        }                                                 \
    } while (0)

/* ---------- harness ---------- */

enum { E_ON, E_OFF, E_BEND, E_MOD, E_PEDAL, E_ALLOFF, E_KILL, E_PARAMS };
typedef struct {
    int at; /* sample index */
    int type;
    int a, b;
    float f;
    pulse_params prm;
} ev_t;

static pulse_params default_params(void)
{
    pulse_params p;
    p.shape = PULSE_DEFAULT_SHAPE;
    p.sweep = PULSE_DEFAULT_SWEEP;
    p.rate = PULSE_DEFAULT_RATE;
    p.detune = PULSE_DEFAULT_DETUNE;
    p.attack = PULSE_DEFAULT_ATTACK;
    p.decay = PULSE_DEFAULT_DECAY;
    p.sustain = PULSE_DEFAULT_SUSTAIN;
    p.release = PULSE_DEFAULT_RELEASE;
    p.level = PULSE_DEFAULT_LEVEL;
    p.bend_range = PULSE_DEFAULT_BEND_RANGE;
    return p;
}

static ev_t ev_on(int at, int note, int vel) { ev_t e; memset(&e, 0, sizeof e); e.at = at; e.type = E_ON; e.a = note; e.b = vel; return e; }
static ev_t ev_off(int at, int note) { ev_t e; memset(&e, 0, sizeof e); e.at = at; e.type = E_OFF; e.a = note; return e; }
static ev_t ev_f(int at, int type, float f) { ev_t e; memset(&e, 0, sizeof e); e.at = at; e.type = type; e.f = f; return e; }
static ev_t ev_i(int at, int type, int a) { ev_t e; memset(&e, 0, sizeof e); e.at = at; e.type = type; e.a = a; return e; }
static ev_t ev_params(int at, const pulse_params *p) { ev_t e; memset(&e, 0, sizeof e); e.at = at; e.type = E_PARAMS; e.prm = *p; return e; }

static void apply(pulse_t *p, const ev_t *e)
{
    switch (e->type) {
    case E_ON: pulse_note_on(p, e->a, e->b); break;
    case E_OFF: pulse_note_off(p, e->a); break;
    case E_BEND: pulse_pitch_bend(p, e->f); break;
    case E_MOD: pulse_mod_wheel(p, e->f); break;
    case E_PEDAL: pulse_sustain_pedal(p, e->a); break;
    case E_ALLOFF: pulse_all_off(p); break;
    case E_KILL: pulse_all_sound_off(p); break;
    case E_PARAMS: pulse_set_params(p, &e->prm); break;
    }
}

/* Render `total` frames. Events apply at their sample; the host block size only decides where extra render calls start.
 * Events must be sorted by `at`. */
static void run_events(pulse_t *p, const ev_t *evs, int nev, float *L, float *R, int total, int block)
{
    int pos = 0, ei = 0;
    while (pos < total) {
        while (ei < nev && evs[ei].at <= pos)
            apply(p, &evs[ei++]);
        int next = (pos / block + 1) * block;
        if (next > total)
            next = total;
        if (ei < nev && evs[ei].at < next)
            next = evs[ei].at;
        pulse_render(p, L + pos, R + pos, next - pos);
        pos = next;
    }
}

/* Fresh core at sr with params, then run. */
static void run_fresh(pulse_t *p, float sr, const pulse_params *prm, const ev_t *evs, int nev, float *L, float *R, int total, int block)
{
    pulse_init(p, sr);
    pulse_set_params(p, prm);
    pulse_reset(p);
    run_events(p, evs, nev, L, R, total, block);
}

static float *buf(int n)
{
    float *b = (float *)calloc((size_t)n, sizeof(float));
    if (!b) {
        printf("out of memory\n");
        exit(2);
    }
    return b;
}

static float peak_of(const float *x, int n)
{
    float m = 0;
    for (int i = 0; i < n; i++)
        m = fmaxf(m, fabsf(x[i]));
    return m;
}

static double rms_of(const float *x, int n)
{
    double s = 0;
    for (int i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (n > 0 ? n : 1));
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

/* Hann-windowed Goertzel magnitude of x[0..n) at f Hz. */
static double gmag(const float *x, int n, double sr, double f)
{
    const double w = 2.0 * PI * f / sr, coeff = 2.0 * cos(w);
    double s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) {
        const double h = 0.5 - 0.5 * cos(2.0 * PI * i / (n - 1));
        const double s0 = x[i] * h + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2);
}

/* Find the spectral peak within +-range cents of f0 (1 cent grid, parabolic refinement). Returns cents from f0. */
static double peak_cents(const float *x, int n, double sr, double f0, int range)
{
    int nb = 2 * range + 1;
    double *m = (double *)malloc(sizeof(double) * (size_t)nb);
    int best = 0;
    for (int i = 0; i < nb; i++) {
        m[i] = gmag(x, n, sr, f0 * pow(2.0, (i - range) / 1200.0));
        if (m[i] > m[best])
            best = i;
    }
    double off = 0;
    if (best > 0 && best < nb - 1) {
        const double a = log(m[best - 1] + 1e-30), b = log(m[best] + 1e-30), c = log(m[best + 1] + 1e-30);
        const double den = a - 2 * b + c;
        if (den != 0)
            off = 0.5 * (a - c) / den;
    }
    free(m);
    return (best - range) + off;
}

static double note_hz(int note) { return 440.0 * pow(2.0, (note - 69) / 12.0); }

/* ---------- tests ---------- */

static void test_pitch_accuracy(void)
{
    const float rates[] = {22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f};
    const int notes_lo[] = {36, 60, 84};
    float worst = 0;
    for (int ri = 0; ri < 5; ri++) {
        const float sr = rates[ri];
        const int nn = ri == 4 ? 1 : 3;
        for (int ni = 0; ni < nn; ni++) {
            const int note = nn == 1 ? 60 : notes_lo[ni];
            for (int use_detune = 0; use_detune < 2; use_detune++) {
                pulse_params prm = default_params();
                prm.sustain = 1.0f;
                prm.decay = 0.0f;
                prm.sweep = 0.0f; /* the PWM LFO amplitude-modulates the fundamental and skews a short-window peak; pitch is measured without it */
                if (!use_detune)
                    prm.detune = 0.0f;
                const int total = (int)(1.7f * sr);
                float *L = buf(total), *R = buf(total), *M = buf(total);
                pulse_t p;
                const ev_t evs[] = {ev_on(0, note, 100)};
                run_fresh(&p, sr, &prm, evs, 1, L, R, total, 128);
                const int skip = (int)(0.5f * sr), n = total - skip;
                /* With detune the two oscillators sit about 9 cents apart, too close for one 1.2 s window to separate, so the
                 * left and right peaks (each led by one oscillator) are measured and their mean is the played pitch. */
                double c;
                if (use_detune) {
                    const double cl = peak_cents(L + skip, n, sr, note_hz(note), 40), cr = peak_cents(R + skip, n, sr, note_hz(note), 40);
                    c = 0.5 * (cl + cr);
                } else {
                    for (int i = 0; i < n; i++)
                        M[i] = L[skip + i] + R[skip + i];
                    c = peak_cents(M, n, sr, note_hz(note), 40);
                }
                worst = fmaxf(worst, (float)fabs(c));
                CHECK(fabs(c) < 5.0, "pitch note %d sr %.0f detune %d: off by %.2f cents", note, (double)sr, use_detune, c);
                free(L); free(R); free(M);
            }
        }
    }
    printf("pitch accuracy: worst error %.2f cents over note 36/60/84 at 22.05k to 192k, detune off, and default detune (mean of left and right), sweep 0 (limit 5)\n", (double)worst);

    /* pitch bend: full wheel is bend_range semitones */
    for (int dir = -1; dir <= 1; dir += 2) {
        pulse_params prm = default_params();
        prm.sustain = 1.0f;
        prm.detune = 0.0f;
        const float sr = 44100.0f;
        const int total = (int)(2.2f * sr);
        float *L = buf(total), *R = buf(total), *M = buf(total);
        pulse_t p;
        const ev_t evs[] = {ev_on(0, 60, 100), ev_f(100, E_BEND, (float)dir)};
        run_fresh(&p, sr, &prm, evs, 2, L, R, total, 128);
        const int skip = (int)(1.0f * sr), n = total - skip;
        for (int i = 0; i < n; i++)
            M[i] = L[skip + i] + R[skip + i];
        const double c = peak_cents(M, n, sr, note_hz(60), 260);
        CHECK(fabs(c - 200.0 * dir) < 5.0, "bend %d: expected %d cents, got %.2f", dir, 200 * dir, c);
        printf("pitch bend %+d: %.2f cents (expected %+d)\n", dir, c, 200 * dir);
        free(L); free(R); free(M);
    }
}

static void test_polyphony_and_headroom(void)
{
    const float sr = 44100.0f;
    const int total = (int)(1.5f * sr);
    float *L = buf(total), *R = buf(total);
    pulse_t p;
    pulse_params prm = default_params();
    prm.level = 1.0f;
    ev_t evs[8];
    for (int i = 0; i < 8; i++)
        evs[i] = ev_on(i * 37, 48 + 3 * i, 127);
    run_fresh(&p, sr, &prm, evs, 8, L, R, total, 128);
    CHECK(all_finite(L, total) && all_finite(R, total), "8-note chord produced NaN or Inf");
    const float pk = fmaxf(peak_of(L, total), peak_of(R, total));
    CHECK(pk > 0.1f, "8-note chord too quiet: %.3f", (double)pk);
    CHECK(pk < 1.0f, "8-note chord peak %.4f", (double)pk);
    CHECK(pulse_active_voices(&p) == 8, "expected 8 active voices, got %d", pulse_active_voices(&p));
    printf("polyphony: 8 notes at velocity 127, level 1.0: peak %.3f, %d voices\n", (double)pk, pulse_active_voices(&p));
    {
        pulse_params d = default_params();
        run_fresh(&p, sr, &d, evs, 8, L, R, total, 128);
        const float pd = fmaxf(peak_of(L, total), peak_of(R, total));
        CHECK(pd < 0.9f, "8 notes at default level peak %.3f", (double)pd);
        printf("            same chord at the default level 0.8: peak %.3f\n", (double)pd);
    }

    /* worst-case headroom: every knob corner, 8 close notes at full velocity, bend and mod wheel up */
    float worst = 0;
    for (int shape = 0; shape < 3; shape++)
        for (int sweep = 0; sweep < 2; sweep++)
            for (int det = -1; det <= 1; det++)
                for (int combo = 0; combo < 2; combo++) {
                    pulse_params q = default_params();
                    q.shape = 0.5f * shape;
                    q.sweep = (float)sweep;
                    q.detune = (float)det;
                    q.level = 1.0f;
                    q.sustain = 1.0f;
                    q.attack = 0.0f;
                    q.rate = 0.4f;
                    ev_t e[10];
                    int ne = 0;
                    for (int i = 0; i < 8; i++)
                        e[ne++] = ev_on(0, combo ? 60 + i : 36 + 12 * (i % 5) + i, 127);
                    e[ne++] = ev_f(1000, E_MOD, (float)combo);
                    e[ne++] = ev_f(2000, E_BEND, combo ? 1.0f : -1.0f);
                    const int n = (int)(0.6f * sr);
                    float *a = buf(n), *b = buf(n);
                    run_fresh(&p, sr, &q, e, ne, a, b, n, 128);
                    CHECK(all_finite(a, n) && all_finite(b, n), "headroom corner NaN");
                    worst = fmaxf(worst, fmaxf(peak_of(a, n), peak_of(b, n)));
                    free(a); free(b);
                }
    CHECK(worst < 1.0f, "headroom corners: peak %.4f", (double)worst);
    CHECK(worst <= 0.9801f, "headroom corners: peak %.4f is over the 0.98 ceiling", (double)worst);
    printf("headroom: worst peak over 36 corner cases, 8 voices, velocity 127, level 1.0: %.4f (ceiling 0.98, limit 1.0)\n", (double)worst);
    free(L); free(R);
}

static void test_release_to_silence(void)
{
    const float sr = 44100.0f;
    for (int ri = 0; ri < 2; ri++) {
        pulse_params prm = default_params();
        prm.release = ri ? 0.55f : 0.3f;
        prm.sustain = 1.0f;
        prm.sweep = 0.0f;
        prm.detune = 0.0f;
        const float t60 = pulse_release_s(prm.release);
        const int off_at = (int)(0.6f * sr);
        const int total = off_at + (int)((2.5f * t60 + 0.1f) * sr);
        float *L = buf(total), *R = buf(total);
        pulse_t p;
        const ev_t evs[] = {ev_on(0, 57, 110), ev_off(off_at, 57)};
        run_fresh(&p, sr, &prm, evs, 2, L, R, total, 128);
        const float sus_peak = peak_of(L + off_at - 2000, 2000);
        /* peak over a window starting at half the release time: expect about -30 dB (a factor 0.0316) */
        const int w0 = off_at + (int)(0.5f * t60 * sr), wlen = (int)(0.02f * sr);
        const float half_peak = peak_of(L + w0, wlen);
        const double db = 20.0 * log10((double)half_peak / sus_peak);
        CHECK(db > -38.0 && db < -22.0, "release %.3fs: level at t/2 is %.1f dB, expected about -30", (double)t60, db);
        const int w1 = off_at + (int)(1.6f * t60 * sr);
        const float late = peak_of(L + w1, total - w1);
        CHECK(late < 2e-3f, "release %.3fs: still %.5f at 1.6x the release time", (double)t60, (double)late);
        CHECK(pulse_active_voices(&p) == 0, "voice still active after release");
        CHECK(peak_of(L + total - 100, 100) == 0.0f && peak_of(R + total - 100, 100) == 0.0f, "not digital silence at the end");
        printf("release %.3fs: %.1f dB at t/2 (expect -30), %.1e at 1.6t, silent at end\n", (double)t60, db, (double)late);
        free(L); free(R);
    }

    /* velocity 0 acts as note off */
    pulse_params prm = default_params();
    prm.release = 0.1f;
    const int total = (int)(1.0f * sr);
    float *L = buf(total), *R = buf(total);
    pulse_t p;
    const ev_t evs[] = {ev_on(0, 60, 100), ev_on(20000, 60, 0)};
    run_fresh(&p, sr, &prm, evs, 2, L, R, total, 128);
    CHECK(peak_of(L + total - 500, 500) == 0.0f, "note on with velocity 0 did not release");
    free(L); free(R);
}

static void test_sustain_pedal_and_all_off(void)
{
    const float sr = 44100.0f;
    pulse_params prm = default_params();
    prm.release = 0.2f;
    prm.sustain = 0.9f;
    const int total = (int)(3.0f * sr);
    float *L = buf(total), *R = buf(total);
    pulse_t p;
    const ev_t evs[] = {ev_i(0, E_PEDAL, 1), ev_on(100, 60, 100), ev_off((int)(0.3f * sr), 60), ev_i((int)(2.0f * sr), E_PEDAL, 0)};
    run_fresh(&p, sr, &prm, evs, 4, L, R, total, 128);
    const float held = peak_of(L + (int)(1.5f * sr), 4000);
    const float after = peak_of(L + total - 2000, 2000);
    CHECK(held > 0.05f, "pedal did not hold the note: %.4f", (double)held);
    CHECK(after == 0.0f, "pedal release did not end the note: %.5f", (double)after);
    printf("sustain pedal: held at %.3f while down, silent after release\n", (double)held);

    /* all notes off releases; all sound off fades within a few ms */
    for (int mode = 0; mode < 2; mode++) {
        prm = default_params();
        prm.release = 0.2f;
        prm.sustain = 1.0f;
        prm.detune = 0.0f;
        const int n = (int)(1.5f * sr);
        float *a = buf(n), *b = buf(n);
        ev_t e[5];
        for (int i = 0; i < 4; i++)
            e[i] = ev_on(0, 48 + 5 * i, 100);
        e[4] = ev_i((int)(0.5f * sr), mode ? E_KILL : E_ALLOFF, 0);
        run_fresh(&p, sr, &prm, e, 5, a, b, n, 128);
        const int at = (int)(0.5f * sr);
        const float before = peak_of(a + at - 1000, 1000);
        const int quiet_from = at + (int)(0.006f * sr);
        const float soon = peak_of(a + quiet_from, 500);
        if (mode)
            CHECK(soon == 0.0f && before > 0.05f, "all sound off: %.5f left after 6 ms", (double)soon);
        else
            CHECK(soon > 0.0f && peak_of(a + n - 200, 200) == 0.0f, "all notes off should release, not cut");
        CHECK(pulse_active_voices(&p) == 0, "voices left after %s", mode ? "all sound off" : "all notes off");
        free(a); free(b);
    }
    free(L); free(R);
}

/* Steal and retrigger: no hard cut. */
static void test_steal_and_retrigger(void)
{
    const float sr = 44100.0f;
    pulse_params prm = default_params();
    prm.sustain = 1.0f;
    prm.attack = 0.15f;

    /* Reference: the largest single-sample step in steady-state 8-note chords (BLEP'd pulse edges set the scale). */
    float ref = 0;
    {
        const int n = (int)(1.0f * sr);
        float *a = buf(n), *b = buf(n);
        pulse_t p;
        for (int variant = 0; variant < 3; variant++) {
            ev_t e[8];
            for (int i = 0; i < 8; i++)
                e[i] = ev_on(0, 48 + 4 * i + variant, 110);
            run_fresh(&p, sr, &prm, e, 8, a, b, n, 128);
            ref = fmaxf(ref, fmaxf(max_step(a, n / 2, n), max_step(b, n / 2, n)));
        }
        free(a); free(b);
    }
    /* Single voice reference too. */
    float ref1 = 0;
    {
        const int n = (int)(0.8f * sr);
        float *a = buf(n), *b = buf(n);
        pulse_t p;
        const ev_t e[] = {ev_on(0, 60, 110)};
        run_fresh(&p, sr, &prm, e, 1, a, b, n, 128);
        ref1 = fmaxf(max_step(a, n / 2, n), max_step(b, n / 2, n));
        free(a); free(b);
    }

    float worst_retrig = 0, worst_steal = 0, worst_kill = 0;
    for (int trial = 0; trial < 48; trial++) {
        const int at = 20000 + trial * 37 + (trial % 7) * 11; /* sweeps the strike point across the waveform cycle */
        const int total = at + 4000;
        float *a = buf(total), *b = buf(total);
        pulse_t p;

        /* retrigger the same key on a single sounding voice */
        {
            const ev_t e[] = {ev_on(0, 60, 110), ev_on(at, 60, 110)};
            run_fresh(&p, sr, &prm, e, 2, a, b, total, 128);
            worst_retrig = fmaxf(worst_retrig, fmaxf(max_step(a, at - 4, total), max_step(b, at - 4, total)) / ref1);
        }
        /* steal: nine and ten notes into eight voices */
        {
            ev_t e[10];
            for (int i = 0; i < 8; i++)
                e[i] = ev_on(i * 5, 48 + 4 * i, 110);
            e[8] = ev_on(at, 90, 110);
            e[9] = ev_on(at + 700, 91, 110);
            run_fresh(&p, sr, &prm, e, 10, a, b, total, 128);
            worst_steal = fmaxf(worst_steal, fmaxf(max_step(a, at - 4, total), max_step(b, at - 4, total)) / ref);
        }
        /* all sound off */
        {
            ev_t e[5];
            for (int i = 0; i < 4; i++)
                e[i] = ev_on(0, 48 + 4 * i, 110);
            e[4] = ev_i(at, E_KILL, 0);
            run_fresh(&p, sr, &prm, e, 5, a, b, total, 128);
            float ref4 = 0;
            ref4 = fmaxf(max_step(a, at - 2000, at - 4), max_step(b, at - 2000, at - 4));
            worst_kill = fmaxf(worst_kill, fmaxf(max_step(a, at - 4, total), max_step(b, at - 4, total)) / ref4);
        }
        free(a); free(b);
    }
    /* The limit: a step no larger than the biggest step the same material makes in steady state, plus 10 percent. A hard cut or a
     * hard start of a sounding voice jumps by a whole plateau, which on this material is well above the 2-sample BLEP edge step. */
    CHECK(worst_retrig <= 1.10f, "retrigger step %.3fx the steady-state step", (double)worst_retrig);
    CHECK(worst_steal <= 1.10f, "steal step %.3fx the steady-state step", (double)worst_steal);
    CHECK(worst_kill <= 1.10f, "all sound off step %.3fx the steady-state step", (double)worst_kill);
    printf("click check over 48 strike points: retrigger %.3fx, steal %.3fx, all-sound-off %.3fx of the steady-state max step (limit 1.10x)\n",
           (double)worst_retrig, (double)worst_steal, (double)worst_kill);

    /* The fade is a ramp, not an instant cut. Until the fade ends the oscillator and envelope run exactly as they would
     * have without the retrigger, so the ratio to that reference is the fade gain: 1 falling linearly to 0 in 2.5 ms. */
    {
        prm.detune = 0.0f;
        prm.sweep = 0.0f;
        const int at = 22050;
        const int total = at + 9000;
        float *a = buf(total), *b = buf(total), *c = buf(total), *d = buf(total);
        pulse_t p;
        const ev_t e[] = {ev_on(0, 60, 110), ev_on(at, 60, 110)};
        const ev_t e0[] = {ev_on(0, 60, 110)};
        run_fresh(&p, sr, &prm, e, 2, a, b, total, 128);
        run_fresh(&p, sr, &prm, e0, 1, c, d, total, 128);
        const int fade_n = (int)(0.0025f * sr + 0.5f);
        double worst_dev = 0;
        int compared = 0;
        for (int k = 0; k < fade_n - 1; k++) {
            if (fabsf(c[at + k]) < 0.03f)
                continue;
            const double ratio = (double)a[at + k] / (double)c[at + k];
            const double want = 1.0 - (double)k / (double)fade_n;
            worst_dev = fmax(worst_dev, fabs(ratio - want));
            compared++;
        }
        CHECK(compared > 30, "fade check compared only %d samples", compared);
        CHECK(worst_dev < 0.02, "retrigger fade is not the linear 2.5 ms ramp: worst deviation %.4f", worst_dev);
        const float before = peak_of(c + at - 800, 700);
        const float right_after = peak_of(a + at + fade_n + 1, 3);
        CHECK(right_after < 0.06f * before, "level %.4f right after the fade (vs %.4f before)", (double)right_after, (double)before);
        const float again = peak_of(a + at + (int)(0.1f * sr), 800);
        CHECK(again > 0.5f * before, "retriggered note did not come back");
        printf("retrigger fade: follows the linear 2.5 ms ramp within %.4f over %d samples; %.1f%% of the old level when the new note starts; back to %.0f%% by 100 ms\n",
               worst_dev, compared, 100.0 * right_after / before, 100.0 * again / before);
        free(a); free(b); free(c); free(d);
    }
}

static double bandrms_h(const float *x, int n, double sr, double f, int h)
{
    return gmag(x, n, sr, f * h);
}

/* Each of the nine controls changes something you can measure. */
static void test_controls(void)
{
    const float sr = 44100.0f;
    const int note = 57; /* 220 Hz */
    const double f0 = note_hz(note);
    const int total = (int)(2.0f * sr);
    float *L = buf(total), *R = buf(total);
    pulse_t p;

    /* SHAPE: 0 is a square wave (no even harmonics), a thin pulse has them */
    {
        double r_sq = 0, r_mid = 0, r_thin = 0;
        const float shapes[] = {0.0f, 0.5f, 1.0f};
        for (int i = 0; i < 3; i++) {
            pulse_params prm = default_params();
            prm.shape = shapes[i];
            prm.sweep = 0.0f;
            prm.detune = 0.0f;
            prm.sustain = 1.0f;
            const ev_t e[] = {ev_on(0, note, 110)};
            run_fresh(&p, sr, &prm, e, 1, L, R, total, 128);
            const int skip = (int)(0.5f * sr), n = total - skip;
            const double h1 = bandrms_h(L + skip, n, sr, f0, 1), h2 = bandrms_h(L + skip, n, sr, f0, 2);
            const double r = h2 / h1;
            if (i == 0) r_sq = r; else if (i == 1) r_mid = r; else r_thin = r;
        }
        CHECK(r_sq < 0.02, "SHAPE 0 should be a square wave: H2/H1 = %.4f", r_sq);
        CHECK(r_mid > 0.3, "SHAPE 0.5: H2/H1 = %.4f", r_mid);
        CHECK(r_thin > 0.5, "SHAPE 1: H2/H1 = %.4f", r_thin);
        printf("SHAPE: H2/H1 = %.4f (0), %.3f (0.5), %.3f (1); duty %.1f%% / %.1f%% / %.1f%%\n", r_sq, r_mid, r_thin,
               (double)pulse_duty_pct(0), (double)pulse_duty_pct(0.5f), (double)pulse_duty_pct(1));
    }

    /* SWEEP and RATE: the second harmonic breathes with the LFO */
    {
        double cv[2];
        int cross[2];
        const float sweeps[] = {0.0f, 1.0f};
        const float rates[] = {0.3f, 0.6f};
        const int long_total = (int)(8.0f * sr);
        float *a = buf(long_total), *b = buf(long_total);
        for (int si = 0; si < 2; si++) {
            for (int ri = 0; ri < 2; ri++) {
                if (si == 0 && ri == 1)
                    continue;
                pulse_params prm = default_params();
                prm.sweep = sweeps[si];
                prm.rate = rates[ri];
                prm.detune = 0.0f;
                prm.sustain = 1.0f;
                const ev_t e[] = {ev_on(0, note, 110)};
                run_fresh(&p, sr, &prm, e, 1, a, b, long_total, 128);
                const int win = (int)(0.02f * sr);
                const int nw = (long_total - (int)(0.5f * sr)) / win;
                double *env = (double *)malloc(sizeof(double) * (size_t)nw);
                double mean = 0;
                for (int w = 0; w < nw; w++) {
                    env[w] = bandrms_h(a + (int)(0.5f * sr) + w * win, win, sr, f0, 2);
                    mean += env[w];
                }
                mean /= nw;
                double var = 0;
                int crossings = 0;
                for (int w = 0; w < nw; w++) {
                    var += (env[w] - mean) * (env[w] - mean);
                    if (w && ((env[w] - mean) * (env[w - 1] - mean) < 0))
                        crossings++;
                }
                const double c = sqrt(var / nw) / mean;
                if (ri == 0) {
                    cv[si] = c;
                    cross[0] = crossings;
                } else {
                    cross[1] = crossings;
                }
                free(env);
            }
        }
        CHECK(cv[0] < 0.05, "SWEEP 0: second harmonic should be steady, CV %.3f", cv[0]);
        CHECK(cv[1] > 0.25, "SWEEP 1: second harmonic should move, CV %.3f", cv[1]);
        const double ratio = (double)cross[1] / (double)(cross[0] > 0 ? cross[0] : 1);
        const double expect = pow(2.0, 7.0 * (0.6 - 0.3));
        CHECK(ratio > 0.6 * expect && ratio < 1.5 * expect, "RATE 0.3 to 0.6: LFO crossings %d to %d (ratio %.2f, expected about %.2f)", cross[0],
              cross[1], ratio, expect);
        printf("SWEEP: harmonic-2 variation %.3f at 0, %.3f at 1. RATE: crossings in 7.5 s %d (%.2f Hz) vs %d (%.2f Hz), ratio %.2f (expected %.2f)\n",
               cv[0], cv[1], cross[0], (double)pulse_rate_hz(0.3f), cross[1], (double)pulse_rate_hz(0.6f), ratio, expect);
        free(a); free(b);
    }

    /* MOD WHEEL pushes the sweep up */
    {
        double cvm[2];
        const int long_total = (int)(4.0f * sr);
        float *a = buf(long_total), *b = buf(long_total);
        for (int m = 0; m < 2; m++) {
            pulse_params prm = default_params();
            prm.sweep = 0.0f;
            prm.rate = 0.5f;
            prm.detune = 0.0f;
            prm.sustain = 1.0f;
            const ev_t e[] = {ev_f(0, E_MOD, (float)m), ev_on(0, note, 110)};
            run_fresh(&p, sr, &prm, e, 2, a, b, long_total, 128);
            const int win = (int)(0.02f * sr), nw = (long_total - (int)(0.5f * sr)) / win;
            double mean = 0, var = 0;
            double *env = (double *)malloc(sizeof(double) * (size_t)nw);
            for (int w = 0; w < nw; w++) {
                env[w] = bandrms_h(a + (int)(0.5f * sr) + w * win, win, sr, f0, 2);
                mean += env[w];
            }
            mean /= nw;
            for (int w = 0; w < nw; w++)
                var += (env[w] - mean) * (env[w] - mean);
            cvm[m] = sqrt(var / nw) / mean;
            free(env);
        }
        CHECK(cvm[0] < 0.05 && cvm[1] > 0.25, "mod wheel: harmonic-2 variation %.3f down, %.3f up", cvm[0], cvm[1]);
        printf("MOD WHEEL: harmonic-2 variation %.3f down, %.3f up (sweep knob at 0)\n", cvm[0], cvm[1]);
        free(a); free(b);
    }

    /* DETUNE: 0 is identical in both channels; +-1 splits them by 100 cents and the sign picks the side */
    {
        pulse_params prm = default_params();
        prm.sweep = 0.0f;
        prm.detune = 0.0f;
        prm.sustain = 1.0f;
        const ev_t e[] = {ev_on(0, note, 110)};
        run_fresh(&p, sr, &prm, e, 1, L, R, total, 128);
        float dmax = 0;
        for (int i = 0; i < total; i++)
            dmax = fmaxf(dmax, fabsf(L[i] - R[i]));
        CHECK(dmax == 0.0f, "DETUNE 0 with SWEEP 0: left and right differ by %.8f", (double)dmax);
        double cl[2], cr[2];
        for (int s = 0; s < 2; s++) {
            prm.detune = s ? -1.0f : 1.0f;
            run_fresh(&p, sr, &prm, e, 1, L, R, total, 128);
            const int skip = (int)(0.5f * sr), n = total - skip;
            cl[s] = peak_cents(L + skip, n, sr, f0, 80);
            cr[s] = peak_cents(R + skip, n, sr, f0, 80);
        }
        CHECK(fabs(cl[0] + 50.0) < 6.0 && fabs(cr[0] - 50.0) < 6.0, "DETUNE +1: left %.1f right %.1f cents (expected -50, +50)", cl[0], cr[0]);
        CHECK(fabs(cl[1] - 50.0) < 6.0 && fabs(cr[1] + 50.0) < 6.0, "DETUNE -1: left %.1f right %.1f cents (expected +50, -50)", cl[1], cr[1]);
        printf("DETUNE: 0 gives identical channels; +1 gives left %.1f / right %.1f cents; -1 gives left %.1f / right %.1f\n", cl[0], cr[0], cl[1], cr[1]);
    }

    /* ATTACK: time to 90 percent of the sustained level */
    {
        const float attacks[] = {0.2f, 0.5f, 0.8f};
        double prev = 0;
        for (int i = 0; i < 3; i++) {
            pulse_params prm = default_params();
            prm.attack = attacks[i];
            prm.sustain = 1.0f;
            prm.sweep = 0.0f;
            prm.detune = 0.0f;
            const ev_t e[] = {ev_on(0, 96, 127)}; /* 2.1 kHz so every window holds several cycles */
            const int n = (int)(5.0f * sr);
            float *a = buf(n), *b = buf(n);
            run_fresh(&p, sr, &prm, e, 1, a, b, n, 128);
            const int win = (int)(0.0005f * sr);
            const float fin = peak_of(a + n - (int)(0.1f * sr), (int)(0.1f * sr));
            int t90 = -1;
            for (int w = 0; w * win < n; w++)
                if (peak_of(a + w * win, win) >= 0.9f * fin) {
                    t90 = w * win;
                    break;
                }
            const double want = 0.9 * pulse_attack_s(attacks[i]);
            const double got = (double)t90 / sr;
            CHECK(fabs(got - want) < 0.15 * want + 0.0015, "attack %.2f: 90%% at %.4f s, expected about %.4f", (double)attacks[i], got, want);
            CHECK(got > prev, "attack time did not grow");
            prev = got;
            printf("ATTACK %.2f: 90%% of level at %.4f s (expected %.4f)\n", (double)attacks[i], got, want);
            free(a); free(b);
        }
    }

    /* SUSTAIN: the settled level is the sustain fraction of what sustain 1.0 gives for the same note */
    {
        const float sus[] = {0.25f, 0.6f};
        double ref_rms = 0;
        for (int i = -1; i < 2; i++) {
            pulse_params prm = default_params();
            prm.attack = 0.0f;
            prm.decay = 0.25f;
            prm.sustain = i < 0 ? 1.0f : sus[i];
            prm.sweep = 0.0f;
            prm.detune = 0.0f;
            prm.shape = 0.0f;
            const ev_t e[] = {ev_on(0, note, 127)};
            const int n = (int)(3.0f * sr);
            float *a = buf(n), *b = buf(n);
            run_fresh(&p, sr, &prm, e, 1, a, b, n, 128);
            const double late = rms_of(a + n - (int)(0.5f * sr), (int)(0.5f * sr));
            if (i < 0) {
                ref_rms = late;
            } else {
                const double ratio = late / ref_rms;
                CHECK(fabs(ratio - sus[i]) < 0.03, "sustain %.2f: settled at %.3f of the sustain-1 level", (double)sus[i], ratio);
                printf("SUSTAIN %.2f: settles at %.3f of the sustain-1 level\n", (double)sus[i], ratio);
            }
            free(a); free(b);
        }
        /* DECAY: with sustain 0, the level is 30 dB down at half the decay time */
        const float decays[] = {0.5f, 0.7f};
        for (int i = 0; i < 2; i++) {
            pulse_params prm = default_params();
            prm.attack = 0.0f;
            prm.decay = decays[i];
            prm.sustain = 0.0f;
            prm.sweep = 0.0f;
            prm.detune = 0.0f;
            prm.shape = 0.0f;
            const ev_t e[] = {ev_on(0, 96, 127)};
            const int n = (int)(1.0f * sr);
            float *a = buf(n), *b = buf(n);
            run_fresh(&p, sr, &prm, e, 1, a, b, n, 128);
            const float t60 = pulse_decay_s(decays[i]);
            const float early = peak_of(a + (int)(0.001f * sr), (int)(0.001f * sr));
            const float mid = peak_of(a + (int)(0.5f * t60 * sr), (int)(0.002f * sr));
            const double db = 20.0 * log10((double)mid / early);
            CHECK(db < -26.0 && db > -34.0, "decay %.2f (t60 %.3f s): %.1f dB at t60/2, expected about -30", (double)decays[i], (double)t60, db);
            printf("DECAY %.2f (t60 %.3f s): %.1f dB at half the time (expect -30)\n", (double)decays[i], (double)t60, db);
            free(a); free(b);
        }
    }

    /* LEVEL and VELOCITY scale the output */
    {
        double r[3];
        for (int i = 0; i < 3; i++) {
            pulse_params prm = default_params();
            prm.sustain = 1.0f;
            prm.level = i == 1 ? 0.4f : 0.8f;
            const ev_t e[] = {ev_on(0, note, i == 2 ? 64 : 127)};
            run_fresh(&p, sr, &prm, e, 1, L, R, total, 128);
            r[i] = rms_of(L + total / 2, total / 2);
        }
        CHECK(fabs(r[1] / r[0] - 0.5) < 0.02, "LEVEL 0.4 vs 0.8 should be half: %.3f", r[1] / r[0]);
        const double want_v = (0.15 + 0.85 * 64.0 / 127.0);
        CHECK(fabs(r[2] / r[0] - want_v) < 0.03, "velocity 64 vs 127: %.3f, expected %.3f", r[2] / r[0], want_v);
        printf("LEVEL 0.4/0.8 = %.3f (expect 0.5); velocity 64/127 = %.3f (expect %.3f)\n", r[1] / r[0], r[2] / r[0], want_v);
    }
    free(L); free(R);
}

static void test_determinism_and_block_independence(void)
{
    const float sr = 48000.0f;
    pulse_params prm = default_params();
    prm.release = 0.3f;
    ev_t e[16];
    int ne = 0;
    e[ne++] = ev_on(0, 52, 100);
    e[ne++] = ev_on(333, 59, 90);
    e[ne++] = ev_on(1000, 64, 127);
    e[ne++] = ev_f(4001, E_BEND, 0.7f);
    e[ne++] = ev_f(6003, E_MOD, 0.5f);
    e[ne++] = ev_off(9000, 59);
    prm.attack = 0.4f;
    e[ne++] = ev_params(12001, &prm);
    e[ne++] = ev_on(15000, 70, 100);
    e[ne++] = ev_on(15001, 52, 80);
    e[ne++] = ev_i(20000, E_PEDAL, 1);
    e[ne++] = ev_off(21000, 64);
    e[ne++] = ev_i(30000, E_PEDAL, 0);
    e[ne++] = ev_i(34000, E_ALLOFF, 0);
    const int total = 60000;
    float *L0 = buf(total), *R0 = buf(total), *L1 = buf(total), *R1 = buf(total);
    pulse_t p;
    run_fresh(&p, sr, &prm, e, ne, L0, R0, total, 128);
    CHECK(peak_of(L0, total) > 0.05f, "determinism scenario is silent");
    const int blocks[] = {1, 7, 64, 100, 1000, 60000};
    for (int bi = 0; bi < 6; bi++) {
        run_fresh(&p, sr, &prm, e, ne, L1, R1, total, blocks[bi]);
        CHECK(memcmp(L0, L1, sizeof(float) * (size_t)total) == 0 && memcmp(R0, R1, sizeof(float) * (size_t)total) == 0,
              "output depends on block size %d", blocks[bi]);
    }
    /* reset returns to the same state as a fresh init */
    pulse_reset(&p);
    run_events(&p, e, ne, L1, R1, total, 128);
    CHECK(memcmp(L0, L1, sizeof(float) * (size_t)total) == 0, "pulse_reset does not restore the initial state");
    printf("determinism: bit-identical output for blocks of 1, 7, 64, 100, 1000 and 60000 frames, and after reset\n");
    free(L0); free(R0); free(L1); free(R1);
}

static unsigned rng_state = 777u;
static unsigned rnd(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}
static float rndf(void) { return (float)(rnd() & 0xffff) / 65535.0f; }

static void test_stability(void)
{
    const float rates[] = {22050.0f, 32000.0f, 44100.0f, 48000.0f, 88200.0f, 96000.0f, 176400.0f, 192000.0f};
    const int blocks[] = {1, 17, 128};
    float worst = 0;
    long frames = 0;
    for (int ri = 0; ri < 8; ri++)
        for (int bi = 0; bi < 3; bi++) {
            const float sr = rates[ri];
            const int block = blocks[bi];
            const int total = block == 1 ? (int)(0.4f * sr) : (int)(1.5f * sr);
            float *L = buf(total), *R = buf(total);
            pulse_t p;
            pulse_params prm = default_params();
            pulse_init(&p, sr);
            pulse_set_params(&p, &prm);
            pulse_reset(&p);
            int pos = 0;
            while (pos < total) {
                /* random events and random parameter corners, including notes above Nyquist at low rates */
                const unsigned r = rnd() % 10;
                if (r < 3)
                    pulse_note_on(&p, (int)(rnd() % 128), 1 + (int)(rnd() % 127));
                else if (r < 5)
                    pulse_note_off(&p, (int)(rnd() % 128));
                else if (r == 5)
                    pulse_pitch_bend(&p, rndf() * 2 - 1);
                else if (r == 6)
                    pulse_mod_wheel(&p, rndf());
                else if (r == 7)
                    pulse_sustain_pedal(&p, (int)(rnd() & 1));
                else if (r == 8) {
                    prm.shape = (rnd() & 1) ? rndf() : (float)(rnd() & 1);
                    prm.sweep = (rnd() & 1) ? rndf() : (float)(rnd() & 1);
                    prm.rate = rndf();
                    prm.detune = (rnd() & 1) ? rndf() * 2 - 1 : (float)((int)(rnd() % 3) - 1);
                    prm.attack = rndf();
                    prm.decay = rndf();
                    prm.sustain = rndf();
                    prm.release = rndf();
                    prm.level = (rnd() & 1) ? 1.0f : rndf();
                    prm.bend_range = 1.0f + 11.0f * rndf();
                    pulse_set_params(&p, &prm);
                } else if ((rnd() % 40) == 0)
                    (rnd() & 1) ? pulse_all_off(&p) : pulse_all_sound_off(&p);
                int n = block == 1 ? 1 : (int)(1 + rnd() % (unsigned)(block * 2));
                if (pos + n > total)
                    n = total - pos;
                pulse_render(&p, L + pos, R + pos, n);
                pos += n;
            }
            CHECK(all_finite(L, total) && all_finite(R, total), "NaN or Inf at sr %.0f block %d", (double)sr, block);
            const float pk = fmaxf(peak_of(L, total), peak_of(R, total));
            worst = fmaxf(worst, pk);
            CHECK(pk <= 0.9801f, "peak %.4f at sr %.0f block %d", (double)pk, (double)sr, block);
            frames += total;
            free(L); free(R);
        }
    printf("stability: %ld frames of random notes, controls and events at 22.05k to 192k, blocks of 1 to 255; no NaN, worst peak %.4f\n", frames,
           (double)worst);
}

/* Moving the controls while a note sounds must not add steps beyond the waveform's own. */
static void test_parameter_smoothness(void)
{
    const float sr = 44100.0f;
    const int total = (int)(2.0f * sr);
    float *L = buf(total), *R = buf(total), *L2 = buf(total), *R2 = buf(total);
    pulse_t p;
    pulse_params prm = default_params();
    prm.sustain = 1.0f;
    prm.sweep = 0.0f;
    prm.detune = 0.0f;
    const ev_t note[] = {ev_on(0, 57, 110)};
    run_fresh(&p, sr, &prm, note, 1, L, R, total, 128);
    const float ref = fmaxf(max_step(L, total / 2, total), max_step(R, total / 2, total));

    /* jump SHAPE, SWEEP, DETUNE and LEVEL between their extremes every 128 frames */
    ev_t e[1 + 700];
    int ne = 0;
    e[ne++] = ev_on(0, 57, 110);
    int t = 4096, flip = 0;
    while (t < total - 200 && ne < 700) {
        pulse_params q = prm;
        q.shape = flip ? 0.0f : 1.0f;
        q.sweep = flip ? 1.0f : 0.0f;
        q.detune = flip ? -1.0f : 1.0f;
        q.level = flip ? 0.2f : 1.0f;
        q.rate = flip ? 0.9f : 0.1f;
        e[ne++] = ev_params(t, &q);
        flip ^= 1;
        t += 128;
    }
    run_fresh(&p, sr, &prm, e, ne, L2, R2, total, 128);
    const float got = fmaxf(max_step(L2, 4096, total), max_step(R2, 4096, total));
    CHECK(got <= 1.35f * ref, "parameter jumps add a step: %.4f vs steady %.4f (%.2fx)", (double)got, (double)ref, (double)(got / ref));
    CHECK(all_finite(L2, total), "NaN while jumping parameters");
    printf("parameter jumps every 128 frames: max step %.4f vs %.4f steady state (%.2fx, limit 1.35x)\n", (double)got, (double)ref, (double)(got / ref));
    free(L); free(R); free(L2); free(R2);
}

static void test_stereo_balance(void)
{
    /* Both channels carry signal. The PWM LFO moves energy between them (oscillator A leans left, B leans right and they sweep
     * opposite ways), so the balance is judged over two full LFO cycles (rate knob at its default, 0.36 Hz). */
    const float sr = 44100.0f;
    const int total = (int)(7.0f * sr);
    float *L = buf(total), *R = buf(total);
    pulse_t p;
    pulse_params prm = default_params();
    const ev_t e[] = {ev_on(0, 60, 110)};
    run_fresh(&p, sr, &prm, e, 1, L, R, total, 128);
    const int from = (int)(1.0f * sr);
    const double rl = rms_of(L + from, total - from), rr = rms_of(R + from, total - from);
    CHECK(rl > 0.01 && rr > 0.01, "silent channel");
    CHECK(fabs(20.0 * log10(rl / rr)) < 1.5, "channel balance %.2f dB", 20.0 * log10(rl / rr));
    printf("stereo balance at default settings over 6 s: %.2f dB L over R\n", 20.0 * log10(rl / rr));
    free(L); free(R);
}

static void test_no_stuck_voices(void)
{
    const float sr = 44100.0f;
    pulse_params prm = default_params();
    prm.release = 0.2f;
    const int total = (int)(2.0f * sr);
    float *L = buf(total), *R = buf(total);
    pulse_t p;
    ev_t e[40];
    int ne = 0;
    for (int i = 0; i < 20; i++) {
        e[ne++] = ev_on(i * 300, 40 + (i * 7) % 40, 100);
        e[ne++] = ev_off(i * 300 + 150, 40 + (i * 7) % 40);
    }
    run_fresh(&p, sr, &prm, e, ne, L, R, total, 128);
    CHECK(pulse_active_voices(&p) == 0, "%d voices stuck after all notes were released", pulse_active_voices(&p));
    CHECK(peak_of(L + total - 100, 100) == 0.0f, "not silent");
    /* a note-off for a note stolen while it waited must not leave a voice hanging */
    const ev_t e2[] = {ev_on(0, 40, 100), ev_on(10, 41, 100), ev_on(20, 42, 100), ev_on(30, 43, 100), ev_on(40, 44, 100), ev_on(50, 45, 100),
                       ev_on(60, 46, 100), ev_on(70, 47, 100), ev_on(100, 70, 100), ev_off(101, 70), ev_off(200, 40), ev_off(200, 41),
                       ev_off(200, 42), ev_off(200, 43), ev_off(200, 44), ev_off(200, 45), ev_off(200, 46), ev_off(200, 47)};
    run_fresh(&p, sr, &prm, e2, 18, L, R, total, 128);
    CHECK(pulse_active_voices(&p) == 0, "%d voices stuck after steal then note-off", pulse_active_voices(&p));
    printf("voice bookkeeping: no stuck voices after note storms or a steal followed by its note-off\n");
    free(L); free(R);
}

#ifdef PULSE_TIMING
static void timing(void)
{
    const float sr = 44100.0f;
    pulse_t p;
    pulse_params prm = default_params();
    pulse_init(&p, sr);
    pulse_set_params(&p, &prm);
    pulse_reset(&p);
    static float L[128], R[128];
    for (int i = 0; i < 8; i++)
        pulse_note_on(&p, 48 + 3 * i, 100);
    const int blocks = (int)(sr / 128.0f) * 20;
    clock_t t0 = clock();
    for (int b = 0; b < blocks; b++)
        pulse_render(&p, L, R, 128);
    const double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
    const double per_block_us = 1e6 * s / blocks;
    printf("timing (this host, 8 voices held, 44.1 kHz, 128-frame blocks): %.1f us per block = %.2f%% of the 2902 us block budget\n", per_block_us,
           100.0 * per_block_us / 2902.0);
}
#endif

int main(int argc, char **argv)
{
#ifdef PULSE_TIMING
    if (argc > 1 && !strcmp(argv[1], "--timing")) {
        timing();
        return 0;
    }
#else
    (void)argc;
    (void)argv;
#endif
    test_pitch_accuracy();
    test_polyphony_and_headroom();
    test_release_to_silence();
    test_sustain_pedal_and_all_off();
    test_steal_and_retrigger();
    test_controls();
    test_determinism_and_block_independence();
    test_stability();
    test_parameter_smoothness();
    test_stereo_balance();
    test_no_stuck_voices();
    if (failures) {
        printf("%d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("ALL %d CHECKS PASSED\n", checks);
    return 0;
}
