/* Behavioral tests for the DSynth core. Run with run.sh (clang, ASan + UBSan). */
#include "../dsynth_core.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PI 3.14159265358979

static int g_fail = 0, g_checks = 0;

static void check(int ok, const char *fmt, ...)
{
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    g_checks++;
    if (!ok)
        g_fail++;
    printf("%s  %s\n", ok ? "PASS" : "FAIL", buf);
}

/* ------------------------------------------------------------------------------------------ helpers */

enum { EV_ON, EV_OFF, EV_BEND, EV_WHEEL, EV_PEDAL, EV_ALLOFF };
typedef struct {
    int at, type, a, b;
} event;

static void apply(dsynth_t *d, const event *e)
{
    switch (e->type) {
    case EV_ON: dsynth_note_on(d, e->a, e->b); break;
    case EV_OFF: dsynth_note_off(d, e->a); break;
    case EV_BEND: dsynth_pitch_bend(d, (float)e->a / 1000.0f); break;
    case EV_WHEEL: dsynth_mod_wheel(d, (float)e->a / 1000.0f); break;
    case EV_PEDAL: dsynth_sustain_pedal(d, e->a); break;
    case EV_ALLOFF: dsynth_all_off(d); break;
    }
}

/* Render `frames` samples with events at exact sample positions, in render calls of at most `chunk` frames. */
static void play(dsynth_t *d, const event *ev, int nev, float *l, float *r, int frames, int chunk)
{
    int pos = 0, e = 0;
    while (pos < frames) {
        while (e < nev && ev[e].at <= pos)
            apply(d, &ev[e++]);
        int n = frames - pos;
        if (n > chunk)
            n = chunk;
        if (e < nev && ev[e].at - pos < n)
            n = ev[e].at - pos;
        if (n < 1)
            n = 1;
        dsynth_render(d, l + pos, r + pos, n);
        pos += n;
    }
}

static float *buf(int n)
{
    float *p = (float *)calloc((size_t)n + 8, sizeof(float));
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
    return p;
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
    float p = 0.0f;
    for (int i = 0; i < n; i++)
        if (fabsf(x[i]) > p)
            p = fabsf(x[i]);
    return p;
}

static float max_step(const float *x, int a, int b)
{
    float m = 0.0f;
    for (int i = a + 1; i < b; i++) {
        const float s = fabsf(x[i] - x[i - 1]);
        if (s > m)
            m = s;
    }
    return m;
}

static double rms_of(const float *x, int a, int b)
{
    double s = 0.0;
    for (int i = a; i < b; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (double)(b - a));
}

/* Fundamental by rising zero crossings with linear interpolation, over [a,b). */
static double zc_freq(const float *x, int a, int b, double sr)
{
    double first = -1.0, last = -1.0;
    int count = 0;
    for (int i = a + 1; i < b; i++) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double t = (double)(i - 1) + (double)(-x[i - 1]) / (double)(x[i] - x[i - 1]);
            if (first < 0.0)
                first = t;
            last = t;
            count++;
        }
    }
    return count < 2 ? 0.0 : (double)(count - 1) * sr / (last - first);
}

static double cents(double f, double ref)
{
    return 1200.0 * log2(f / ref);
}

static double note_hz(int note)
{
    return 440.0 * pow(2.0, (note - 69) / 12.0);
}

/* A small complex FFT for spectrum measurements. */
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
        const double ang = -2.0 * PI / len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < len / 2; k++) {
                const double wr = cos(ang * k), wi = sin(ang * k);
                const double ur = re[i + k], ui = im[i + k];
                const double vr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
                const double vi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
                re[i + k] = ur + vr; im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
            }
    }
}

/* Power spectrum of FFT_N samples starting at `start`, Hann windowed. */
static void spectrum(const float *x, int start, double *pow_out)
{
    static double re[FFT_N], im[FFT_N];
    for (int i = 0; i < FFT_N; i++) {
        const double w = 0.5 - 0.5 * cos(2.0 * PI * i / FFT_N);
        re[i] = (double)x[start + i] * w;
        im[i] = 0.0;
    }
    fft(re, im, FFT_N);
    for (int i = 0; i < FFT_N / 2; i++)
        pow_out[i] = re[i] * re[i] + im[i] * im[i];
}

static double band_energy(const double *p, double lo_hz, double hi_hz, double sr)
{
    int a = (int)(lo_hz * FFT_N / sr), b = (int)(hi_hz * FFT_N / sr);
    if (a < 1)
        a = 1;
    if (b > FFT_N / 2)
        b = FFT_N / 2;
    double s = 0.0;
    for (int i = a; i < b; i++)
        s += p[i];
    return s;
}

static double centroid(const double *p, double sr)
{
    double num = 0.0, den = 0.0;
    for (int i = 1; i < FFT_N / 2; i++) {
        num += p[i] * (double)i * sr / FFT_N;
        den += p[i];
    }
    return den > 0.0 ? num / den : 0.0;
}

