/* Behavioral tests for the DrWave core. Build and run with core/tests/run.sh */
#include "../drwave_core.h"

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

static void apply(dw_t *d, const event *e)
{
    switch (e->type) {
    case EV_ON: dw_note_on(d, e->a, (int)e->b); break;
    case EV_OFF: dw_note_off(d, e->a); break;
    case EV_BEND: dw_pitch_bend(d, e->b); break;
    case EV_PEDAL: dw_sustain_pedal(d, e->a); break;
    case EV_ALLOFF: dw_all_off(d); break;
    case EV_MOD: dw_mod_wheel(d, e->b); break;
    }
}

/* Render total frames with events at exact frames, calls never longer than block. Events must be sorted. */
static void run_script(dw_t *d, const event *ev, int nev, float *l, float *r, int total, int block)
{
    int cur = 0, ei = 0;
    while (cur < total) {
        while (ei < nev && ev[ei].frame <= cur)
            apply(d, &ev[ei++]);
        int n = total - cur;
        if (n > block)
            n = block;
        if (ei < nev && ev[ei].frame - cur < n)
            n = ev[ei].frame - cur;
        dw_render(d, l + cur, r + cur, n);
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

static dw_t *new_synth(float sr)
{
    dw_t *d = malloc(sizeof *d);
    dw_init(d, sr);
    return d;
}

/* Set the parameters and settle the smoothers on them, as the plugin does in prepareToPlay. Without the reset the
 * controls glide from the factory defaults for the first 20 ms, which is right for a live change and wrong for a
 * test that wants a known state at the first sample. */
static void setup(dw_t *d, const dw_params *p)
{
    dw_set_params(d, p);
    dw_reset(d);
}

/* A plain sine: wave 0, no blend, no motion, no fold, open tone, flat envelope. */
static dw_params pure_params(void)
{
    dw_params p;
    dw_default_params(&p);
    p.wave = 0.0f;
    p.blend = 0.0f;
    p.motion = 0.0f;
    p.fold = 0.0f;
    p.tone = 1.0f;
    p.attack = 0.001f;
    p.sustain = 1.0f;
    p.level = 1.0f;
    return p;
}

/* Wave bank position p (0..7) as the knob value. */
static float wave_knob(float position)
{
    return position / (float)(DW_WAVES - 1);
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
    const double dd = y0 - 2 * y1 + y2;
    const double off = dd != 0 ? 0.5 * (y0 - y2) / dd : 0;
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

/* Same, for a double buffer (used on the wave tables). */
static void spectrum_db_d(const float *x, double *db)
{
    static float tmp[NFFT];
    memcpy(tmp, x, sizeof tmp);
    spectrum_db(tmp, 0, db);
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

/* One held note at 44.1 kHz, returns the spectrum starting at frame `from`. */
static void note_spectrum_at(const dw_params *p, int note, int vel, int from, double *db)
{
    enum { N = 22050 };
    static float l[N], r[N];
    dw_t *d = new_synth(44100.0f);
    dw_set_params(d, p);
    dw_reset(d);
    event ev[] = {{0, EV_ON, note, (float)vel}};
    run_script(d, ev, 1, l, r, N, 128);
    spectrum_db(l, from, db);
    free(d);
}

static void note_spectrum(const dw_params *p, int note, int vel, double *db)
{
    note_spectrum_at(p, note, vel, 11025, db);
}

/* ---- tests ---- */

static void test_factory_defaults(void)
{
    /* knob raw values 24000, 16000, 8200, 24000 and extra 32000 of 32767 */
    CHECK(fabsf(DW_DEFAULT_WAVE - 24000.0f / 32767.0f) < 1e-4f, "wave default");
    CHECK(fabsf(DW_DEFAULT_BLEND - 16000.0f / 32767.0f) < 1e-4f, "blend default");
    CHECK(fabsf(DW_DEFAULT_MOTION - 8200.0f / 32767.0f) < 1e-4f, "motion default");
    CHECK(fabsf(DW_DEFAULT_FOLD - 24000.0f / 32767.0f) < 1e-4f, "fold default");
    CHECK(fabsf(DW_DEFAULT_TONE - 32000.0f / 32767.0f) < 1e-4f, "tone default");
    printf("  defaults: wave position %.2f (%s to %s), blend %.0f%%, motion %.0f%%, fold %.0f%%, tone %.0f Hz\n",
           (double)dw_wave_position(DW_DEFAULT_WAVE), dw_wave_name((int)dw_wave_position(DW_DEFAULT_WAVE)),
           dw_wave_name((int)dw_wave_position(DW_DEFAULT_WAVE) + 1), (double)DW_DEFAULT_BLEND * 100,
           (double)DW_DEFAULT_MOTION * 100, (double)DW_DEFAULT_FOLD * 100, (double)dw_tone_hz(DW_DEFAULT_TONE));
}

static void test_mappings(void)
{
    float prev = -1, prevT = -1;
    for (int i = 0; i <= 4096; i++) {
        const float n = (float)i / 4096.0f;
        const float w = dw_wave_position(n), t = dw_tone_hz(n);
        CHECK(w >= prev, "wave position not monotone at %f", (double)n);
        CHECK(t >= prevT, "tone mapping not monotone at %f", (double)n);
        prev = w;
        prevT = t;
    }
    CHECK(dw_wave_position(0.0f) == 0.0f && fabsf(dw_wave_position(1.0f) - 7.0f) < 1e-6f, "wave range");
    CHECK(fabsf(dw_tone_hz(0.0f) - 80.0f) < 1e-3f && fabsf(dw_tone_hz(1.0f) - 20000.0f) < 1.0f, "tone range");
    CHECK(fabsf(dw_level_gain(1.0f) - 1.0f) < 1e-6f && dw_level_gain(0.0f) == 0.0f, "level range");
}

static void test_wave_bank(void)
{
    dw_t *d = new_synth(44100.0f);
    static double db[NFFT / 2];
    float worst_rms_db = 0, worst_dc = 0, worst_peak = 0;
    for (int w = 0; w < DW_WAVES; w++) {
        double s = 0, dc = 0, pk = 0;
        for (int i = 0; i < DW_TABLE_SIZE; i++) {
            const double x = d->table[w][0][i];
            s += x * x;
            dc += x;
            pk = fmax(pk, fabs(x));
        }
        const double r = sqrt(s / DW_TABLE_SIZE);
        const float dev_db = (float)fabs(20.0 * log10(r / 0.30));
        worst_rms_db = fmaxf(worst_rms_db, dev_db);
        worst_dc = fmaxf(worst_dc, (float)fabs(dc / DW_TABLE_SIZE));
        worst_peak = fmaxf(worst_peak, (float)pk);
        CHECK(pk <= 1.0, "wave %d peak %.3f", w, pk);
        CHECK(dev_db < 1.0f, "wave %d (%s) rms %.3f is %.2f dB from the 0.30 target", w, dw_wave_name(w), r, (double)dev_db);
        for (int m = 0; m < DW_MIPS; m++)
            CHECK(d->table[w][m][DW_TABLE_SIZE] == d->table[w][m][0], "wave %d level %d guard sample", w, m);
    }
    CHECK(worst_dc < 0.005f, "DC offset %.4f", (double)worst_dc);
    /* neighbours differ, so the WAVE knob has something to morph between */
    double mind = 1e9;
    for (int w = 0; w + 1 < DW_WAVES; w++) {
        static double db2[NFFT / 2];
        spectrum_db_d(d->table[w][0], db);
        spectrum_db_d(d->table[w + 1][0], db2);
        mind = fmin(mind, spectral_distance(db, db2));
    }
    CHECK(mind > 3.0, "two neighbouring waves differ by only %.2f dB", mind);

    /* Band limiting: level L keeps harmonics up to 480>>L. Anything above must be far below the fundamental.
     * The saw and the square have the most to lose. */
    float worst_leak = -200;
    const int tops[DW_MIPS] = {480, 240, 120, 60, 30, 15, 7, 3, 1};
    for (int pass = 0; pass < 2; pass++) {
        const int w = pass == 0 ? 4 : 5;
        for (int m = 0; m < DW_MIPS; m++) {
            spectrum_db_d(d->table[w][m], db);
            double fund = db[1], leak = -300;
            for (int k = tops[m] + 2; k < NFFT / 2; k++)
                leak = fmax(leak, db[k]);
            worst_leak = fmaxf(worst_leak, (float)(leak - fund));
        }
    }
    CHECK(worst_leak < -75.0f, "a band-limited level leaks %.1f dB above its top harmonic", (double)worst_leak);
    printf("  wave bank: worst rms deviation %.2f dB from target, worst DC %.4f, peak %.3f, neighbour spectra differ >= %.1f dB,\n"
           "    band-limit leak %.0f dB\n",
           (double)worst_rms_db, (double)worst_dc, (double)worst_peak, mind, (double)worst_leak);
    free(d);
}

static void test_silence_when_idle(void)
{
    enum { N = 4096 };
    static float l[N], r[N];
    dw_t *d = new_synth(44100.0f);
    for (int i = 0; i < N; i++)
        l[i] = r[i] = 1.0f;
    dw_render(d, l, r, N);
    CHECK(peak(l, N) == 0.0f && peak(r, N) == 0.0f, "idle synth must output exact zeros");
    free(d);
}

static void test_pitch(void)
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
            dw_t *d = new_synth((float)sr);
            dw_params p = pure_params();
            setup(d, &p);
            event ev[] = {{0, EV_ON, notes[ni], 127}};
            run_script(d, ev, 1, l, r, total, 256);
            const double m = zero_cross_hz(l, (int)(sr * 0.2), total, sr);
            const float c = cents(m, hz);
            worst = fmaxf(worst, fabsf(c));
            CHECK(fabsf(c) < 5.0f, "note %d at %.0f Hz measured %.3f Hz (%.2f cents)", notes[ni], sr, m, (double)c);
            free(d);
            free(l);
            free(r);
        }
    printf("  sine pitch: worst error %.3f cents over notes and rates\n", (double)worst);

    /* The saw (bank position 4) and the vowel (3) have many partials; the period still has to be the note's. */
    enum { N = 88200 };
    static float l[N], r[N];
    const int notes2[] = {45, 57, 69, 81};
    const float positions[] = {4.0f, 3.0f, 7.0f};
    float worst2 = 0;
    for (int pi = 0; pi < 3; pi++)
        for (int ni = 0; ni < 4; ni++) {
            dw_t *d = new_synth(44100.0f);
            dw_params p = pure_params();
            p.wave = wave_knob(positions[pi]);
            p.sustain = 0.8f;
            setup(d, &p);
            event ev[] = {{0, EV_ON, notes2[ni], 100}};
            run_script(d, ev, 1, l, r, N, 128);
            const double hz = note_hz(notes2[ni]);
            const double m = autocorr_hz(l, 22050, 8192, 44100.0, hz);
            const float c = cents(m, hz);
            worst2 = fmaxf(worst2, fabsf(c));
            CHECK(fabsf(c) < 5.0f, "wave %.0f note %d measured %.3f Hz vs %.3f (%.2f cents)", (double)positions[pi],
                  notes2[ni], m, hz, (double)c);
            free(d);
        }
    printf("  rich-wave pitch (saw, vowel, digital; autocorrelation): worst error %.3f cents\n", (double)worst2);
}

static void test_bend(void)
{
    enum { N = 66150 };
    static float l[N], r[N];
    dw_t *d = new_synth(44100.0f);
    dw_params p = pure_params();
    setup(d, &p);
    event ev[] = {{0, EV_ON, 60, 100}, {22050, EV_BEND, 0, 1.0f}};
    run_script(d, ev, 2, l, r, N, 128);
    const double before = zero_cross_hz(l, 11025, 22050, 44100.0);
    const double after = zero_cross_hz(l, 44100, N, 44100.0);
    const float up = cents(after, before);
    CHECK(fabsf(up - 200.0f) < 5.0f, "full bend with the default range should be +200 cents, got %.2f", (double)up);
    free(d);
    printf("  full bend up: %+.2f cents (range 2 semitones)\n", (double)up);
}

static void test_polyphony_headroom(void)
{
    enum { N = 44100 };
    static float l[N], r[N];
    const int chord[8] = {36, 43, 52, 60, 64, 67, 72, 79};
    float worst_peak = 0;
    for (int set = 0; set < 5; set++) {
        dw_t *d = new_synth(44100.0f);
        dw_params p;
        dw_default_params(&p);
        p.level = 1.0f;
        if (set == 1) { p.wave = wave_knob(5.0f); p.blend = 0.0f; p.fold = 0.0f; p.sustain = 1.0f; p.attack = 0.0005f; }
        if (set == 2) { p.wave = wave_knob(7.0f); p.blend = 0.5f; p.fold = 1.0f; p.motion = 1.0f; }
        if (set == 3) { p.wave = wave_knob(4.0f); p.blend = 1.0f; p.fold = 0.0f; p.sustain = 1.0f; p.tone = 1.0f; }
        if (set == 4) { p.wave = wave_knob(6.0f); p.blend = 0.5f; p.fold = 0.6f; p.sustain = 1.0f; }
        setup(d, &p);
        event ev[16];
        for (int i = 0; i < 8; i++) {
            ev[i].frame = i * 7; ev[i].type = EV_ON; ev[i].a = chord[i]; ev[i].b = 127;
        }
        run_script(d, ev, 8, l, r, N, 128);
        const float pk = fmaxf(peak(l, N), peak(r, N));
        worst_peak = fmaxf(worst_peak, pk);
        CHECK(all_finite(l, N) && all_finite(r, N), "NaN or Inf with 8 voices (set %d)", set);
        CHECK(pk < 1.0f, "peak %.4f reaches 0 dBFS with 8 voices at velocity 127 (set %d)", (double)pk, set);
        CHECK(dw_active_voices(d) == 8, "expected 8 active voices, got %d", dw_active_voices(d));
        free(d);
    }
    printf("  8 voices, velocity 127, five parameter sets: worst peak %.4f (%.2f dBFS)\n", (double)worst_peak,
           20.0 * log10((double)worst_peak));
}

static void test_single_note_level(void)
{
    /* One note at the default patch and level should sit at a sensible level: not tiny, not near clipping. */
    enum { N = 22050 };
    static float l[N], r[N];
    dw_t *d = new_synth(44100.0f);
    dw_params p;
    dw_default_params(&p);
    setup(d, &p);
    event ev[] = {{0, EV_ON, 60, 100}};
    run_script(d, ev, 1, l, r, N, 128);
    const double rm = rms(l + 4000, 8000);
    const float pk = peak(l + 4000, 8000);
    CHECK(rm > 0.02 && pk < 0.6f, "default note rms %.4f peak %.4f", rm, (double)pk);
    printf("  default patch, C4, velocity 100: left rms %.4f (%.1f dBFS), peak %.4f\n", rm, 20 * log10(rm), (double)pk);
    free(d);
}

static void test_release_to_silence(void)
{
    enum { N = 132300 };
    static float l[N], r[N];
    dw_t *d = new_synth(44100.0f);
    dw_params p;
    dw_default_params(&p);
    p.release = 0.1f;
    p.level = 1.0f;
    setup(d, &p);
    event ev[16];
    int n = 0;
    for (int i = 0; i < 8; i++) { ev[n].frame = 0; ev[n].type = EV_ON; ev[n].a = 50 + i * 3; ev[n].b = 110; n++; }
    for (int i = 0; i < 8; i++) { ev[n].frame = 22050; ev[n].type = EV_OFF; ev[n].a = 50 + i * 3; ev[n].b = 0; n++; }
    run_script(d, ev, n, l, r, N, 128);
    const float held = (float)rms(l + 11025, 4096);
    const float tail = peak(l + 66150, N - 66150);
    CHECK(tail == 0.0f, "output after release should be exactly zero, peak %.2e", (double)tail);
    CHECK(dw_active_voices(d) == 0, "voices still active after release: %d", dw_active_voices(d));
    const float at150 = (float)rms(l + 22050 + 6615, 441);
    CHECK(at150 < held * 0.02f, "150 ms into a 100 ms release the level should be under -34 dB (%.4f vs %.4f)", (double)at150, (double)held);
    printf("  release 100 ms: rms held %.4f, at +150 ms %.5f, silent after 1.5 s\n", (double)held, (double)at150);
    free(d);
}

static void test_pedal_and_all_off(void)
{
    enum { N = 88200 };
    static float l[N], r[N];
    dw_t *d = new_synth(44100.0f);
    dw_params p;
    dw_default_params(&p);
    p.release = 0.05f;
    p.sustain = 0.8f;
    setup(d, &p);
    event ev[] = {{0, EV_PEDAL, 1, 0}, {0, EV_ON, 60, 100}, {4410, EV_OFF, 60, 0}, {44100, EV_PEDAL, 0, 0}};
    run_script(d, ev, 4, l, r, N, 128);
    const double during = rms(l + 30000, 4000);
    const double after = peak(l + 66150, N - 66150);
    CHECK(during > 0.01, "note should keep sounding while the pedal is down (rms %.5f)", during);
    CHECK(after == 0.0, "note should end after the pedal lifts (peak %.2e)", after);

    dw_init(d, 44100.0f);
    setup(d, &p);
    event ev2[] = {{0, EV_ON, 60, 100}, {0, EV_ON, 64, 100}, {0, EV_PEDAL, 1, 0}, {2000, EV_ALLOFF, 0, 0}};
    run_script(d, ev2, 4, l, r, N, 128);
    CHECK(peak(l + 44100, N - 44100) == 0.0f, "all notes off should end every voice");
    CHECK(dw_active_voices(d) == 0, "voices left after all notes off");
    free(d);
    printf("  sustain pedal holds a released note, lifting it or all-notes-off ends the voices\n");
}

/* A note that takes over a sounding voice must not step harder than the same note arriving on a free voice.
 * Each variant is run twice with the same new note: once with all 8 voices busy (a steal) and once with one voice
 * free (a plain new note). The free-voice run is the natural attack, so it sets the bar. */
static float steal_run(int variant, int busy, float *l, float *r, int total, int hit)
{
    dw_t *d = new_synth(44100.0f);
    dw_params p;
    dw_default_params(&p);
    p.sustain = 0.8f;
    p.motion = 0.6f;
    if (variant == 3)
        p.attack = 0.0005f;
    setup(d, &p);
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
    run_script(d, ev, n, l, r, total, 128);
    free(d);
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
    dw_t *d = new_synth(44100.0f);
    dw_params p = pure_params();
    p.attack = 0.0005f;
    setup(d, &p);
    event ev[] = {{100, EV_ON, 96, 127}};
    run_script(d, ev, 1, l, r, N, 128);
    const float first = fabsf(l[100]) + fabsf(r[100]);
    const float steady = max_step(l, 1000, 3000);
    CHECK(first < 0.05f, "first sample of a note is %.4f", (double)first);
    CHECK(max_step(l, 90, 400) < 1.6f * steady + 0.02f, "start of a 0.5 ms attack steps by %.4f (steady %.4f)",
          (double)max_step(l, 90, 400), (double)steady);
    printf("  0.5 ms attack on note 96: first sample %.5f, max start step %.4f vs steady %.4f\n", (double)first,
           (double)max_step(l, 90, 400), (double)steady);
    free(d);
}

static void test_knobs_change_spectrum(void)
{
    static double a[NFFT / 2], b[NFFT / 2];
    const char *names[5] = {"wave", "blend", "motion", "fold", "tone"};
    double dist[5], c_lo[5], c_hi[5];
    for (int k = 0; k < 5; k++) {
        dw_params lo, hi;
        dw_default_params(&lo);
        lo.sustain = 1.0f;
        lo.motion = 0.0f;
        lo.fold = 0.0f;
        lo.blend = 0.0f;
        lo.tone = 1.0f;
        lo.wave = wave_knob(3.5f);
        hi = lo;
        int from = 11025;
        switch (k) {
        case 0: lo.wave = 0.0f; hi.wave = 1.0f; break;                    /* sine to digital */
        case 1: lo.wave = wave_knob(0.7f); hi = lo; hi.blend = 1.0f; break; /* near-sine against its mirror, a pulse */
        case 2: lo.wave = wave_knob(0.5f); hi = lo; hi.motion = 1.0f; from = 1500; break; /* sweep at note start */
        case 3: lo.wave = wave_knob(1.0f); hi = lo; hi.fold = 1.0f; break;
        case 4: lo.wave = wave_knob(4.0f); hi = lo; lo.tone = 0.25f; break;
        }
        note_spectrum_at(&lo, 57, 100, from, a);
        note_spectrum_at(&hi, 57, 100, from, b);
        dist[k] = spectral_distance(a, b);
        c_lo[k] = centroid_hz(a, 44100.0);
        c_hi[k] = centroid_hz(b, 44100.0);
        CHECK(dist[k] > 2.0, "%s: spectra differ by only %.2f dB", names[k], dist[k]);
        printf("  %-7s knob lo -> hi: mean spectral difference %5.1f dB, centroid %6.0f -> %6.0f Hz\n", names[k], dist[k],
               c_lo[k], c_hi[k]);
    }
    CHECK(c_hi[0] > c_lo[0] * 2.0, "WAVE from sine to digital should raise the centroid");
    CHECK(c_hi[1] > c_lo[1] * 1.5, "BLEND to the mirror wave should raise the centroid here");
    CHECK(c_hi[4] > c_lo[4] * 1.3, "TONE open should have a higher centroid than TONE closed");
}

static void test_motion_settles(void)
{
    /* MOTION sweeps the wave position at note start and settles onto the WAVE setting: the spectrum a few seconds
     * in must match the spectrum with MOTION at zero. */
    enum { N = 220500 };
    static float l[N], r[N];
    static double a[NFFT / 2], b[NFFT / 2];
    dw_params p = pure_params();
    p.wave = wave_knob(1.0f);
    p.sustain = 1.0f;
    dw_t *d = new_synth(44100.0f);
    p.motion = 1.0f;
    setup(d, &p);
    event ev[] = {{0, EV_ON, 57, 100}};
    run_script(d, ev, 1, l, r, N, 128);
    spectrum_db(l, 215000, a);
    dw_init(d, 44100.0f);
    p.motion = 0.0f;
    setup(d, &p);
    run_script(d, ev, 1, l, r, N, 128);
    spectrum_db(l, 215000, b);
    const double dist = spectral_distance(a, b);
    CHECK(dist < 1.0, "after 5 s MOTION should have settled, spectra still differ by %.2f dB", dist);
    printf("  motion settles: spectrum 5 s in differs from MOTION 0 by %.2f dB\n", dist);
    free(d);
}

static void test_wheel_velocity(void)
{
    static double a[NFFT / 2], b[NFFT / 2];
    dw_params p;
    dw_default_params(&p);
    p.motion = 0.0f;
    p.wave = wave_knob(1.0f);
    p.sustain = 1.0f;
    note_spectrum(&p, 57, 100, a);
    {
        enum { N = 22050 };
        static float l[N], r[N];
        dw_t *d = new_synth(44100.0f);
        setup(d, &p);
        event ev[] = {{0, EV_MOD, 0, 1.0f}, {2000, EV_ON, 57, 100}};
        run_script(d, ev, 2, l, r, N, 128);
        spectrum_db(l, 11025, b);
        const double dd = spectral_distance(a, b);
        CHECK(dd > 1.0, "mod wheel changes the spectrum by only %.2f dB", dd);
        printf("  mod wheel 0 -> 1: spectral difference %.1f dB, centroid %.0f -> %.0f Hz\n", dd, centroid_hz(a, 44100.0),
               centroid_hz(b, 44100.0));
        free(d);
    }
    /* velocity scales level on a pure tone: 127 vs 64 */
    enum { M = 22050 };
    static float l[M], r[M], l2[M], r2[M];
    dw_params pp = pure_params();
    dw_t *d = new_synth(44100.0f);
    setup(d, &pp);
    event e1[] = {{0, EV_ON, 69, 127}};
    run_script(d, e1, 1, l, r, M, 128);
    dw_init(d, 44100.0f);
    setup(d, &pp);
    event e2[] = {{0, EV_ON, 69, 64}};
    run_script(d, e2, 1, l2, r2, M, 128);
    const double ratio = rms(l2 + 8000, 8000) / rms(l + 8000, 8000);
    CHECK(fabs(ratio - 64.0 / 127.0) < 0.01, "velocity 64 vs 127 amplitude ratio %.4f", ratio);
    free(d);
}

static void test_envelope(void)
{
    enum { N = 88200 };
    static float l[N], r[N];
    dw_t *d = new_synth(44100.0f);
    dw_params p = pure_params();
    p.attack = 0.3f;
    p.sustain = 0.5f;
    p.decay = 0.2f;
    setup(d, &p);
    event ev[] = {{0, EV_ON, 69, 127}};
    run_script(d, ev, 1, l, r, N, 128);
    const double early = rms(l + 441, 441);
    const double peakr = rms(l + 13000, 441);
    const double late = rms(l + 60000, 4410);
    CHECK(early < 0.1 * peakr, "a 300 ms attack should start quietly (%.4f vs %.4f)", early, peakr);
    CHECK(fabs(late / peakr - 0.5 * 1.0) < 0.12, "sustain 0.5 should settle near half the peak (%.3f)", late / peakr);
    printf("  envelope: 300 ms attack starts at %.1f%% of peak, sustain 0.5 settles at %.1f%%\n", 100 * early / peakr,
           100 * late / peakr);
    free(d);
}

static void test_deterministic_and_block_size(void)
{
    enum { N = 66150 };
    static float la[N], ra[N], lb[N], rb[N];
    const int sizes[] = {1, 7, 13, 64, 128, 1000, 4096};
    event ev[32];
    int n = 0;
    for (int i = 0; i < 14; i++) {
        ev[n].frame = i * 2500 + (i * 37) % 300;
        ev[n].type = EV_ON;
        ev[n].a = 36 + (i * 11) % 50;
        ev[n].b = (float)(40 + (i * 29) % 87);
        n++;
    }
    ev[n].frame = 20000; ev[n].type = EV_BEND; ev[n].a = 0; ev[n].b = 0.6f; n++;
    ev[n].frame = 30000; ev[n].type = EV_OFF; ev[n].a = 36 + (3 * 11) % 50; ev[n].b = 0; n++;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (ev[j].frame < ev[i].frame) { event t = ev[i]; ev[i] = ev[j]; ev[j] = t; }

    dw_params p;
    dw_default_params(&p);
    p.release = 0.3f;
    dw_t *d = new_synth(44100.0f);
    setup(d, &p);
    run_script(d, ev, n, la, ra, N, 4096);
    CHECK(peak(la, N) > 0.05f, "script produced no sound");
    for (int s = 0; s < 7; s++) {
        dw_init(d, 44100.0f);
        setup(d, &p);
        memset(lb, 0, sizeof lb);
        run_script(d, ev, n, lb, rb, N, sizes[s]);
        CHECK(memcmp(la, lb, sizeof la) == 0 && memcmp(ra, rb, sizeof ra) == 0,
              "output differs between block size 4096 and %d", sizes[s]);
    }
    dw_reset(d);
    setup(d, &p);
    run_script(d, ev, n, lb, rb, N, 128);
    CHECK(memcmp(la, lb, sizeof la) == 0, "output after dw_reset differs from a fresh instance");
    free(d);
    printf("  bit-identical output for block sizes 1, 7, 13, 64, 128, 1000, 4096 and after reset (no random numbers used)\n");
}

static void test_sample_rates_and_extremes(void)
{
    const float rates[] = {22050.0f, 32000.0f, 44100.0f, 48000.0f, 88200.0f, 96000.0f, 192000.0f};
    for (int ri = 0; ri < 7; ri++) {
        const int N = (int)(rates[ri] * 0.5f);
        float *l = malloc(sizeof(float) * (size_t)N), *r = malloc(sizeof(float) * (size_t)N);
        dw_t *d = new_synth(rates[ri]);
        for (int corner = 0; corner < 8; corner++) {
            dw_init(d, rates[ri]);
            dw_params p;
            dw_default_params(&p);
            p.wave = (corner & 1) ? 1.0f : 0.0f;
            p.blend = (corner & 2) ? 1.0f : 0.0f;
            p.motion = (corner & 4) ? 1.0f : 0.0f;
            p.fold = (corner & 2) ? 1.0f : 0.0f;
            p.tone = (corner & 4) ? 0.0f : 1.0f;
            p.level = 1.0f;
            p.attack = 0.0005f;
            setup(d, &p);
            event ev[10];
            for (int i = 0; i < 10; i++) { ev[i].frame = i * 100; ev[i].type = EV_ON; ev[i].a = i == 9 ? 127 : 12 + i * 13; ev[i].b = 127; }
            ev[9].frame = 900;
            run_script(d, ev, 10, l, r, N, 128);
            const float pk = fmaxf(peak(l, N), peak(r, N));
            CHECK(all_finite(l, N) && all_finite(r, N), "non-finite at %.0f Hz corner %d", (double)rates[ri], corner);
            CHECK(pk < 1.0f, "peak %.4f at %.0f Hz corner %d", (double)pk, (double)rates[ri], corner);
        }
        free(d);
        free(l);
        free(r);
    }
    printf("  22.05 to 192 kHz, eight extreme corners, 10 notes incl. 127: finite and under 0 dBFS\n");
}

/* Render a held chord. Every 128 samples the chosen control jumps between lo and hi (toggle) or stays at lo. */
static float sweep_run(int knob, float lo, float hi, int toggle, float *l, float *r, int total)
{
    dw_params p;
    dw_default_params(&p);
    p.sustain = 1.0f;
    p.motion = 0.0f;
    p.attack = 0.002f;
    p.level = 0.8f;
    dw_t *d = new_synth(44100.0f);
    setup(d, &p);
    dw_note_on(d, 55, 100);
    dw_note_on(d, 62, 100);
    dw_note_on(d, 67, 100);
    int k = 0;
    for (int i = 0; i < total; i += 128, k++) {
        dw_params q = p;
        const float v = (toggle && (k & 1)) ? hi : lo;
        float *tgt = knob == 0 ? &q.wave : knob == 1 ? &q.blend : knob == 2 ? &q.motion : knob == 3 ? &q.fold
                   : knob == 4 ? &q.tone : &q.level;
        *tgt = v;
        dw_set_params(d, &q);
        dw_render(d, l + i, r + i, total - i < 128 ? total - i : 128);
    }
    free(d);
    return max_step(l, 20000, total);
}

static void test_param_sweep_no_zipper(void)
{
    enum { N = 88200 };
    static float l[N], r[N];
    const char *names[6] = {"wave", "blend", "motion", "fold", "tone", "level"};
    const float lo[6] = {0.1f, 0.0f, 0.0f, 0.0f, 0.2f, 0.2f};
    const float hi[6] = {0.9f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    /* Jumping a control by its whole range every 128 samples changes the sound a great deal, so the bar is the
     * largest step any static setting along the way makes by itself. A hard jump in the signal would be far above that. */
    for (int k = 0; k < 6; k++) {
        /* the bar includes the values in between, because a sweep passes through them */
        float bar = 0, a = 0, b = 0;
        for (int s = 0; s <= 4; s++) {
            const float v = lo[k] + (hi[k] - lo[k]) * (float)s / 4.0f;
            const float st = sweep_run(k, v, v, 0, l, r, N);
            bar = fmaxf(bar, st);
            if (s == 0) a = st;
            if (s == 4) b = st;
        }
        const float swept = sweep_run(k, lo[k], hi[k], 1, l, r, N);
        printf("  %-7s jumping %.1f <-> %.1f every 128 samples: max step %.4f (static %.4f at the ends, %.4f worst of five)\n",
               names[k], (double)lo[k], (double)hi[k], (double)swept, (double)a, (double)bar);
        (void)b;
        CHECK(swept < bar * 1.3f + 0.005f, "%s: sweep steps %.4f against a static bar of %.4f", names[k], (double)swept, (double)bar);
        CHECK(all_finite(l, N), "NaN in %s sweep", names[k]);
    }
}

static void test_level_and_stereo(void)
{
    enum { N = 22050 };
    static float l[N], r[N];
    dw_t *d = new_synth(44100.0f);
    dw_params p;
    dw_default_params(&p);
    p.level = 0.0f;
    setup(d, &p);
    dw_reset(d);
    event ev[] = {{0, EV_ON, 60, 127}};
    run_script(d, ev, 1, l, r, N, 128);
    CHECK(peak(l, N) == 0.0f && peak(r, N) == 0.0f, "level 0 must be silent");

    p.level = 1.0f;
    dw_init(d, 44100.0f);
    setup(d, &p);
    dw_reset(d);
    event ev2[] = {{0, EV_ON, 60, 100}, {0, EV_ON, 67, 100}, {0, EV_ON, 72, 100}};
    run_script(d, ev2, 3, l, r, N, 128);
    static float m[N], s[N];
    for (int i = 0; i < N; i++) { m[i] = l[i] + r[i]; s[i] = l[i] - r[i]; }
    const double rm = rms(m + 4000, 8000), rs = rms(s + 4000, 8000), rl = rms(l + 4000, 8000);
    CHECK(rm > rl * 1.2, "mono sum should add up (sum %.4f, left %.4f)", rm, rl);
    CHECK(rs > 0.0 && rs < rm, "stereo difference should be present but smaller than the sum (%.4f vs %.4f)", rs, rm);
    printf("  stereo: mid %.4f, side %.4f (side/mid %.2f)\n", rm, rs, rs / rm);
    free(d);
}

static void test_activity(void)
{
    enum { N = 8192 };
    static float l[N], r[N];
    dw_t *d = new_synth(44100.0f);
    dw_activity a;
    dw_take_activity(d, &a);
    CHECK(a.peak == 0.0f && a.voices == 0, "idle activity");
    const unsigned n0 = a.notes;
    dw_note_on(d, 60, 100);
    dw_render(d, l, r, N);
    dw_take_activity(d, &a);
    CHECK(a.peak > 0.01f && a.voices == 1 && a.notes != n0, "activity after a note: peak %.4f voices %d", (double)a.peak, a.voices);
    dw_take_activity(d, &a);
    CHECK(a.peak == 0.0f, "peak clears after it is taken");
    free(d);
}

/* Aliasing. The note is picked so that its period divides the FFT window: sr = f * NFFT / k, so every harmonic
 * lands in one bin. Anything away from a harmonic bin is aliasing or interpolation noise. */
static void alias_run(int note, int k, float position, float fold, double *worst_db, double *top_h_db)
{
    enum { N = 16384 };
    static float l[N], r[N];
    static double db[NFFT / 2];
    const double f = note_hz(note);
    const float sr = (float)(f * NFFT / k);
    dw_t *d = new_synth(sr);
    dw_params p = pure_params();
    p.wave = wave_knob(position);
    p.fold = fold;
    p.sustain = 1.0f;
    setup(d, &p);
    event ev[] = {{0, EV_ON, note, 127}};
    run_script(d, ev, 1, l, r, N, 128);
    spectrum_db(l, 8000, db);
    double fund = db[k], worst = -300;
    for (int b = 1; b < NFFT / 2; b++) {
        int near = 0;
        for (int h = 1; h * k < NFFT / 2 + 4; h++)
            if (abs(b - h * k) <= 3)
                near = 1;
        if (!near && db[b] > worst)
            worst = db[b];
    }
    double top = -300;
    for (int h = 1; h * k < NFFT / 2; h++)
        top = fmax(top, db[h * k]);
    *worst_db = worst - fund;
    *top_h_db = top - fund;
    free(d);
}

static void test_aliasing(void)
{
    const int notes[3] = {84, 96, 105};
    const int bins[3] = {97, 89, 61};
    const char *names[3] = {"saw", "square", "digital"};
    const float pos[3] = {4.0f, 5.0f, 7.0f};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            double w, t;
            alias_run(notes[j], bins[j], pos[i], 0.0f, &w, &t);
            CHECK(w < -60.0, "%s at note %d: strongest non-harmonic component %.1f dB re the fundamental", names[i], notes[j], w);
            printf("  alias: %-7s note %3d, strongest non-harmonic bin %6.1f dB re the fundamental\n", names[i], notes[j], w);
        }
    /* FOLD is a waveshaper, so it adds partials above the band limit. Reported, not asserted. */
    double w, t;
    alias_run(72, 58, 0.0f, 0.73f, &w, &t);
    printf("  alias: sine through FOLD 0.73, note 72: strongest non-harmonic bin %.1f dB re the fundamental (reported only)\n", w);
}

#ifdef DRWAVE_TIMING_ONLY
static void bench(void)
{
    enum { B = 128, BLOCKS = 20000 };
    static float l[B], r[B];
    dw_t *d = new_synth(44100.0f);
    dw_params p;
    dw_default_params(&p);
    p.sustain = 1.0f;
    const char *names[2] = {"default patch (BLEND on, FOLD on)", "BLEND 0, FOLD 0"};
    for (int cfg = 0; cfg < 2; cfg++) {
        if (cfg == 1) { p.blend = 0.0f; p.fold = 0.0f; }
        printf("%s\n", names[cfg]);
        for (int v = 1; v <= 8; v *= 2) {
            dw_init(d, 44100.0f);
            setup(d, &p);
            for (int i = 0; i < v; i++)
                dw_note_on(d, 48 + i * 4, 100);
            dw_render(d, l, r, B);
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (int b = 0; b < BLOCKS; b++)
                dw_render(d, l, r, B);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            const double us = ((double)(t1.tv_sec - t0.tv_sec) * 1e6 + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-3) / BLOCKS;
            printf("  %d voices: %.2f us per 128-frame block on this host (%.2f%% of the 2902 us budget)\n", v, us, us / 29.02);
        }
    }
    clock_t t0 = clock();
    for (int i = 0; i < 20; i++)
        dw_init(d, 44100.0f);
    printf("dw_init (builds the wave bank): %.2f ms\n", (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / 20.0);
    free(d);
}
#endif

int main(void)
{
#ifdef DRWAVE_TIMING_ONLY
    printf("DrWave core timing (host CPU, -O2, not the MPC):\n");
    bench();
    return 0;
#else
    printf("DrWave core tests\n");
    test_factory_defaults();
    test_mappings();
    test_wave_bank();
    test_silence_when_idle();
    test_pitch();
    test_bend();
    test_polyphony_headroom();
    test_single_note_level();
    test_release_to_silence();
    test_pedal_and_all_off();
    test_no_click_steal_and_retrigger();
    test_attack_start_smooth();
    test_knobs_change_spectrum();
    test_motion_settles();
    test_wheel_velocity();
    test_envelope();
    test_deterministic_and_block_size();
    test_sample_rates_and_extremes();
    test_param_sweep_no_zipper();
    test_level_and_stereo();
    test_activity();
    test_aliasing();
    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("ALL PASSED\n");
    return 0;
#endif
}
