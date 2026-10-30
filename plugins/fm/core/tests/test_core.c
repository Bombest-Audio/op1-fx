/* Behavioral tests for the Fm core. Build and run with core/tests/run.sh */
#include "../fm_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

/* ---- event scripting ---- */

enum { EV_ON, EV_OFF, EV_BEND, EV_PEDAL, EV_ALLOFF, EV_MOD };
typedef struct {
    int frame;
    int type;
    int a;
    float b;
} event;

static void apply(fm_t *f, const event *e)
{
    switch (e->type) {
    case EV_ON: fm_note_on(f, e->a, (int)e->b); break;
    case EV_OFF: fm_note_off(f, e->a); break;
    case EV_BEND: fm_pitch_bend(f, e->b); break;
    case EV_PEDAL: fm_sustain_pedal(f, e->a); break;
    case EV_ALLOFF: fm_all_off(f); break;
    case EV_MOD: fm_mod_wheel(f, e->b); break;
    }
}

/* Render total frames with events at exact frames, calls never longer than block. Events must be sorted. */
static void run_script(fm_t *f, const event *ev, int nev, float *l, float *r, int total, int block)
{
    int cur = 0, ei = 0;
    while (cur < total) {
        while (ei < nev && ev[ei].frame <= cur)
            apply(f, &ev[ei++]);
        int n = total - cur;
        if (n > block)
            n = block;
        if (ei < nev && ev[ei].frame - cur < n)
            n = ev[ei].frame - cur;
        fm_render(f, l + cur, r + cur, n);
        cur += n;
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

static float max_step(const float *x, int a, int b)
{
    float m = 0;
    for (int i = a + 1; i < b; i++)
        m = fmaxf(m, fabsf(x[i] - x[i - 1]));
    return m;
}

static int all_finite(const float *x, int n)
{
    for (int i = 0; i < n; i++)
        if (!isfinite(x[i]))
            return 0;
    return 1;
}

static float cents(double f, double ref)
{
    return (float)(1200.0 * log2(f / ref));
}

static double note_hz(double note)
{
    return 440.0 * pow(2.0, (note - 69.0) / 12.0);
}

/* A plain sine: no modulation, no second pair, flat envelope. */
static fm_params pure_params(void)
{
    fm_params p;
    fm_default_params(&p);
    p.index = 0.0f;
    p.feedback = 0.0f;
    p.layer = 0.0f;
    p.attack = 0.001f;
    p.sustain = 1.0f;
    p.level = 1.0f;
    p.key_track = 0.0f;
    return p;
}

/* Frequency from rising zero crossings with linear interpolation. */
static double zero_cross_hz(const float *x, int a, int b, double sr)
{
    double first = -1, last = -1;
    int count = 0;
    for (int i = a + 1; i < b; i++) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double t = (double)(i - 1) + (double)(-x[i - 1]) / (double)(x[i] - x[i - 1]);
            if (first < 0)
                first = t;
            last = t;
            count++;
        }
    }
    if (count < 3)
        return 0;
    return (double)(count - 1) / (last - first) * sr;
}

/* Fundamental from the autocorrelation peak near the expected period, parabolic interpolation. */
static double autocorr_hz(const float *x, int a, int len, double sr, double expect)
{
    const double P = sr / expect;
    const int lo = (int)(P * 0.7), hi = (int)(P * 1.3) + 1;
    double best = -1e30;
    int bl = lo;
    static double ac[20000];
    for (int lag = lo - 1; lag <= hi + 1; lag++) {
        double s = 0;
        for (int i = 0; i < len; i++)
            s += (double)x[a + i] * x[a + i + lag];
        ac[lag - (lo - 1)] = s;
        if (lag >= lo && lag <= hi && s > best) {
            best = s;
            bl = lag;
        }
    }
    const double y0 = ac[bl - 1 - (lo - 1)], y1 = ac[bl - (lo - 1)], y2 = ac[bl + 1 - (lo - 1)];
    const double d = y0 - 2 * y1 + y2;
    const double off = d != 0 ? 0.5 * (y0 - y2) / d : 0;
    return sr / ((double)bl + off);
}

/* ---- spectrum ---- */

#define NFFT 2048
static void fft(double *re, double *im)
{
    for (int i = 1, j = 0; i < NFFT; i++) {
        int bit = NFFT >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= NFFT; len <<= 1) {
        const double ang = -2 * PI / len;
        for (int i = 0; i < NFFT; i += len)
            for (int k = 0; k < len / 2; k++) {
                const double wr = cos(ang * k), wi = sin(ang * k);
                const int u = i + k, v = i + k + len / 2;
                const double xr = re[v] * wr - im[v] * wi, xi = re[v] * wi + im[v] * wr;
                re[v] = re[u] - xr; im[v] = im[u] - xi;
                re[u] += xr; im[u] += xi;
            }
    }
}