/* Parameters for a plain, long, unfiltered sine: the measuring setup most tests start from. */
static dsynth_params plain(void)
{
    dsynth_params p;
    dsynth_default_params(&p);
    p.mod = 0.0f;
    p.wave = 0.0f;
    p.decay = 0.5f;          /* 2 s decay */
    p.filter_mode = 0;
    p.osc2_decay = 0.0f;     /* pitch drop gone within a few tens of ms */
    p.level = 1.0f;
    return p;
}

static void fresh(dsynth_t *d, float sr, const dsynth_params *p)
{
    dsynth_init(d, sr);
    dsynth_set_params(d, p);
    dsynth_reset(d);
}

/* One note, left channel returned (caller frees). */
static float *one_note(const dsynth_params *p, float sr, int note, int vel, int frames, float **right)
{
    static dsynth_t d;
    fresh(&d, sr, p);
    float *l = buf(frames), *r = buf(frames);
    event ev[1] = {{0, EV_ON, note, vel}};
    play(&d, ev, 1, l, r, frames, 128);
    if (right)
        *right = r;
    else
        free(r);
    return l;
}

/* ------------------------------------------------------------------------------------------ tests */

__attribute__((unused)) static void test_mappings(void)
{
    printf("-- mappings and defaults\n");
    check(fabsf(dsynth_decay_seconds(0.0f) - 0.015f) < 1e-6f && fabsf(dsynth_decay_seconds(0.5f) - 2.0f) < 1e-4f &&
              fabsf(dsynth_decay_seconds(1.0f) - 2.0f) < 1e-4f,
          "decay knob: 0 -> %.3f s, 0.5 -> %.3f s, 1 -> %.3f s", dsynth_decay_seconds(0.0f), dsynth_decay_seconds(0.5f),
          dsynth_decay_seconds(1.0f));
    check(fabsf(dsynth_attack_seconds(0.5f) - 0.001f) < 1e-6f && fabsf(dsynth_attack_seconds(1.0f) - 0.5f) < 1e-3f,
          "attack: 0.5 -> %.4f s, 1 -> %.3f s", dsynth_attack_seconds(0.5f), dsynth_attack_seconds(1.0f));
    int mono = 1;
    float last = -1.0f;
    for (int i = 0; i <= 100; i++) {
        const float v = dsynth_decay_seconds(i / 100.0f);
        mono &= v >= last;
        last = v;
    }
    check(mono, "decay seconds never fall as the knob rises");
    check(fabsf(dsynth_mod_depth(1.0f) - 9.0f) < 1e-5f && dsynth_mod_depth(0.0f) == 0.0f, "mod depth spans 0 to 9 rad");
    check(fabsf(dsynth_osc2_ratio(0.0f) - 1.0f) < 1e-6f && fabsf(dsynth_osc2_ratio(1.0f) - 8.0f) < 1e-4f,
          "osc 2 ratio spans 1 to 8");
    check(fabsf(dsynth_cutoff_ratio(0.0f) - 2.0f) < 1e-6f && fabsf(dsynth_cutoff_ratio(1.0f) - 64.0f) < 1e-3f,
          "cutoff ratio spans 2 to 64");
    const float raw[8] = {DSYNTH_DEFAULT_MOD, DSYNTH_DEFAULT_WAVE, DSYNTH_DEFAULT_DECAY, 0.19f,
                          DSYNTH_DEFAULT_OSC2_PITCH, DSYNTH_DEFAULT_OSC2_WAVE, DSYNTH_DEFAULT_OSC2_DECAY, DSYNTH_DEFAULT_CUTOFF};
    const int want[8] = {20664, 0, 8528, 6225, 328, 0, 12130, 11465};
    int ok = 1;
    for (int i = 0; i < 8; i++)
        ok &= fabsf(raw[i] * 32767.0f - (float)want[i]) < 6.0f;
    check(ok, "defaults match the factory values 20664 0 8528 6225 328 0 12130 11465 of 32767 (within rounding)");
    check((int)lroundf(6225.0f / 32767.0f * 7.0f) == DSYNTH_DEFAULT_FILTER_MODE,
          "factory filter value 6225/32767 on the 0..7 dial rounds to default mode %d", DSYNTH_DEFAULT_FILTER_MODE);
}