/* dB magnitude spectrum of a Hann window starting at a, bins 0..NFFT/2-1 */
static void spectrum_db(const float *x, int a, double *db)
{
    static double re[NFFT], im[NFFT];
    for (int i = 0; i < NFFT; i++) {
        re[i] = (double)x[a + i] * (0.5 - 0.5 * cos(2 * PI * i / NFFT));
        im[i] = 0;
    }
    fft(re, im);
    for (int k = 0; k < NFFT / 2; k++)
        db[k] = 10.0 * log10(re[k] * re[k] + im[k] * im[k] + 1e-18);
}

/* Mean absolute dB difference over bins that carry signal in either spectrum (within 70 dB of the peak). */
static double spectral_distance(const double *a, const double *b)
{
    double top = -1e9;
    for (int k = 0; k < NFFT / 2; k++)
        top = fmax(top, fmax(a[k], b[k]));
    double sum = 0;
    int n = 0;
    for (int k = 1; k < NFFT / 2; k++)
        if (a[k] > top - 70 || b[k] > top - 70) {
            sum += fabs(a[k] - b[k]);
            n++;
        }
    return n ? sum / n : 0;
}

static double centroid_hz(const double *db, double sr)
{
    double num = 0, den = 0;
    for (int k = 1; k < NFFT / 2; k++) {
        const double m = pow(10.0, db[k] / 20.0);
        num += m * k * sr / NFFT;
        den += m;
    }
    return den > 0 ? num / den : 0;
}

/* One held note at 44.1 kHz, returns the spectrum 0.25 s in. */
static void note_spectrum(const fm_params *p, int note, int vel, double *db)
{
    enum { N = 22050 };
    static float l[N], r[N];
    fm_t *f = malloc(sizeof *f);
    fm_init(f, 44100.0f);
    fm_set_params(f, p);
    fm_reset(f);
    event ev[] = {{0, EV_ON, note, (float)vel}};
    run_script(f, ev, 1, l, r, N, 128);
    spectrum_db(l, 11025, db);
    free(f);
}

/* ---- tests ---- */

static void test_factory_defaults(void)
{
    /* knob raw values 20000, 4500, 3000, 18000 and extras 15000, 0, 100, 1500 of 32767 */
    CHECK(fabsf(FM_DEFAULT_RATIO - 20000.0f / 32767.0f) < 1e-4f, "ratio default");
    CHECK(fabsf(FM_DEFAULT_INDEX - 4500.0f / 32767.0f) < 1e-4f, "index default");
    CHECK(fabsf(FM_DEFAULT_FEEDBACK - 3000.0f / 32767.0f) < 1e-4f, "feedback default");
    CHECK(fabsf(FM_DEFAULT_LAYER - 18000.0f / 32767.0f) < 1e-4f, "layer default");
    CHECK(fabsf(FM_DEFAULT_MOD_DECAY - 15000.0f / 32767.0f) < 1e-4f, "mod decay default");
    CHECK(FM_DEFAULT_DETUNE == 0.0f, "detune default");
    CHECK(fabsf(FM_DEFAULT_VEL_INDEX - 100.0f / 32767.0f) < 1e-4f, "vel index default");
    CHECK(fabsf(FM_DEFAULT_KEY_TRACK - 1500.0f / 32767.0f) < 1e-4f, "key track default");
    printf("  default ratio %.3f, index %.3f rad, feedback %.3f rad, mod decay %.3f s\n",
           (double)fm_ratio_value(FM_DEFAULT_RATIO), (double)fm_index_value(FM_DEFAULT_INDEX),
           (double)fm_feedback_value(FM_DEFAULT_FEEDBACK), (double)fm_mod_decay_seconds(FM_DEFAULT_MOD_DECAY));
}

static void test_mappings(void)
{
    float prev = -1, prevI = -1;
    for (int i = 0; i <= 4096; i++) {
        const float n = (float)i / 4096.0f;
        const float r = fm_ratio_value(n), x = fm_index_value(n);
        CHECK(r >= prev, "ratio mapping not monotone at %f (%f < %f)", (double)n, (double)r, (double)prev);
        CHECK(x >= prevI, "index mapping not monotone at %f", (double)n);
        prev = r;
        prevI = x;
    }
    CHECK(fabsf(fm_ratio_value(0.0f) - 0.5f) < 1e-6f, "ratio min %f", (double)fm_ratio_value(0.0f));
    CHECK(fabsf(fm_ratio_value(1.0f) - 16.0f) < 1e-4f, "ratio max %f", (double)fm_ratio_value(1.0f));
    CHECK(fabsf(fm_ratio_value(0.4f) - 2.0f) < 1e-5f, "ratio 0.4 should be exactly 2, got %f", (double)fm_ratio_value(0.4f));
    CHECK(fm_index_value(0.0f) == 0.0f && fabsf(fm_index_value(1.0f) - 16.0f) < 1e-4f, "index range");
    CHECK(fabsf(fm_level_gain(1.0f) - 1.0f) < 1e-6f && fm_level_gain(0.0f) == 0.0f, "level range");
}

static void test_silence_when_idle(void)
{
    enum { N = 4096 };
    static float l[N], r[N];
    fm_t *f = malloc(sizeof *f);
    fm_init(f, 44100.0f);
    for (int i = 0; i < N; i++)
        l[i] = r[i] = 1.0f;
    fm_render(f, l, r, N);
    CHECK(peak(l, N) == 0.0f && peak(r, N) == 0.0f, "idle synth must output exact zeros");
    free(f);
}