__attribute__((unused)) static void test_pitch(void)
{
    printf("-- pitch accuracy\n");
    const float srs[] = {22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f};
    const int notes[] = {24, 36, 48, 60, 69, 84, 96};
    double worst = 0.0;
    int n_meas = 0;
    for (size_t s = 0; s < sizeof srs / sizeof srs[0]; s++)
        for (size_t k = 0; k < sizeof notes / sizeof notes[0]; k++) {
            const double f = note_hz(notes[k]);
            if (f > 0.4 * srs[s])
                continue;
            dsynth_params p = plain();
            const int frames = (int)(1.2f * srs[s]);
            float *l = one_note(&p, srs[s], notes[k], 100, frames, NULL);
            const double m = zc_freq(l, (int)(0.3f * srs[s]), (int)(1.1f * srs[s]), srs[s]);
            const double c = fabs(cents(m, f));
            if (c > worst)
                worst = c;
            n_meas++;
            if (c > 5.0)
                check(0, "note %d at %.0f Hz: measured %.3f Hz, %.2f cents off", notes[k], srs[s], m, c);
            free(l);
        }
    check(worst <= 5.0, "%d notes x sample rates 22050..192000: worst error %.3f cents (limit 5)", n_meas, worst);

    dsynth_params p = plain();
    static dsynth_t d;
    fresh(&d, 44100.0f, &p);
    float *l = buf(44100), *r = buf(44100);
    event ev[2] = {{0, EV_ON, 60, 100}, {1000, EV_BEND, 1000, 0}};
    play(&d, ev, 2, l, r, 44100, 128);
    const double fb = zc_freq(l, 20000, 40000, 44100.0);
    const double want = note_hz(60) * pow(2.0, 2.0 / 12.0);
    check(fabs(cents(fb, want)) < 5.0, "full pitch bend up at the default range: %.2f Hz vs %.2f Hz, %.2f cents off", fb, want,
          cents(fb, want));
    free(l);
    free(r);
}

__attribute__((unused)) static void test_release_and_pedal(void)
{
    printf("-- note off, pedal, natural decay\n");
    dsynth_params p = plain();
    p.amp_release = 0.1f;
    static dsynth_t d;
    fresh(&d, 44100.0f, &p);
    const int N = 44100 * 2;
    float *l = buf(N), *r = buf(N);
    event ev[2] = {{0, EV_ON, 60, 100}, {13230, EV_OFF, 60, 0}};
    play(&d, ev, 2, l, r, N, 128);
    const double before = rms_of(l, 13230 - 2000, 13230);
    const double after2 = rms_of(l, 13230 + 8820, 13230 + 8820 + 441); /* 0.2 s after */
    check(after2 < before * 0.002, "0.1 s release: level 0.2 s after note off is at least %.1f dB below the held level",
          fmin(120.0, 20.0 * log10(before / (after2 + 1e-30))));
    float tail = 0.0f;
    for (int i = 13230 + 22050; i < N; i++)
        tail = fmaxf(tail, fabsf(l[i]));
    check(tail == 0.0f && dsynth_active_voices(&d) == 0, "after 0.5 s the voice is freed and the output is exactly zero (max %g)",
          (double)tail);

    /* pedal holds the note */
    fresh(&d, 44100.0f, &p);
    memset(l, 0, sizeof(float) * (size_t)N);
    event ev2[4] = {{0, EV_PEDAL, 1, 0}, {1000, EV_ON, 60, 100}, {13230, EV_OFF, 60, 0}, {44100, EV_PEDAL, 0, 0}};
    play(&d, ev2, 4, l, r, N, 128);
    const double held_b = rms_of(l, 13230 - 2000, 13230);
    const double held_a = rms_of(l, 13230 + 8820, 13230 + 8820 + 441);
    /* the 2 s oscillator decay alone takes 0.2 s * 60 dB / 2 s = 6 dB off, so about -6 dB means no release happened */
    check(20.0 * log10(held_a / held_b) > -9.0 && 20.0 * log10(held_a / held_b) < -3.0,
          "pedal down: level 0.2 s after note off is %.1f dB against the held level (6 dB is the oscillator's own decay)",
          20.0 * log10(held_a / held_b));
    const double rel = rms_of(l, 44100 + 8820, 44100 + 8820 + 441);
    check(rel < held_b * 0.002, "pedal up releases it: at least %.1f dB below the held level 0.2 s later", fmin(120.0, 20.0 * log10(held_b / (rel + 1e-30))));

    /* a short decay ends the voice with no note off at all */
    p = plain();
    p.decay = 0.0f;
    fresh(&d, 44100.0f, &p);
    event ev3[1] = {{0, EV_ON, 60, 127}};
    play(&d, ev3, 1, l, r, N, 128);
    check(dsynth_active_voices(&d) == 0 && peak_of(l + 44100, 44100) == 0.0f,
          "a 15 ms decay frees the voice by itself (voices %d, output after 1 s exactly zero)", dsynth_active_voices(&d));

    /* all notes off */
    p = plain();
    p.amp_release = 0.05f;
    fresh(&d, 44100.0f, &p);
    event ev4[4] = {{0, EV_ON, 50, 100}, {0, EV_ON, 57, 100}, {0, EV_ON, 64, 100}, {4410, EV_ALLOFF, 0, 0}};
    play(&d, ev4, 4, l, r, N, 128);
    check(dsynth_active_voices(&d) == 0 && peak_of(l + 4410 + 22050, 22050) == 0.0f, "all-off releases three held notes to silence");
    free(l);
    free(r);
}