static void test_pitch_pure(void)
{
    const float rates[] = {22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f};
    const int notes[] = {24, 36, 48, 60, 69, 84, 96};
    float worst = 0;
    for (int ri = 0; ri < 5; ri++)
        for (int ni = 0; ni < 7; ni++) {
            const double sr = rates[ri];
            const double hz = note_hz(notes[ni]);
            if (hz > sr * 0.2)
                continue;
            const int total = (int)(sr * 1.5);
            float *l = malloc(sizeof(float) * (size_t)total), *r = malloc(sizeof(float) * (size_t)total);
            fm_t *f = malloc(sizeof *f);
            fm_init(f, (float)sr);
            fm_params p = pure_params();
            fm_set_params(f, &p);
            event ev[] = {{0, EV_ON, notes[ni], 127}};
            run_script(f, ev, 1, l, r, total, 256);
            const double m = zero_cross_hz(l, (int)(sr * 0.2), total, sr);
            const float c = cents(m, hz);
            worst = fmaxf(worst, fabsf(c));
            CHECK(fabsf(c) < 5.0f, "note %d at %.0f Hz measured %.3f Hz (%.2f cents)", notes[ni], sr, m, (double)c);
            free(f);
            free(l);
            free(r);
        }
    printf("  pure sine pitch: worst error %.3f cents over notes and rates\n", (double)worst);
}

static void test_pitch_fm(void)
{
    /* Whole-number ratio, so the FM spectrum is harmonic and the period is that of the played note. */
    enum { N = 88200 };
    static float l[N], r[N];
    const int notes[] = {45, 57, 69, 81};
    float worst = 0;
    for (int ni = 0; ni < 4; ni++) {
        fm_t *f = malloc(sizeof *f);
        fm_init(f, 44100.0f);
        fm_params p;
        fm_default_params(&p);
        p.ratio = 0.4f; /* ratio 2 */
        p.index = 0.35f;
        p.feedback = 0.15f;
        p.layer = 0.6f;
        p.sustain = 0.8f;
        fm_set_params(f, &p);
        event ev[] = {{0, EV_ON, notes[ni], 100}};
        run_script(f, ev, 1, l, r, N, 128);
        const double hz = note_hz(notes[ni]);
        const double m = autocorr_hz(l, 22050, 8192, 44100.0, hz);
        const float c = cents(m, hz);
        worst = fmaxf(worst, fabsf(c));
        CHECK(fabsf(c) < 5.0f, "FM note %d measured %.3f Hz vs %.3f (%.2f cents)", notes[ni], m, hz, (double)c);
        free(f);
    }
    printf("  FM pitch (ratio 2, layer on): worst error %.3f cents\n", (double)worst);
}

static void test_bend(void)
{
    enum { N = 66150 };
    static float l[N], r[N];
    fm_t *f = malloc(sizeof *f);
    fm_init(f, 44100.0f);
    fm_params p = pure_params();
    fm_set_params(f, &p);
    event ev[] = {{0, EV_ON, 60, 100}, {22050, EV_BEND, 0, 1.0f}};
    run_script(f, ev, 2, l, r, N, 128);
    const double before = zero_cross_hz(l, 11025, 22050, 44100.0);
    const double after = zero_cross_hz(l, 44100, N, 44100.0);
    const float up = cents(after, before);
    CHECK(fabsf(up - 200.0f) < 5.0f, "full bend with the default range should be +200 cents, got %.2f", (double)up);
    free(f);
    printf("  full bend up: %+.2f cents (range 2 semitones)\n", (double)up);
}

static void test_polyphony_headroom(void)
{
    enum { N = 44100 };
    static float l[N], r[N];
    const int chord[8] = {36, 43, 52, 60, 64, 67, 72, 79};
    float worst_peak = 0;
    for (int set = 0; set < 4; set++) {
        fm_t *f = malloc(sizeof *f);
        fm_init(f, 44100.0f);
        fm_params p;
        fm_default_params(&p);
        if (set == 1) { p.index = 1.0f; p.feedback = 1.0f; p.layer = 1.0f; p.level = 1.0f; }
        if (set == 2) { p.index = 0.0f; p.feedback = 0.0f; p.layer = 0.0f; p.level = 1.0f; p.sustain = 1.0f; p.attack = 0.0005f; }
        if (set == 3) { p.ratio = 1.0f; p.index = 1.0f; p.layer = 1.0f; p.level = 1.0f; p.mod_decay = 1.0f; }
        fm_set_params(f, &p);
        event ev[16];
        for (int i = 0; i < 8; i++) {
            ev[i].frame = i * 7; ev[i].type = EV_ON; ev[i].a = chord[i] + (set == 2 ? 0 : 0); ev[i].b = 127;
        }
        run_script(f, ev, 8, l, r, N, 128);
        const float pk = fmaxf(peak(l, N), peak(r, N));
        worst_peak = fmaxf(worst_peak, pk);
        CHECK(all_finite(l, N) && all_finite(r, N), "NaN or Inf with 8 voices (set %d)", set);
        CHECK(pk < 1.0f, "peak %.4f reaches 0 dBFS with 8 voices at velocity 127 (set %d)", (double)pk, set);
        CHECK(fm_active_voices(f) == 8, "expected 8 active voices, got %d", fm_active_voices(f));
        free(f);
    }
    printf("  8 voices, velocity 127, four parameter sets: worst peak %.4f (%.2f dBFS)\n", (double)worst_peak,
           20.0 * log10((double)worst_peak));
}

static void test_unison_headroom(void)
{
    /* The worst case for the sum: eight voices on one note's partials in phase is not reachable (a note
     * retriggers one voice), so use the closest thing: eight neighbouring semitones plus the pedal. */
    enum { N = 22050 };
    static float l[N], r[N];
    fm_t *f = malloc(sizeof *f);
    fm_init(f, 44100.0f);
    fm_params p = pure_params();
    fm_set_params(f, &p);
    event ev[8];
    for (int i = 0; i < 8; i++) { ev[i].frame = 0; ev[i].type = EV_ON; ev[i].a = 60 + i; ev[i].b = 127; }
    run_script(f, ev, 8, l, r, N, 128);
    const float pk = fmaxf(peak(l, N), peak(r, N));
    CHECK(pk < 1.0f, "peak %.4f", (double)pk);
    printf("  8 adjacent semitones, pure sines, level 1: peak %.4f\n", (double)pk);
    free(f);
}

static void test_release_to_silence(void)
{
    enum { N = 132300 };
    static float l[N], r[N];
    fm_t *f = malloc(sizeof *f);
    fm_init(f, 44100.0f);
    fm_params p;
    fm_default_params(&p);
    p.release = 0.1f;
    p.level = 1.0f;
    fm_set_params(f, &p);
    event ev[16];
    int n = 0;
    for (int i = 0; i < 8; i++) { ev[n].frame = 0; ev[n].type = EV_ON; ev[n].a = 50 + i * 3; ev[n].b = 110; n++; }
    for (int i = 0; i < 8; i++) { ev[n].frame = 22050; ev[n].type = EV_OFF; ev[n].a = 50 + i * 3; ev[n].b = 0; n++; }
    run_script(f, ev, n, l, r, N, 128);
    const float held = (float)rms(l + 11025, 4096);
    const float tail = peak(l + 66150, N - 66150);
    CHECK(tail == 0.0f, "output after release should be exactly zero, peak %.2e", (double)tail);
    CHECK(fm_active_voices(f) == 0, "voices still active after release: %d", fm_active_voices(f));
    /* a release of 100 ms is 60 dB down at 100 ms: check about 150 ms after the note off */
    const float at150 = (float)rms(l + 22050 + 6615, 441);
    CHECK(at150 < held * 0.02f, "150 ms into a 100 ms release the level should be under -34 dB (%.4f vs %.4f)", (double)at150, (double)held);
    printf("  release 100 ms: rms held %.4f, at +150 ms %.5f, silent after 1.5 s\n", (double)held, (double)at150);
    free(f);
}

static void test_pedal_and_all_off(void)
{
    enum { N = 88200 };
    static float l[N], r[N];
    fm_t *f = malloc(sizeof *f);
    fm_init(f, 44100.0f);
    fm_params p;
    fm_default_params(&p);
    p.release = 0.05f;
    p.sustain = 0.8f;
    fm_set_params(f, &p);
    event ev[] = {{0, EV_PEDAL, 1, 0}, {0, EV_ON, 60, 100}, {4410, EV_OFF, 60, 0}, {44100, EV_PEDAL, 0, 0}};
    run_script(f, ev, 4, l, r, N, 128);
    const double during = rms(l + 30000, 4000);
    const double after = peak(l + 66150, N - 66150);
    CHECK(during > 0.01, "note should keep sounding while the pedal is down (rms %.5f)", during);
    CHECK(after == 0.0, "note should end after the pedal lifts (peak %.2e)", after);

    fm_init(f, 44100.0f);
    fm_set_params(f, &p);
    event ev2[] = {{0, EV_ON, 60, 100}, {0, EV_ON, 64, 100}, {0, EV_PEDAL, 1, 0}, {2000, EV_ALLOFF, 0, 0}};
    run_script(f, ev2, 4, l, r, N, 128);
    CHECK(peak(l + 44100, N - 44100) == 0.0f, "all notes off should end every voice");
    CHECK(fm_active_voices(f) == 0, "voices left after all notes off");
    free(f);
    printf("  sustain pedal holds a released note, lifting it or all-notes-off ends the voices\n");
}

/* A note that takes over a sounding voice must not step harder than the same note arriving on a free voice.
 * Each variant is run twice with the same new note: once with all 8 voices busy (a steal) and once with
 * one voice free (a plain new note). The free-voice run is the natural attack, so it sets the bar. */