__attribute__((unused)) static void test_polyphony_and_headroom(void)
{
    printf("-- polyphony, headroom\n");
    dsynth_params p;
    dsynth_default_params(&p);
    p.level = 1.0f;
    p.decay = 0.5f;
    static dsynth_t d;
    const int N = 44100;
    float *l = buf(N), *r = buf(N);
    fresh(&d, 44100.0f, &p);
    event ev[11];
    int ne = 0;
    for (int i = 0; i < 8; i++)
        ev[ne++] = (event){0, EV_ON, 48 + 3 * i, 127};
    ev[ne++] = (event){8000, EV_ON, 40, 127};
    ev[ne++] = (event){12000, EV_ON, 41, 127};
    ev[ne++] = (event){16000, EV_ON, 100, 127};
    play(&d, ev, ne, l, r, N, 128);
    check(all_finite(l, N) && all_finite(r, N), "8 notes plus three steals: no NaN or inf");
    check(dsynth_active_voices(&d) <= DSYNTH_VOICES, "voice count stays at or below %d (now %d)", DSYNTH_VOICES, dsynth_active_voices(&d));
    const float pk = fmaxf(peak_of(l, N), peak_of(r, N));
    check(pk < 1.0f && pk <= 0.98f + 1e-6f, "8 voices at velocity 127 and level 1.0: peak %.4f (under 0 dBFS)", (double)pk);
    check(pk > 0.3f, "and it is not silent (peak %.3f)", (double)pk);

    /* headroom across the filter modes and the loudest settings */
    float worst = 0.0f;
    int cases = 0;
    for (int mode = 0; mode < DSYNTH_FILTER_MODES; mode++)
        for (int mi = 0; mi < 2; mi++)
            for (int wi = 0; wi < 3; wi++) {
                dsynth_params q;
                dsynth_default_params(&q);
                q.level = 1.0f;
                q.decay = 0.5f;
                q.filter_mode = mode;
                q.mod = (float)mi;
                q.wave = 0.5f * (float)wi;
                q.cutoff = 0.1f * (float)(mode % 4);
                fresh(&d, 44100.0f, &q);
                event e2[8];
                for (int i = 0; i < 8; i++)
                    e2[i] = (event){0, EV_ON, 36 + 5 * i, 127};
                play(&d, e2, 8, l, r, N, 128);
                const float m = fmaxf(peak_of(l, N), peak_of(r, N));
                worst = fmaxf(worst, m);
                cases++;
                if (!all_finite(l, N) || m > 0.98f + 1e-6f)
                    check(0, "case mode %d mod %d wave %d: peak %.4f", mode, mi, wi, (double)m);
            }
    check(worst <= 0.98f + 1e-6f, "%d settings (8 filter modes x mod 0/1 x wave 0/0.5/1), 8 voices, velocity 127, level 1: worst peak %.4f",
          cases, (double)worst);

    /* a single default note */
    dsynth_default_params(&p);
    float *one = one_note(&p, 44100.0f, 48, 127, 44100, NULL);
    check(peak_of(one, 44100) < 0.7f && peak_of(one, 44100) > 0.05f, "one default note at velocity 127: peak %.3f (headroom for chords)",
          (double)peak_of(one, 44100));
    free(one);
    free(l);
    free(r);
}

__attribute__((unused)) static void test_clicks(void)
{
    printf("-- retrigger and steal without clicks\n");
    dsynth_params p = plain();
    static dsynth_t d;
    const int N = 44100;
    float *l = buf(N), *r = buf(N);
    fresh(&d, 44100.0f, &p);
    event ev[2] = {{0, EV_ON, 60, 127}, {13333, EV_ON, 60, 127}};
    play(&d, ev, 2, l, r, N, 128);
    const float ref = max_step(l, 5000, 13000);
    const float evs = max_step(l, 13333 - 4, 13333 + 2205);
    check(evs < 0.05f && evs < 2.5f * ref,
          "same key struck again: largest sample step %.4f across the event, steady-state %.4f, limit 0.05 (a hard cut would step by the instantaneous level, up to ~0.5)",
          (double)evs, (double)ref);

    fresh(&d, 44100.0f, &p);
    p.level = 0.6f;
    dsynth_set_params(&d, &p);
    event e2[10];
    int ne = 0;
    for (int i = 0; i < 8; i++)
        e2[ne++] = (event){0, EV_ON, 36 + 4 * i, 127};
    e2[ne++] = (event){15000, EV_ON, 30, 127};
    e2[ne++] = (event){15001, EV_ON, 31, 127}; /* two steals back to back */
    play(&d, e2, ne, l, r, N, 128);
    const float ref2 = max_step(l, 8000, 14900);
    const float evs2 = max_step(l, 14990, 14990 + 2205);
    check(evs2 < 0.05f && evs2 < 2.5f * ref2, "9th and 10th note steal voices: largest step %.4f, chord steady-state %.4f, limit 0.05",
          (double)evs2, (double)ref2);

    /* a note released before its steal finished starts in release, not stuck */
    fresh(&d, 44100.0f, &p);
    event e3[10];
    for (int i = 0; i < 8; i++)
        e3[i] = (event){0, EV_ON, 36 + 4 * i, 127};
    e3[8] = (event){15000, EV_ON, 90, 127};
    e3[9] = (event){15010, EV_OFF, 90, 0};
    play(&d, e3, 10, l, r, 15500, 128);
    int stuck = 0;
    for (int i = 0; i < DSYNTH_VOICES; i++)
        if (d.voice[i].alive && d.voice[i].note == 90 && (d.voice[i].held || d.voice[i].sustained))
            stuck = 1;
    check(!stuck, "a key released inside the 2 ms steal fade starts released (it never sounded, so it is freed) and does not stick");
    free(l);
    free(r);
}

__attribute__((unused)) static void test_split_invariance(void)
{
    printf("-- determinism and block-size independence\n");
    dsynth_params p;
    dsynth_default_params(&p);
    p.wave = 0.3f;
    p.filter_mode = 2;
    p.amp_decay = 0.3f;
    p.amp_sustain = 0.4f;
    const int N = 44100;
    event ev[] = {{0, EV_ON, 48, 100}, {300, EV_ON, 55, 90}, {701, EV_ON, 60, 127}, {5000, EV_BEND, 700, 0},
                  {9000, EV_OFF, 48, 0}, {12000, EV_WHEEL, 800, 0}, {15000, EV_ON, 48, 70}, {15003, EV_ON, 52, 70},
                  {20000, EV_PEDAL, 1, 0}, {21000, EV_OFF, 60, 0}, {30000, EV_PEDAL, 0, 0}};
    const int nev = (int)(sizeof ev / sizeof ev[0]);
    static dsynth_t a, b;
    float *la = buf(N), *ra = buf(N), *lb = buf(N), *rb = buf(N);
    fresh(&a, 44100.0f, &p);
    play(&a, ev, nev, la, ra, N, 128);
    fresh(&b, 44100.0f, &p);
    play(&b, ev, nev, lb, rb, N, 128);
    check(memcmp(la, lb, sizeof(float) * (size_t)N) == 0 && memcmp(ra, rb, sizeof(float) * (size_t)N) == 0,
          "two instances given the same notes produce bit-identical output");
    const int chunks[] = {1, 3, 15, 16, 17, 64, 127, 333, 4096};
    int same = 1;
    for (size_t c = 0; c < sizeof chunks / sizeof chunks[0]; c++) {
        fresh(&b, 44100.0f, &p);
        play(&b, ev, nev, lb, rb, N, chunks[c]);
        const int ok = memcmp(la, lb, sizeof(float) * (size_t)N) == 0 && memcmp(ra, rb, sizeof(float) * (size_t)N) == 0;
        if (!ok)
            printf("     chunk %d differs\n", chunks[c]);
        same &= ok;
    }
    check(same, "output is bit-identical for render calls of 1, 3, 15, 16, 17, 64, 127, 333 and 4096 frames");

    /* reset returns to a clean state */
    fresh(&b, 44100.0f, &p);
    play(&b, ev, nev, lb, rb, 20000, 128);
    dsynth_reset(&b);
    float *l1 = buf(N), *r1 = buf(N);
    dsynth_render(&b, l1, r1, 2048);
    check(peak_of(l1, 2048) == 0.0f && dsynth_active_voices(&b) == 0, "reset: silence and no voices");
    memset(l1, 0, sizeof(float) * (size_t)N);
    play(&b, ev, nev, l1, r1, N, 128);
    check(memcmp(la, l1, sizeof(float) * (size_t)N) == 0, "after a reset the same notes give the same output as a fresh instance (all state cleared)");
    free(la); free(ra); free(lb); free(rb); free(l1); free(r1);
}