static float steal_run(int variant, int busy, float *l, float *r, int total, int hit)
{
    fm_t *f = malloc(sizeof *f);
    fm_init(f, 44100.0f);
    fm_params p;
    fm_default_params(&p);
    p.sustain = 0.8f;
    p.mod_decay = 0.3f;
    p.index = 0.4f;
    if (variant == 3)
        p.attack = 0.0005f;
    fm_set_params(f, &p);
    event ev[24];
    int n = 0;
    const int chord[8] = {40, 47, 52, 59, 64, 67, 71, 76};
    for (int i = 0; i < busy; i++) { ev[n].frame = i; ev[n].type = EV_ON; ev[n].a = chord[i]; ev[n].b = 90; n++; }
    if (variant == 0) { ev[n].frame = hit; ev[n].type = EV_ON; ev[n].a = 83; ev[n].b = 127; n++; }
    if (variant == 1) { ev[n].frame = hit; ev[n].type = EV_ON; ev[n].a = 64; ev[n].b = 127; n++; }
    if (variant == 2) {
        ev[n].frame = hit - 1000; ev[n].type = EV_OFF; ev[n].a = 52; ev[n].b = 0; n++;
        ev[n].frame = hit; ev[n].type = EV_ON; ev[n].a = 88; ev[n].b = 127; n++;
    }
    if (variant == 3) { ev[n].frame = hit; ev[n].type = EV_ON; ev[n].a = 30; ev[n].b = 127; n++; }
    run_script(f, ev, n, l, r, total, 128);
    free(f);
    return fmaxf(max_step(l, hit, hit + 2000), max_step(r, hit, hit + 2000));
}

static void test_no_click_steal_and_retrigger(void)
{
    enum { N = 88200 };
    static float l[N], r[N];
    const char *names[4] = {"steal the oldest voice", "retrigger a held note", "steal a voice in release",
                            "steal with the fastest attack"};
    float worst_ratio = 0;
    for (int variant = 0; variant < 4; variant++) {
        const int hit = 44100;
        const float stolen = steal_run(variant, 8, l, r, N, hit);
        const float steady = fmaxf(max_step(l, hit - 8000, hit), max_step(r, hit - 8000, hit));
        const float natural = steal_run(variant, 7, l, r, N, hit);
        const float ratio = stolen / natural;
        worst_ratio = fmaxf(worst_ratio, ratio);
        printf("  %-30s: max step %.4f, same note on a free voice %.4f (x%.2f), steady chord %.4f\n", names[variant],
               (double)stolen, (double)natural, (double)ratio, (double)steady);
        /* A hard step in a voice would be as large as the voice itself (about 0.1 to 0.3 in the sum). */
        CHECK(ratio < 1.25f, "variant %d: a steal steps %.2fx harder than a new note on a free voice", variant, (double)ratio);
        CHECK(stolen < 0.5f, "variant %d: step %.4f is too large", variant, (double)stolen);
    }
    printf("  steal and retrigger: worst ratio to a free-voice note %.2f\n", (double)worst_ratio);
}

static void test_attack_start_smooth(void)
{
    enum { N = 4410 };
    static float l[N], r[N];
    fm_t *f = malloc(sizeof *f);
    fm_init(f, 44100.0f);
    fm_params p = pure_params();
    p.attack = 0.0005f;
    fm_set_params(f, &p);
    event ev[] = {{100, EV_ON, 96, 127}};
    run_script(f, ev, 1, l, r, N, 128);
    const float first = fabsf(l[100]) + fabsf(r[100]);
    const float steady = max_step(l, 1000, 3000);
    CHECK(first < 0.05f, "first sample of a note is %.4f", (double)first);
    CHECK(max_step(l, 90, 400) < 1.6f * steady + 0.02f, "start of a 0.5 ms attack steps by %.4f (steady %.4f)",
          (double)max_step(l, 90, 400), (double)steady);
    printf("  0.5 ms attack on note 96: first sample %.5f, max start step %.4f vs steady %.4f\n", (double)first,
           (double)max_step(l, 90, 400), (double)steady);
    free(f);
}

static void test_knobs_change_spectrum(void)
{
    static double a[NFFT / 2], b[NFFT / 2];
    const char *names[4] = {"ratio", "index", "feedback", "layer"};
    double dist[4], c_lo[4], c_hi[4];
    for (int k = 0; k < 4; k++) {
        fm_params lo, hi;
        fm_default_params(&lo);
        hi = lo;
        float *pl = k == 0 ? &lo.ratio : k == 1 ? &lo.index : k == 2 ? &lo.feedback : &lo.layer;
        float *ph = k == 0 ? &hi.ratio : k == 1 ? &hi.index : k == 2 ? &hi.feedback : &hi.layer;
        *pl = 0.0f;
        *ph = 1.0f;
        if (k == 2) { lo.index = hi.index = 0.45f; } /* feedback is only audible when the modulator is */
        if (k == 0) { lo.index = hi.index = 0.45f; }
        if (k == 3) { lo.index = hi.index = 0.30f; }
        note_spectrum(&lo, 57, 100, a);
        note_spectrum(&hi, 57, 100, b);
        dist[k] = spectral_distance(a, b);
        c_lo[k] = centroid_hz(a, 44100.0);
        c_hi[k] = centroid_hz(b, 44100.0);
        CHECK(dist[k] > 2.0, "%s: spectra differ by only %.2f dB", names[k], dist[k]);
        printf("  %-8s knob 0 -> 1: mean spectral difference %5.1f dB, centroid %6.0f -> %6.0f Hz\n", names[k], dist[k],
               c_lo[k], c_hi[k]);
    }
    CHECK(c_hi[1] > c_lo[1] * 1.5, "INDEX should raise the spectral centroid");
    CHECK(c_hi[2] > c_lo[2], "FEEDBACK should raise the spectral centroid");
}