__attribute__((unused)) static void test_knobs(void)
{
    printf("-- each knob changes the sound\n");
    const float sr = 44100.0f;
    const int note = 48;
    const double f0 = note_hz(note);
    static double pa[FFT_N / 2], pb[FFT_N / 2];

    /* MOD */
    dsynth_params p = plain();
    p.osc2_decay = 0.5f; /* modulation stays up for the measurement */
    p.mod = 0.0f;
    float *a = one_note(&p, sr, note, 100, 20000, NULL);
    p.mod = 1.0f;
    float *b = one_note(&p, sr, note, 100, 20000, NULL);
    spectrum(a, 2000, pa);
    spectrum(b, 2000, pb);
    const double ca = centroid(pa, sr), cb = centroid(pb, sr);
    check(cb > 3.0 * ca, "MOD 0 -> 1: spectral centroid %.0f Hz -> %.0f Hz", ca, cb);
    free(a); free(b);

    /* mod wheel does the same on top of a low MOD */
    p.mod = 0.0f;
    static dsynth_t d;
    fresh(&d, sr, &p);
    float *l = buf(20000), *r = buf(20000);
    event ev[2] = {{0, EV_WHEEL, 1000, 0}, {0, EV_ON, note, 100}};
    play(&d, ev, 2, l, r, 20000, 128);
    spectrum(l, 2000, pb);
    check(centroid(pb, sr) > 3.0 * ca, "mod wheel at full on MOD 0: centroid %.0f Hz (was %.0f Hz)", centroid(pb, sr), ca);
    free(l); free(r);

    /* WAVE: sine, square, saw */
    p = plain();
    double h_even[3], h_odd[3];
    const float waves[3] = {0.0f, 0.5f, 1.0f};
    for (int i = 0; i < 3; i++) {
        p.wave = waves[i];
        a = one_note(&p, sr, note, 100, 20000, NULL);
        spectrum(a, 2000, pa);
        const double fund = band_energy(pa, f0 * 0.9, f0 * 1.1, sr);
        h_even[i] = band_energy(pa, f0 * 1.9, f0 * 2.1, sr) / fund;
        h_odd[i] = band_energy(pa, f0 * 2.9, f0 * 3.1, sr) / fund;
        free(a);
    }
    check(h_odd[0] < 1e-6 && h_even[0] < 1e-6, "WAVE 0 is a sine: 2nd harmonic %.1e, 3rd %.1e of the fundamental (power)", h_even[0], h_odd[0]);
    check(h_odd[1] > 0.05 && h_even[1] < 0.01 * h_odd[1], "WAVE 0.5 is a square: 3rd harmonic %.3f, 2nd %.1e (odd harmonics only)", h_odd[1], h_even[1]);
    check(h_even[2] > 0.1 && h_odd[2] > 0.05, "WAVE 1 is a saw: 2nd harmonic %.3f, 3rd %.3f", h_even[2], h_odd[2]);

    /* DECAY: time to fall 30 dB, measured on a high note so 2 ms windows hold several periods */
    p = plain();
    p.level = 1.0f;
    double t30[2];
    const float dk[2] = {0.1f, 0.4f};
    for (int i = 0; i < 2; i++) {
        p.decay = dk[i];
        a = one_note(&p, sr, 84, 100, 44100 * 4, NULL);
        double pk = 0.0;
        for (int w = 0; w < 44100 * 4 - 88; w += 88)
            pk = fmax(pk, rms_of(a, w, w + 88));
        t30[i] = -1.0;
        for (int w = 0; w < 44100 * 4 - 88; w += 88)
            if (rms_of(a, w, w + 88) < pk * 0.0316 && t30[i] < 0.0)
                t30[i] = (double)w / sr;
        free(a);
    }
    const double e0 = dsynth_decay_seconds(0.1f) * 0.5, e1 = dsynth_decay_seconds(0.4f) * 0.5;
    check(t30[0] > 0.0 && t30[1] > 6.0 * t30[0] && fabs(t30[0] / e0 - 1.0) < 0.25 && fabs(t30[1] / e1 - 1.0) < 0.25,
          "DECAY 0.1 -> 0.4: time to -30 dB %.3f s -> %.3f s (expected about %.3f and %.3f)", t30[0], t30[1], e0, e1);
    double rise[2];
    const float ak[2] = {0.3f, 0.9f};
    for (int i = 0; i < 2; i++) {
        p.decay = ak[i];
        a = one_note(&p, sr, note, 100, 44100, NULL);
        double pk = 0.0;
        for (int w = 0; w < 44100 - 221; w += 221)
            pk = fmax(pk, rms_of(a, w, w + 221));
        rise[i] = -1.0;
        for (int w = 0; w < 44100 - 221; w += 221)
            if (rms_of(a, w, w + 221) > pk * 0.5 && rise[i] < 0.0)
                rise[i] = (double)w / sr;
        free(a);
    }
    check(rise[0] < 0.01 && rise[1] > 0.05, "DECAY 0.3 -> 0.9: time to reach half level %.3f s -> %.3f s (the upper half of the knob is attack)", rise[0], rise[1]);

    /* FILTER: eight distinct positions on a saw */
    p = plain();
    p.wave = 1.0f;
    static float *out[DSYNTH_FILTER_MODES];
    static double hi[DSYNTH_FILTER_MODES], lo[DSYNTH_FILTER_MODES], tot[DSYNTH_FILTER_MODES];
    for (int m = 0; m < DSYNTH_FILTER_MODES; m++) {
        p.filter_mode = m;
        out[m] = one_note(&p, sr, note, 100, 20000, NULL);
        spectrum(out[m], 4000, pa);
        hi[m] = band_energy(pa, 3000.0, 12000.0, sr);
        lo[m] = band_energy(pa, 20.0, 300.0, sr);
        tot[m] = band_energy(pa, 20.0, 20000.0, sr);
    }
    check(hi[1] < 0.2 * hi[0] && hi[2] < hi[1], "FILTER 1 LP12 and 2 LP24 cut the highs: band above 3 kHz %.1f dB and %.1f dB under the unfiltered saw",
          10.0 * log10(hi[0] / hi[1]), 10.0 * log10(hi[0] / hi[2]));
    check(lo[5] < 0.1 * lo[0], "FILTER 5 HP12 cuts the lows: band under 300 Hz %.1f dB down", 10.0 * log10(lo[0] / lo[5]));
    check(tot[4] < tot[3], "FILTER 4 narrow band passes less than 3 band (%.1f dB less)", 10.0 * log10(tot[3] / tot[4]));
    spectrum(out[0], 4000, pa);
    int distinct = 1;
    for (int i = 0; i < DSYNTH_FILTER_MODES; i++)
        for (int j = i + 1; j < DSYNTH_FILTER_MODES; j++) {
            double dd = 0.0, ee = 0.0;
            for (int s = 4000; s < 12000; s++) {
                dd += ((double)out[i][s] - out[j][s]) * ((double)out[i][s] - out[j][s]);
                ee += (double)out[i][s] * out[i][s];
            }
            if (dd < 0.01 * ee) {
                printf("     modes %d and %d are within 20 dB of identical\n", i, j);
                distinct = 0;
            }
        }
    check(distinct, "all 8 filter positions give clearly different waveforms (pairwise difference above -20 dB)");
    for (int m = 0; m < DSYNTH_FILTER_MODES; m++)
        free(out[m]);

    /* extras that the engine reads: osc 2 pitch, osc 2 wave, cutoff */
    p = plain();
    p.mod = 0.7f;
    p.osc2_decay = 0.5f;
    p.osc2_pitch = 0.0f;
    a = one_note(&p, sr, note, 100, 20000, NULL);
    p.osc2_pitch = 0.5f;
    b = one_note(&p, sr, note, 100, 20000, NULL);
    check(memcmp(a, b, sizeof(float) * 20000) != 0 && fabs(rms_of(a, 4000, 12000) - rms_of(b, 4000, 12000)) >= 0.0,
          "osc 2 pitch changes the modulation sidebands");
    spectrum(a, 2000, pa);
    spectrum(b, 2000, pb);
    check(fabs(centroid(pa, sr) - centroid(pb, sr)) > 20.0, "osc 2 pitch 0 -> 0.5: centroid %.0f Hz -> %.0f Hz", centroid(pa, sr), centroid(pb, sr));
    free(a); free(b);
    p = plain();
    p.filter_mode = 1;
    p.wave = 1.0f;
    p.cutoff = 0.1f;
    a = one_note(&p, sr, note, 100, 20000, NULL);
    p.cutoff = 0.8f;
    b = one_note(&p, sr, note, 100, 20000, NULL);
    spectrum(a, 2000, pa);
    spectrum(b, 2000, pb);
    const double ha = band_energy(pa, 1000.0, 12000.0, sr), hb = band_energy(pb, 1000.0, 12000.0, sr);
    check(hb > 4.0 * ha, "cutoff 0.1 -> 0.8 on LP12: energy above 1 kHz rises %.1f dB", 10.0 * log10(hb / ha));
    free(a); free(b);
}

__attribute__((unused)) static void test_velocity_pitch_drop(void)
{
    printf("-- velocity and the pitch drop\n");
    dsynth_params p = plain();
    float *a = one_note(&p, 44100.0f, 60, 30, 20000, NULL);
    float *b = one_note(&p, 44100.0f, 60, 120, 20000, NULL);
    const double ra = rms_of(a, 2000, 10000), rb = rms_of(b, 2000, 10000);
    check(rb > 3.0 * ra, "velocity 30 vs 120: level %.1f dB apart", 20.0 * log10(rb / ra));
    free(a); free(b);

    p = plain();
    p.osc2_decay = 0.45f; /* long enough that the drop is audible in the first 100 ms */
    float *c = one_note(&p, 44100.0f, 48, 100, 44100, NULL);
    const double early = zc_freq(c, 300, 1500, 44100.0);
    const double late = zc_freq(c, 30000, 44000, 44100.0);
    check(early > 1.2 * late && fabs(cents(late, note_hz(48))) < 5.0,
          "pitch drop: %.1f Hz in the first 30 ms, settling to %.2f Hz (note is %.2f Hz)", early, late, note_hz(48));
    free(c);
}

__attribute__((unused)) static void test_robustness(void)
{
    printf("-- sample rates, block size 1, long run\n");
    const float srs[] = {22050.0f, 32000.0f, 44100.0f, 48000.0f, 88200.0f, 96000.0f, 192000.0f};
    int ok = 1;
    int exact = 1;
    for (size_t s = 0; s < sizeof srs / sizeof srs[0]; s++) {
        dsynth_params p;
        dsynth_default_params(&p);
        p.wave = 0.6f;
        p.filter_mode = (int)(s % 8);
        p.level = 1.0f;
        const int N = (int)(0.4f * srs[s]);
        event ev[5] = {{0, EV_ON, 36, 127}, {10, EV_ON, 60, 127}, {20, EV_ON, 84, 127}, {30, EV_ON, 108, 127}, {N / 2, EV_OFF, 36, 0}};
        static dsynth_t d;
        float *l = buf(N), *r = buf(N), *l2 = buf(N), *r2 = buf(N);
        fresh(&d, srs[s], &p);
        play(&d, ev, 5, l, r, N, 1);
        ok &= all_finite(l, N) && all_finite(r, N) && peak_of(l, N) <= 0.98f + 1e-6f;
        fresh(&d, srs[s], &p);
        play(&d, ev, 5, l2, r2, N, 128);
        exact &= memcmp(l, l2, sizeof(float) * (size_t)N) == 0;
        free(l); free(r); free(l2); free(r2);
    }
    check(ok, "22050, 32000, 44100, 48000, 88200, 96000, 192000 Hz with block size 1: finite and bounded");
    check(exact, "block size 1 equals block size 128 at every one of those rates");

    /* long run: no NaN, nothing subnormal once the sound has ended */
    dsynth_params p;
    dsynth_default_params(&p);
    p.filter_mode = 7;
    static dsynth_t d;
    fresh(&d, 44100.0f, &p);
    const int N = 44100 * 20;
    float *l = buf(N), *r = buf(N);
    event ev[2] = {{0, EV_ON, 40, 127}, {0, EV_ON, 52, 127}};
    play(&d, ev, 2, l, r, N, 128);
    int sub = 0;
    for (int i = 0; i < N; i++)
        sub += fpclassify(l[i]) == FP_SUBNORMAL || fpclassify(r[i]) == FP_SUBNORMAL;
    check(sub == 0 && all_finite(l, N), "20 s run through the resonant filter: no subnormal samples, no NaN");
    int dead = 1;
    for (int i = 0; i < DSYNTH_VOICES; i++) {
        const dsynth_voice *v = &d.voice[i];
        dead &= !v->alive && v->f1_ic1 == 0.0f && v->f1_ic2 == 0.0f && v->f2_ic1 == 0.0f && v->f2_ic2 == 0.0f && v->e1 == 0.0f && v->e2 == 0.0f;
    }
    check(dead, "ended voices hold exactly zero in every filter and envelope state");
    free(l); free(r);

    /* parameter abuse */
    dsynth_params bad;
    memset(&bad, 0, sizeof bad);
    bad.mod = 5.0f; bad.wave = -2.0f; bad.decay = 9.0f; bad.filter_mode = 99; bad.level = 7.0f;
    bad.amp_attack = -1.0f; bad.amp_decay = 0.0f; bad.amp_release = 0.0f; bad.bend_range = 100.0f;
    fresh(&d, 44100.0f, &bad);
    l = buf(8000); r = buf(8000);
    event e2[2] = {{0, EV_ON, 60, 127}, {100, EV_BEND, -5000, 0}};
    play(&d, e2, 2, l, r, 8000, 128);
    check(all_finite(l, 8000) && peak_of(l, 8000) <= 0.98f + 1e-6f, "out-of-range parameters are clamped: output finite and bounded");
    free(l); free(r);
}