static void test_wheel_velocity_extras(void)
{
    static double a[NFFT / 2], b[NFFT / 2];
    fm_params p;
    fm_default_params(&p);
    p.index = 0.3f;
    note_spectrum(&p, 57, 100, a);
    /* mod wheel: raise it before the note */
    {
        enum { N = 22050 };
        static float l[N], r[N];
        fm_t *f = malloc(sizeof *f);
        fm_init(f, 44100.0f);
        fm_set_params(f, &p);
        event ev[] = {{0, EV_MOD, 0, 1.0f}, {2000, EV_ON, 57, 100}};
        run_script(f, ev, 2, l, r, N, 128);
        spectrum_db(l, 11025, b);
        const double d = spectral_distance(a, b);
        CHECK(d > 1.0, "mod wheel changes the spectrum by only %.2f dB", d);
        printf("  mod wheel 0 -> 1: spectral difference %.1f dB, centroid %.0f -> %.0f Hz\n", d, centroid_hz(a, 44100.0),
               centroid_hz(b, 44100.0));
        free(f);
    }
    /* velocity to index at full sensitivity */
    fm_params q = p;
    q.vel_index = 1.0f;
    note_spectrum(&q, 57, 127, a);
    note_spectrum(&q, 57, 30, b);
    const double d = spectral_distance(a, b);
    CHECK(d > 1.0, "vel_index 1 should make velocity change the spectrum (%.2f dB)", d);
    printf("  velocity to index at 1.0: 127 vs 30 differ by %.1f dB\n", d);

    /* velocity scales level on a pure tone: 127 vs 64 is about 6 dB */
    enum { M = 22050 };
    static float l[M], r[M], l2[M], r2[M];
    fm_params pp = pure_params();
    fm_t *f = malloc(sizeof *f);
    fm_init(f, 44100.0f);
    fm_set_params(f, &pp);
    event e1[] = {{0, EV_ON, 69, 127}};
    run_script(f, e1, 1, l, r, M, 128);
    fm_init(f, 44100.0f);
    fm_set_params(f, &pp);
    event e2[] = {{0, EV_ON, 69, 64}};
    run_script(f, e2, 1, l2, r2, M, 128);
    const double ratio = rms(l2 + 8000, 8000) / rms(l + 8000, 8000);
    CHECK(fabs(ratio - 64.0 / 127.0) < 0.01, "velocity 64 vs 127 amplitude ratio %.4f", ratio);
    free(f);
}

static void test_envelope(void)
{
    enum { N = 88200 };
    static float l[N], r[N];
    fm_t *f = malloc(sizeof *f);
    /* attack time */
    fm_init(f, 44100.0f);
    fm_params p = pure_params();
    p.attack = 0.3f;
    p.sustain = 0.5f;
    p.decay = 0.2f;
    fm_set_params(f, &p);
    event ev[] = {{0, EV_ON, 69, 127}};
    run_script(f, ev, 1, l, r, N, 128);
    const double early = rms(l + 441, 441);
    const double peakr = rms(l + 13000, 441);
    const double late = rms(l + 60000, 4410);
    CHECK(early < 0.1 * peakr, "a 300 ms attack should start quietly (%.4f vs %.4f)", early, peakr);
    CHECK(fabs(late / peakr - 0.5 * 1.0) < 0.12, "sustain 0.5 should settle near half the peak (%.3f)", late / peakr);
    printf("  envelope: 300 ms attack starts at %.1f%% of peak, sustain 0.5 settles at %.1f%%\n", 100 * early / peakr,
           100 * late / peakr);
    free(f);
}