__attribute__((unused)) static void test_activity(void)
{
    printf("-- activity for the editor\n");
    dsynth_params p = plain();
    static dsynth_t d;
    fresh(&d, 44100.0f, &p);
    float *l = buf(4096), *r = buf(4096);
    dsynth_activity a;
    dsynth_take_activity(&d, &a);
    check(a.peak == 0.0f && a.voices == 0, "idle: peak 0, no voices");
    const unsigned n0 = a.notes;
    dsynth_note_on(&d, 60, 127);
    dsynth_render(&d, l, r, 2048);
    dsynth_take_activity(&d, &a);
    check(a.peak > 0.1f && a.voices == 1 && a.notes != n0, "after a note: peak %.3f, voices %d, note counter moved", (double)a.peak, a.voices);
    dsynth_take_activity(&d, &a);
    check(a.peak == 0.0f, "the peak clears when read");
    free(l); free(r);
}

__attribute__((unused)) static void bench(void)
{
    dsynth_params p;
    dsynth_default_params(&p);
    p.wave = 1.0f;
    p.filter_mode = 2;
    p.decay = 0.5f;
    p.osc2_decay = 0.5f;
    static dsynth_t d;
    fresh(&d, 44100.0f, &p);
    for (int i = 0; i < 8; i++)
        dsynth_note_on(&d, 40 + 5 * i, 100);
    float l[128], r[128];
    const int blocks = 3000;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int b = 0; b < blocks; b++)
        dsynth_render(&d, l, r, 128);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double us = ((double)(t1.tv_sec - t0.tv_sec) * 1e6 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e3) / blocks;
    printf("BENCH  8 voices, saw, LP24, mod on, %d blocks of 128 frames: %.1f us per block = %.2f%% of the 2902 us budget (host CPU, -O2; not an MPC number)\n",
           blocks, us, us / 2902.0 * 100.0);
}

int main(void)
{
#ifdef DSYNTH_TEST_BENCH_ONLY
    bench();
    return 0;
#else
    test_mappings();
    test_pitch();
    test_release_and_pedal();
    test_polyphony_and_headroom();
    test_clicks();
    test_split_invariance();
    test_knobs();
    test_velocity_pitch_drop();
    test_robustness();
    test_activity();
    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
#endif
}