static void test_deterministic_and_block_size(void)
{
    enum { N = 66150 };
    static float la[N], ra[N], lb[N], rb[N];
    const int sizes[] = {1, 7, 13, 64, 128, 1000, 4096};
    event ev[32];
    int n = 0;
    srand(7);
    for (int i = 0; i < 14; i++) {
        ev[n].frame = i * 2500 + (i * 37) % 300;
        ev[n].type = EV_ON;
        ev[n].a = 36 + (i * 11) % 50;
        ev[n].b = (float)(40 + (i * 29) % 87);
        n++;
    }
    ev[n].frame = 20000; ev[n].type = EV_BEND; ev[n].a = 0; ev[n].b = 0.6f; n++;
    ev[n].frame = 30000; ev[n].type = EV_OFF; ev[n].a = 36 + (3 * 11) % 50; ev[n].b = 0; n++;
    /* sort by frame */
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (ev[j].frame < ev[i].frame) { event t = ev[i]; ev[i] = ev[j]; ev[j] = t; }

    fm_params p;
    fm_default_params(&p);
    p.release = 0.3f;
    fm_t *f = malloc(sizeof *f);
    fm_init(f, 44100.0f);
    fm_set_params(f, &p);
    run_script(f, ev, n, la, ra, N, 4096);
    CHECK(peak(la, N) > 0.05f, "script produced no sound");
    for (int s = 0; s < 7; s++) {
        fm_init(f, 44100.0f);
        fm_set_params(f, &p);
        memset(lb, 0, sizeof lb);
        run_script(f, ev, n, lb, rb, N, sizes[s]);
        CHECK(memcmp(la, lb, sizeof la) == 0 && memcmp(ra, rb, sizeof ra) == 0,
              "output differs between block size 4096 and %d", sizes[s]);
    }
    /* reset returns to the initial state */
    fm_reset(f);
    fm_set_params(f, &p);
    run_script(f, ev, n, lb, rb, N, 128);
    CHECK(memcmp(la, lb, sizeof la) == 0, "output after fm_reset differs from a fresh instance");
    free(f);
    printf("  bit-identical output for block sizes 1, 7, 13, 64, 128, 1000, 4096 and after reset\n");
}

static void test_sample_rates_and_extremes(void)
{
    const float rates[] = {22050.0f, 32000.0f, 44100.0f, 48000.0f, 88200.0f, 96000.0f, 192000.0f};
    for (int ri = 0; ri < 7; ri++) {
        const int N = (int)(rates[ri] * 0.5f);
        float *l = malloc(sizeof(float) * (size_t)N), *r = malloc(sizeof(float) * (size_t)N);
        fm_t *f = malloc(sizeof *f);
        for (int corner = 0; corner < 4; corner++) {
            fm_init(f, rates[ri]);
            fm_params p;
            fm_default_params(&p);
            p.ratio = (corner & 1) ? 1.0f : 0.0f;
            p.index = (corner & 2) ? 1.0f : 0.0f;
            p.feedback = (corner & 2) ? 1.0f : 0.0f;
            p.layer = 1.0f;
            p.level = 1.0f;
            p.detune = corner == 3 ? 1.0f : -1.0f;
            p.attack = 0.0005f;
            fm_set_params(f, &p);
            event ev[10];
            for (int i = 0; i < 10; i++) { ev[i].frame = i * 100; ev[i].type = EV_ON; ev[i].a = i == 9 ? 127 : 12 + i * 13; ev[i].b = 127; }
            ev[9].frame = 900;
            run_script(f, ev, 10, l, r, N, 128);
            const float pk = fmaxf(peak(l, N), peak(r, N));
            CHECK(all_finite(l, N) && all_finite(r, N), "non-finite at %.0f Hz corner %d", (double)rates[ri], corner);
            CHECK(pk < 1.0f, "peak %.4f at %.0f Hz corner %d", (double)pk, (double)rates[ri], corner);
        }
        free(f);
        free(l);
        free(r);
    }
    printf("  22.05 to 192 kHz, extreme corners, 10 notes incl. 127: finite and under 0 dBFS\n");
}

/* Render a held chord. Every 128 samples the chosen control jumps between lo and hi (toggle) or stays at lo. */
static float sweep_run(int knob, float lo, float hi, int toggle, float *l, float *r, int total)
{
    fm_params p;
    fm_default_params(&p);
    p.sustain = 1.0f;
    p.mod_decay = 0.0f;
    p.attack = 0.002f;
    p.level = 0.8f;
    fm_t *f = malloc(sizeof *f);
    fm_init(f, 44100.0f);
    fm_set_params(f, &p);
    fm_note_on(f, 55, 100);
    fm_note_on(f, 62, 100);
    fm_note_on(f, 67, 100);
    int k = 0;
    for (int i = 0; i < total; i += 128, k++) {
        fm_params q = p;
        const float v = (toggle && (k & 1)) ? hi : lo;
        float *tgt = knob == 0 ? &q.ratio : knob == 1 ? &q.index : knob == 2 ? &q.feedback : knob == 3 ? &q.layer : &q.level;
        *tgt = v;
        fm_set_params(f, &q);
        fm_render(f, l + i, r + i, total - i < 128 ? total - i : 128);
    }
    free(f);
    return max_step(l, 20000, total);
}

static void test_param_sweep_no_zipper(void)
{
    enum { N = 88200 };
    static float l[N], r[N];
    const char *names[5] = {"ratio", "index", "feedback", "layer", "level"};
    const float lo[5] = {0.1f, 0.1f, 0.0f, 0.0f, 0.2f};
    const float hi[5] = {0.9f, 0.8f, 0.9f, 1.0f, 1.0f};
    /* Jumping a control by its whole range every 128 samples changes the sound a great deal, so the bar is the
     * largest step either static setting makes by itself. A hard jump in the signal would be far above that. */
    for (int k = 0; k < 5; k++) {
        const float a = sweep_run(k, lo[k], hi[k], 0, l, r, N);
        const float b = sweep_run(k, hi[k], hi[k], 0, l, r, N);
        const float swept = sweep_run(k, lo[k], hi[k], 1, l, r, N);
        const float bar = fmaxf(a, b);
        printf("  %-8s jumping %.1f <-> %.1f every 128 samples: max step %.4f (static %.4f and %.4f)\n", names[k],
               (double)lo[k], (double)hi[k], (double)swept, (double)a, (double)b);
        CHECK(swept < bar * 1.3f + 0.005f, "%s: sweep steps %.4f against a static bar of %.4f", names[k], (double)swept, (double)bar);
        CHECK(all_finite(l, N), "NaN in %s sweep", names[k]);
    }
}

static void test_level_and_stereo(void)
{
    enum { N = 22050 };
    static float l[N], r[N];
    fm_t *f = malloc(sizeof *f);
    fm_init(f, 44100.0f);
    fm_params p;
    fm_default_params(&p);
    p.level = 0.0f;
    fm_set_params(f, &p);
    fm_reset(f);
    event ev[] = {{0, EV_ON, 60, 127}};
    run_script(f, ev, 1, l, r, N, 128);
    CHECK(peak(l, N) == 0.0f && peak(r, N) == 0.0f, "level 0 must be silent");

    p.level = 1.0f;
    fm_init(f, 44100.0f);
    fm_set_params(f, &p);
    fm_reset(f);
    event ev2[] = {{0, EV_ON, 60, 100}, {0, EV_ON, 67, 100}, {0, EV_ON, 72, 100}};
    run_script(f, ev2, 3, l, r, N, 128);
    /* mono compatibility: the sum of the channels must not cancel */
    static float m[N], s[N];
    for (int i = 0; i < N; i++) { m[i] = l[i] + r[i]; s[i] = l[i] - r[i]; }
    const double rm = rms(m + 4000, 8000), rs = rms(s + 4000, 8000), rl = rms(l + 4000, 8000);
    CHECK(rm > rl * 1.2, "mono sum should add up (sum %.4f, left %.4f)", rm, rl);
    CHECK(rs > 0.0 && rs < rm, "stereo difference should be present but smaller than the sum (%.4f vs %.4f)", rs, rm);
    printf("  stereo: mid %.4f, side %.4f (side/mid %.2f)\n", rm, rs, rs / rm);
    free(f);
}

static void test_activity(void)
{
    enum { N = 8192 };
    static float l[N], r[N];
    fm_t *f = malloc(sizeof *f);
    fm_init(f, 44100.0f);
    fm_activity a;
    fm_take_activity(f, &a);
    CHECK(a.peak == 0.0f && a.voices == 0, "idle activity");
    const unsigned n0 = a.notes;
    fm_note_on(f, 60, 100);
    fm_render(f, l, r, N);
    fm_take_activity(f, &a);
    CHECK(a.peak > 0.01f && a.voices == 1 && a.notes != n0, "activity after a note: peak %.4f voices %d", (double)a.peak, a.voices);
    fm_take_activity(f, &a);
    CHECK(a.peak == 0.0f, "peak clears after it is taken");
    free(f);
}

#ifdef FM_TIMING_ONLY
static void bench(void)
{
    enum { B = 128, BLOCKS = 20000 };
    static float l[B], r[B];
    fm_t *f = malloc(sizeof *f);
    fm_init(f, 44100.0f);
    fm_params p;
    fm_default_params(&p);
    p.sustain = 1.0f;
    fm_set_params(f, &p);
    for (int v = 1; v <= 8; v *= 2) {
        fm_init(f, 44100.0f);
        fm_set_params(f, &p);
        for (int i = 0; i < v; i++)
            fm_note_on(f, 48 + i * 4, 100);
        fm_render(f, l, r, B);
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int b = 0; b < BLOCKS; b++)
            fm_render(f, l, r, B);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        const double us = ((double)(t1.tv_sec - t0.tv_sec) * 1e6 + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-3) / BLOCKS;
        printf("  %d voices: %.2f us per 128-frame block on this host (%.2f%% of the 2902 us budget)\n", v, us, us / 29.02);
    }
    free(f);
}
#endif

int main(void)
{
#ifdef FM_TIMING_ONLY
    printf("Fm core timing (host CPU, -O2, not the MPC):\n");
    bench();
    return 0;
#else
    printf("Fm core tests\n");
    test_factory_defaults();
    test_mappings();
    test_silence_when_idle();
    test_pitch_pure();
    test_pitch_fm();
    test_bend();
    test_polyphony_headroom();
    test_unison_headroom();
    test_release_to_silence();
    test_pedal_and_all_off();
    test_no_click_steal_and_retrigger();
    test_attack_start_smooth();
    test_knobs_change_spectrum();
    test_wheel_velocity_extras();
    test_envelope();
    test_deterministic_and_block_size();
    test_sample_rates_and_extremes();
    test_param_sweep_no_zipper();
    test_level_and_stereo();
    test_activity();
    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("ALL PASSED\n");
    return 0;
#endif
}
