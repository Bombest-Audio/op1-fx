/* Behavioral tests for the DNA core. Built by run.sh with ASan and UBSan.
 * The core is #included so the tests can reach its static wave reader and check it against brute force. */
#include "../dna_core.c"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int g_fail = 0, g_checks = 0;

#define CHECK(cond, ...) \
    do { \
        g_checks++; \
        if (!(cond)) { \
            g_fail++; \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } while (0)

static void ok(const char *name) { printf("ok   %s\n", name); }

/* ---------- helpers ---------- */

static unsigned g_rs = 12345u;
static double frand(void) { return (double)(xorshift(&g_rs) >> 8) / 16777216.0; }

static dna_t *new_core(float sr, const dna_params *p)
{
    dna_t *d = (dna_t *)malloc(sizeof *d);
    dna_init(d, sr);
    dna_params q;
    dna_default_params(&q);
    if (p)
        q = *p;
    dna_set_params(d, &q);
    dna_reset(d);
    return d;
}

static void render_n(dna_t *d, float *l, float *r, int n)
{
    dna_render(d, l, r, n);
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
    float p = 0.0f;
    for (int i = 0; i < n; i++)
        if (fabsf(x[i]) > p)
            p = fabsf(x[i]);
    return p;
}

static double rms_of(const float *x, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (n > 0 ? n : 1));
}

/* Fundamental of a periodic signal near f0: autocorrelation peak near a lag of about 3000 samples (many whole periods,
 * so a lag error of a fraction of a sample is a tiny pitch error), parabolic interpolation. */
static double measure_hz(const float *x, int n, double sr, double f0)
{
    const double T = sr / f0;
    const int mult = (int)(3000.0 / T) > 1 ? (int)(3000.0 / T) : 1;
    const double center = T * mult;
    const int lo = (int)(center - 0.4 * T), hi = (int)(center + 0.4 * T) + 1; /* less than half a period either way: no neighbouring period to confuse it with */
    const int len = n - hi;
    double best = -1e30, rv[8192];
    int bestlag = lo;
    for (int lag = lo; lag <= hi; lag++) {
        double acc = 0.0, e1 = 0.0, e2 = 0.0;
        for (int i = 0; i < len; i++) {
            acc += (double)x[i] * x[i + lag];
            e1 += (double)x[i] * x[i];
            e2 += (double)x[i + lag] * x[i + lag];
        }
        const double r = acc / sqrt(e1 * e2 + 1e-30);
        rv[lag - lo] = r;
        if (r > best) {
            best = r;
            bestlag = lag;
        }
    }
    double frac = 0.0;
    if (bestlag > lo && bestlag < hi) {
        const double a = rv[bestlag - 1 - lo], b = rv[bestlag - lo], c = rv[bestlag + 1 - lo];
        const double den = a - 2.0 * b + c;
        if (den < 0.0)
            frac = 0.5 * (a - c) / den;
    }
    return sr * mult / ((double)bestlag + frac);
}

static double cents(double measured, double expected) { return 1200.0 * log2(measured / expected); }

static double hz_of_note(double note) { return 440.0 * pow(2.0, (note - 69.0) / 12.0); }

/* radix-2 FFT, in place */
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
        const double ang = -TWO_PI / len;
        const double wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1.0, ci = 0.0;
            for (int k = 0; k < len / 2; k++) {
                const int a = i + k, b = i + k + len / 2;
                const double tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr; im[a] += ti;
                const double ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
}

/* magnitude spectrum with a Blackman-Harris window, bins 0..n/2 */
#define SPEC_N 16384
static void spectrum(const float *x, double *mag)
{
    static double re[SPEC_N], im[SPEC_N];
    for (int i = 0; i < SPEC_N; i++) {
        const double t = (double)i / (SPEC_N - 1);
        const double w = 0.35875 - 0.48829 * cos(2 * M_PI * t) + 0.14128 * cos(4 * M_PI * t) - 0.01168 * cos(6 * M_PI * t);
        re[i] = x[i] * w;
        im[i] = 0.0;
    }
    fft(re, im, SPEC_N);
    for (int i = 0; i <= SPEC_N / 2; i++)
        mag[i] = sqrt(re[i] * re[i] + im[i] * im[i]);
}

/* 0..1 distance between two spectra, each normalized to unit sum below `maxbin` */
static double spec_dist(const double *a, const double *b, int maxbin)
{
    double sa = 0, sb = 0;
    for (int i = 1; i < maxbin; i++) { sa += a[i]; sb += b[i]; }
    double d = 0;
    for (int i = 1; i < maxbin; i++)
        d += fabs(a[i] / sa - b[i] / sb);
    return 0.5 * d;
}

#define BIG 65536
static float bl[BIG], br[BIG];

/* hold a note, skip the attack, return SPEC_N samples of the left channel */
static void hold_note(dna_t *d, int note, int vel, float *out)
{
    dna_note_on(d, note, vel);
    render_n(d, bl, br, 8192);
    render_n(d, bl, br, SPEC_N);
    memcpy(out, bl, sizeof(float) * SPEC_N);
}

static dna_params sustained_params(void)
{
    dna_params p;
    dna_default_params(&p);
    p.sustain = 1.0f;
    p.attack = 0.001f;
    p.level = 1.0f;
    return p;
}

/* ---------- tests ---------- */

/* The anti-aliased reader must equal the waveform convolved with a triangle two samples wide. */
static void test_reader_matches_brute_force(void)
{
    dna_t *d = new_core(44100.0f, NULL);
    double worst = 0.0;
    for (int trial = 0; trial < 400; trial++) {
        const double x = frand();
        const double s = 1e-4 + frand() * 0.05;
        /* brute force: integrate a(x - t) * tri(t) with tri of half-width s, 20000 sub-steps */
        const int steps = 20000;
        double acc = 0.0, wsum = 0.0;
        for (int k = 0; k < steps; k++) {
            const double t = -s + 2.0 * s * ((k + 0.5) / steps);
            const double tri = 1.0 - fabs(t) / s;
            const double pos = wrap01(x + t) * DNA_WAVE_SLOTS;
            int slot = (int)pos;
            if (slot > DNA_WAVE_SLOTS - 1) slot = DNA_WAVE_SLOTS - 1;
            acc += d->wave.a[slot] * tri;
            wsum += tri;
        }
        const double ref = acc / wsum;
        const double got = wave_read(&d->wave, x, s);
        const double err = fabs(ref - got);
        if (err > worst)
            worst = err;
    }
    CHECK(worst < 2e-3, "reader vs brute force worst error %g", worst);
    printf("     reader vs brute-force triangle filter: worst abs error %.2e (wave peak 1)\n", worst);
    /* the table has zero mean and a periodic first integral */
    double mean = 0.0;
    for (int i = 0; i < DNA_WAVE_SLOTS; i++)
        mean += d->wave.a[i];
    CHECK(fabs(mean / DNA_WAVE_SLOTS) < 1e-12, "table mean %g", mean);
    double pk = 0.0;
    for (int i = 0; i < DNA_WAVE_SLOTS; i++)
        if (fabs(d->wave.a[i]) > pk) pk = fabs(d->wave.a[i]);
    CHECK(fabs(pk - 1.0) < 1e-9, "table peak %g", pk);
    free(d);
    ok("reader matches brute-force filter, table is zero mean and peaks at 1");
}

static void test_pitch(void)
{
    const float rates[] = {22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f};
    const int notes[] = {24, 36, 48, 60, 69, 72, 84, 96};
    double worst = 0.0;
    static float buf[20000];
    for (int grit = 0; grit < 2; grit++) {
        for (size_t ri = 0; ri < sizeof rates / sizeof *rates; ri++) {
            for (size_t ni = 0; ni < sizeof notes / sizeof *notes; ni++) {
                const double sr = rates[ri];
                const double f0 = hz_of_note(notes[ni]);
                if (f0 > 0.2 * sr)
                    continue;
                dna_params p = sustained_params();
                if (!grit)
                    p.grit = 0.0f;
                dna_t *d = new_core((float)sr, &p);
                dna_note_on(d, notes[ni], 100);
                render_n(d, bl, br, (int)(0.15 * sr)); /* settle */
                int got = 0;
                while (got < 20000) {
                    const int n = 4096 < 20000 - got ? 4096 : 20000 - got;
                    render_n(d, bl, br, n);
                    memcpy(buf + got, bl, sizeof(float) * (size_t)n);
                    got += n;
                }
                const double hz = measure_hz(buf, 20000, sr, f0);
                const double c = cents(hz, f0);
                if (fabs(c) > worst)
                    worst = fabs(c);
                CHECK(fabs(c) < 5.0, "note %d at %.0f Hz grit=%d: %.3f cents off (measured %.3f Hz, wanted %.3f)",
                      notes[ni], sr, grit, c, hz, f0);
                free(d);
            }
        }
    }
    printf("     worst pitch error over notes 24..96 and 22.05k..192k, grit off and default: %.3f cents\n", worst);
    ok("pitch of the played note within 5 cents");
}

static void test_bend_and_modwheel(void)
{
    static float buf[20000];
    dna_params p = sustained_params();
    p.grit = 0.0f;
    dna_t *d = new_core(44100.0f, &p);
    dna_note_on(d, 60, 100);
    dna_pitch_bend(d, 1.0f);
    render_n(d, bl, br, 20000);
    int got = 0;
    while (got < 20000) {
        render_n(d, bl, br, 4000);
        memcpy(buf + got, bl, 4000 * sizeof(float));
        got += 4000;
    }
    const double c = cents(measure_hz(buf, 20000, 44100.0, hz_of_note(62)), hz_of_note(62));
    CHECK(fabs(c) < 5.0, "full bend up should be +2 semitones, %.3f cents off", c);
    free(d);

    /* mod wheel: vibrato changes the output and stays finite and bounded */
    dna_t *a = new_core(44100.0f, &p), *b = new_core(44100.0f, &p);
    dna_note_on(a, 60, 100);
    dna_note_on(b, 60, 100);
    dna_mod_wheel(b, 1.0f);
    static float al[22050], bl2[22050], tmp[22050];
    render_n(a, al, tmp, 22050);
    render_n(b, bl2, tmp, 22050);
    double diff = 0;
    for (int i = 11025; i < 22050; i++)
        diff += fabs(al[i] - bl2[i]);
    CHECK(diff > 1.0, "mod wheel changes nothing (%g)", diff);
    CHECK(finite_all(bl2, 22050) && peak_of(bl2, 22050) <= 1.0f, "mod wheel output bad");
    free(a);
    free(b);
    ok("pitch bend range and mod wheel vibrato");
}

static void test_polyphony_and_headroom(void)
{
    const float rates[] = {22050.0f, 44100.0f, 96000.0f};
    int cases = 0;
    float worst = 0.0f;
    for (size_t ri = 0; ri < 3; ri++) {
        for (int corner = 0; corner < 64; corner++) {
            dna_params p;
            dna_default_params(&p);
            p.mutate = (corner & 1) ? 1.0f : -1.0f;
            p.bits = (corner & 2) ? 1.0f : 0.0f;
            p.sub = (corner & 4) ? 1.0f : 0.0f;
            p.grit = (corner & 8) ? 1.0f : 0.0f;
            p.sustain = (corner & 16) ? 1.0f : 0.3f;
            p.level = 1.0f;
            p.attack = (corner & 32) ? 0.0005f : 0.05f;
            dna_t *d = new_core(rates[ri], &p);
            /* 8 notes, full velocity, then 4 more to force steals */
            const int chord[12] = {36, 43, 48, 55, 60, 64, 67, 72, 76, 79, 84, 91};
            for (int i = 0; i < 12; i++) {
                dna_note_on(d, chord[i], 127);
                render_n(d, bl, br, 64);
            }
            float pk = 0.0f;
            int fin = 1;
            for (int blk = 0; blk < 20; blk++) {
                render_n(d, bl, br, 512);
                fin &= finite_all(bl, 512) && finite_all(br, 512);
                const float q = peak_of(bl, 512) > peak_of(br, 512) ? peak_of(bl, 512) : peak_of(br, 512);
                if (q > pk) pk = q;
            }
            CHECK(fin, "NaN or Inf at corner %d sr %.0f", corner, rates[ri]);
            CHECK(pk <= 1.0f, "peak %f above 0 dBFS at corner %d sr %.0f", pk, corner, rates[ri]);
            CHECK(dna_active_voices(d) <= DNA_VOICES, "too many voices");
            if (pk > worst) worst = pk;
            cases++;
            free(d);
        }
    }
    printf("     %d corner cases (3 rates x 64 knob corners), 12 notes into 8 voices at velocity 127, level 1: peak %.4f\n", cases, worst);
    ok("polyphony: 8 voices plus steals, no NaN, peak under 0 dBFS");
}

static void test_release_to_silence(void)
{
    dna_params p = sustained_params();
    p.release = 0.1f;
    dna_t *d = new_core(44100.0f, &p);
    for (int n = 48; n < 56; n++)
        dna_note_on(d, n, 100);
    render_n(d, bl, br, 4410);
    CHECK(dna_active_voices(d) == 8, "expected 8 voices, got %d", dna_active_voices(d));
    CHECK(rms_of(bl, 4410) > 0.01, "silent while held");
    for (int n = 48; n < 56; n++)
        dna_note_off(d, n);
    render_n(d, bl, br, 44100);
    render_n(d, bl, br, 4410);
    CHECK(peak_of(bl, 4410) < 1e-4f && peak_of(br, 4410) < 1e-4f, "not silent after release: %g", peak_of(bl, 4410));
    CHECK(dna_active_voices(d) == 0, "voices still active: %d", dna_active_voices(d));
    free(d);

    /* sustain pedal holds a released note until the pedal is lifted */
    d = new_core(44100.0f, &p);
    dna_sustain_pedal(d, 1);
    dna_note_on(d, 60, 100);
    render_n(d, bl, br, 2000);
    dna_note_off(d, 60);
    render_n(d, bl, br, 22050);
    CHECK(rms_of(bl, 4410) > 0.01, "pedal did not hold the note");
    dna_sustain_pedal(d, 0);
    render_n(d, bl, br, 44100);
    render_n(d, bl, br, 4410);
    CHECK(peak_of(bl, 4410) < 1e-4f, "pedal up did not release");
    /* all notes off */
    dna_note_on(d, 60, 100);
    dna_note_on(d, 64, 100);
    render_n(d, bl, br, 2000);
    dna_all_off(d);
    render_n(d, bl, br, 44100);
    render_n(d, bl, br, 4410);
    CHECK(peak_of(bl, 4410) < 1e-4f, "all_off did not release");
    /* reset is instant silence */
    dna_note_on(d, 60, 100);
    render_n(d, bl, br, 2000);
    dna_reset(d);
    render_n(d, bl, br, 512);
    CHECK(peak_of(bl, 512) == 0.0f && dna_active_voices(d) == 0, "reset left sound behind");
    free(d);
    ok("note off, pedal, all-off and reset reach silence");
}

/* A takeover must fade, never cut. The same events run on two identical cores, one of which also receives the new
 * note. Until the fade ends the only difference between them is the stolen voice's fade gain, so output_A / output_B
 * is that gain. It has to follow the linear 1 ms ramp. (The wave has sharp edges of its own, so a per-sample step
 * threshold cannot tell a click from the wave.) */
static void test_retrigger_and_steal_do_not_click(void)
{
    static float la[400], lb[400], tmp[400], big[20000], big2[20000];
    for (int variant = 0; variant < 2; variant++) {
        dna_params p = sustained_params();
        p.grit = 0.0f;
        dna_t *a = new_core(44100.0f, &p), *b = new_core(44100.0f, &p);
        dna_t *both[2] = {a, b};
        for (int k = 0; k < 2; k++) {
            if (variant == 0) {
                dna_note_on(both[k], 60, 110);
            } else {
                dna_note_on(both[k], 40, 110); /* the oldest voice, and the loud one */
                for (int n = 41; n < 48; n++)
                    dna_note_on(both[k], n, 1); /* seven nearly silent notes fill the other voices */
            }
            render_n(both[k], big, big2, 20000);
        }
        /* variant 0 retriggers the same note, variant 1 is a ninth note that must steal */
        dna_note_on(a, variant == 0 ? 60 : 70, variant == 0 ? 110 : 1);
        render_n(a, la, tmp, 400);
        render_n(b, lb, tmp, 400);
        float pk = peak_of(lb, 44);
        const float inc = a->fade_inc;
        int valid = 0;
        float worst = 0.0f, g_end = 1.0f;
        for (int n = 0; n < 40; n++) {
            if (fabsf(lb[n]) < 0.5f * pk)
                continue;
            const float g = 1.0f + (la[n] - lb[n]) / lb[n];
            const float expect = 1.0f - (float)(n + 1) * inc;
            const float err = fabsf(g - expect);
            if (err > worst) worst = err;
            g_end = g;
            valid++;
        }
        printf("     %s: fade gain follows a linear ramp of %.0f samples; worst error %.3f over %d samples\n",
               variant == 0 ? "retrigger" : "steal    ", 1.0f / inc, worst, valid);
        CHECK(valid >= 8, "too few usable samples (%d) to judge the fade", valid);
        CHECK(worst < (variant == 0 ? 0.02f : 0.12f), "%s fade gain departs from the ramp by %.3f", variant == 0 ? "retrigger" : "steal", worst);
        CHECK(g_end < 0.4f, "fade is not heading to zero (%.2f at the last usable sample)", g_end);
        /* after the fade the new note starts from silence: attack, no jump */
        CHECK(fabsf(la[60]) < 0.5f * peak_of(lb, 44), "new note does not start quietly: %g", la[60]);
        free(a);
        free(b);
    }
    ok("retrigger and voice steal fade out over about 1 ms instead of cutting");
}

static double distance_for(float lo, float hi, int knob, int note)
{
    static float a[SPEC_N], b[SPEC_N];
    static double ma[SPEC_N / 2 + 1], mb[SPEC_N / 2 + 1];
    dna_params p = sustained_params();
    p.grit = 0.0f;
    p.sub = 0.0f;
    dna_params q = p;
    float *pf[2] = {NULL, NULL};
    float *fields[2] = {NULL, NULL};
    (void)pf;
    dna_params *pp[2] = {&p, &q};
    const float v[2] = {lo, hi};
    for (int k = 0; k < 2; k++) {
        float *f = knob == 0 ? &pp[k]->mutate : knob == 1 ? &pp[k]->bits : knob == 2 ? &pp[k]->sub : &pp[k]->grit;
        *f = v[k];
        fields[k] = f;
    }
    dna_t *d0 = new_core(44100.0f, &p), *d1 = new_core(44100.0f, &q);
    hold_note(d0, note, 100, a);
    hold_note(d1, note, 100, b);
    spectrum(a, ma);
    spectrum(b, mb);
    free(d0);
    free(d1);
    return spec_dist(ma, mb, SPEC_N / 2 * 10000 / 22050);
}

static void test_knobs_change_the_sound(void)
{
    const char *names[4] = {"MUTATE", "BITS", "SUB", "GRIT"};
    struct { float lo, hi; } rng[4] = {{-1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f}};
    /* a knob has to move the normalized spectrum by more than this; identical settings give exactly 0 */
    const double min_dist = 0.10;
    double same = distance_for(0.3f, 0.3f, 1, 57);
    CHECK(same < 1e-9, "same settings gave distance %g", same);
    for (int k = 0; k < 4; k++) {
        const double dist = distance_for(rng[k].lo, rng[k].hi, k, 57);
        printf("     %-6s full sweep moves the spectrum by %.3f (0 = identical, 1 = disjoint)\n", names[k], dist);
        CHECK(dist > min_dist, "%s barely changes the spectrum: %.3f", names[k], dist);
    }
    /* mid-range steps move the sound too, so the knobs are not dead over most of their travel */
    const float stepA[4] = {-0.5f, 0.2f, 0.2f, 0.1f}, stepB[4] = {-0.3f, 0.4f, 0.4f, 0.3f};
    for (int k = 0; k < 4; k++) {
        const double dist = distance_for(stepA[k], stepB[k], k, 57);
        printf("     %-6s small step moves the spectrum by %.3f\n", names[k], dist);
        CHECK(dist > 0.01, "%s dead over a small step: %.4f", names[k], dist);
    }
    ok("each of the four knobs changes the spectrum");
}

static void test_envelope_knobs(void)
{
    static float x[44100];
    /* attack: time to reach 90% of the first-second peak */
    double t90[2];
    const float att[2] = {0.002f, 0.2f};
    for (int k = 0; k < 2; k++) {
        dna_params p = sustained_params();
        p.attack = att[k];
        p.grit = 0.0f;
        dna_t *d = new_core(44100.0f, &p);
        dna_note_on(d, 57, 100);
        render_n(d, x, br, 44100);
        float pk = 0.0f;
        for (int i = 0; i < 44100; i++) if (fabsf(x[i]) > pk) pk = fabsf(x[i]);
        int i = 0;
        for (; i < 44100; i++) if (fabsf(x[i]) > 0.9f * pk) break;
        t90[k] = i / 44100.0;
        free(d);
    }
    printf("     attack 2 ms reaches 90%% at %.1f ms, attack 200 ms at %.1f ms\n", t90[0] * 1000, t90[1] * 1000);
    CHECK(t90[0] < 0.02 && t90[1] > 0.1, "attack knob timing off");

    /* decay and sustain: level 1 s after the note starts */
    double lev[3];
    const float dec[3] = {0.1f, 3.0f, 3.0f}, sus[3] = {0.2f, 0.2f, 0.9f};
    for (int k = 0; k < 3; k++) {
        dna_params p = sustained_params();
        p.decay = dec[k];
        p.sustain = sus[k];
        p.grit = 0.0f;
        dna_t *d = new_core(44100.0f, &p);
        dna_note_on(d, 57, 100);
        render_n(d, x, br, 44100);
        render_n(d, x, br, 4410);
        lev[k] = peak_of(x, 4410);
        free(d);
    }
    printf("     level at 1 s: decay 0.1 s sustain 0.2 -> %.3f, decay 3 s sustain 0.2 -> %.3f, decay 3 s sustain 0.9 -> %.3f\n",
           lev[0], lev[1], lev[2]);
    CHECK(lev[1] > 1.25 * lev[0], "decay knob has no effect");
    CHECK(lev[2] > 2.0 * lev[0], "sustain knob has no effect");

    /* release: time from note off to -40 dB */
    double rel[2];
    const float rl[2] = {0.05f, 1.0f};
    for (int k = 0; k < 2; k++) {
        dna_params p = sustained_params();
        p.release = rl[k];
        p.grit = 0.0f;
        dna_t *d = new_core(44100.0f, &p);
        dna_note_on(d, 57, 100);
        render_n(d, x, br, 11025);
        const float before = peak_of(x, 2000);
        dna_note_off(d, 57);
        int n = 0;
        for (; n < 44100 * 4; n += 256) {
            render_n(d, x, br, 256);
            if (peak_of(x, 256) < before * 0.01f) break;
        }
        rel[k] = n / 44100.0;
        free(d);
    }
    printf("     release 50 ms falls 40 dB in %.0f ms, release 1 s in %.0f ms (spec: 60 dB in the set time)\n", rel[0] * 1000, rel[1] * 1000);
    CHECK(rel[1] > 4.0 * rel[0], "release knob has no effect");
    CHECK(rel[0] > 0.02 && rel[0] < 0.08, "release 50 ms measured %.3f s", rel[0]);

    /* level */
    dna_params p = sustained_params();
    p.grit = 0.0f;
    float peakv[2];
    const float lv[2] = {0.2f, 0.4f};
    for (int k = 0; k < 2; k++) {
        p.level = lv[k];
        dna_t *d = new_core(44100.0f, &p);
        dna_note_on(d, 57, 30);
        render_n(d, x, br, 20000);
        peakv[k] = peak_of(x, 20000);
        free(d);
    }
    CHECK(fabsf(peakv[1] / peakv[0] - 2.0f) < 0.01f, "level is not linear: %f", peakv[1] / peakv[0]);
    p.level = 0.0f;
    dna_t *d = new_core(44100.0f, &p);
    dna_note_on(d, 57, 127);
    render_n(d, x, br, 20000);
    CHECK(peak_of(x, 20000) == 0.0f, "level 0 is not silent");
    free(d);
    ok("attack, decay, sustain, release and level");
}

static void test_determinism_and_split_invariance(void)
{
    dna_params p;
    dna_default_params(&p);
    p.grit = 0.4f;
    p.sub = 0.3f;
    static float refl[30000], refr[30000], gl[30000], gr[30000];
    for (int run = 0; run < 6; run++) {
        dna_t *d = new_core(44100.0f, &p);
        float *ol = run == 0 ? refl : gl, *or_ = run == 0 ? refr : gr;
        int pos = 0;
        unsigned rs = 777u + (unsigned)run;
        /* the same musical events at the same sample positions; only the block splitting differs */
        int events[][3] = {{0, 60, 100}, {3000, 64, 90}, {3001, 67, 80}, {9000, 60, 0}, {12001, 72, 127}, {15000, -1, 0},
                           {17000, 64, 0}, {20000, 36, 100}};
        int ev = 0;
        while (pos < 30000) {
            int next = 30000;
            if (ev < 8 && events[ev][0] < next) next = events[ev][0];
            int n = next - pos;
            if (run > 0 && n > 0) {
                const int cap = run == 1 ? 1 : (int)(xorshift(&rs) % 300) + 1;
                if (n > cap) n = cap;
            }
            if (n > 0) {
                render_n(d, ol + pos, or_ + pos, n);
                pos += n;
            }
            while (ev < 8 && events[ev][0] == pos) {
                if (events[ev][1] < 0) {
                    dna_pitch_bend(d, 0.5f);
                    dna_mod_wheel(d, 0.7f);
                } else if (events[ev][2] == 0)
                    dna_note_off(d, events[ev][1]);
                else
                    dna_note_on(d, events[ev][1], events[ev][2]);
                ev++;
            }
        }
        if (run > 0)
            CHECK(memcmp(refl, gl, sizeof refl) == 0 && memcmp(refr, gr, sizeof refr) == 0,
                  "run %d (different block splits) differs from the reference", run);
        free(d);
    }
    CHECK(rms_of(refl, 30000) > 0.01, "reference run is silent");
    /* a fresh instance and a reset instance agree */
    dna_t *a = new_core(44100.0f, &p), *b = new_core(44100.0f, &p);
    dna_note_on(b, 50, 100);
    render_n(b, gl, gr, 5000);
    dna_reset(b);
    dna_note_on(a, 60, 100);
    dna_note_on(b, 60, 100);
    static float al[4000], bl3[4000], t[4000];
    render_n(a, al, t, 4000);
    render_n(b, bl3, t, 4000);
    CHECK(memcmp(al, bl3, sizeof al) == 0, "reset instance differs from a fresh one");
    free(a);
    free(b);
    ok("deterministic, bit-identical for any block splitting (block size 1 included), reset equals fresh");
}

/* Sweeping the shaping knobs while a note holds must not add clicks beyond what the waves along the way have. The
 * reference is the largest sample step of the static wave at five settings across the knob; a sweep may exceed it by
 * 15%, which leaves room for the in-between waves and nothing like a cut (a cut would add a step of the full swing). */
static float max_step(const float *o, int n)
{
    float m = 0.0f;
    for (int i = 1; i < n; i++) {
        const float s = fabsf(o[i] - o[i - 1]);
        if (s > m) m = s;
    }
    return m;
}

static void set_knob(dna_params *q, int knob, float u)
{
    if (knob == 0) q->mutate = -1.0f + 2.0f * u;
    if (knob == 1) q->bits = u;
    if (knob == 2) q->sub = u;
}

static void test_knob_motion_is_smooth(void)
{
    dna_params p = sustained_params();
    p.grit = 0.0f;
    static float o[22050];
    const char *nm[3] = {"MUTATE", "BITS", "SUB"};
    for (int knob = 0; knob < 3; knob++) {
        float ref = 0.0f;
        for (int k = 0; k <= 4; k++) {
            dna_params q = p;
            set_knob(&q, knob, 0.25f * (float)k);
            dna_t *d = new_core(44100.0f, &q);
            dna_note_on(d, 57, 100);
            render_n(d, bl, br, 8000);
            render_n(d, o, br, 8000);
            const float m = max_step(o, 8000);
            if (m > ref) ref = m;
            free(d);
        }
        dna_t *d = new_core(44100.0f, &p);
        dna_note_on(d, 57, 100);
        render_n(d, bl, br, 8000);
        for (int blk = 0; blk < 22050 / 64; blk++) {
            dna_params q = p;
            set_knob(&q, knob, (float)blk / (22050.0f / 64.0f));
            dna_set_params(d, &q);
            render_n(d, o + blk * 64, br, 64);
        }
        const float sweep = max_step(o, 22050 - 64);
        printf("     %-6s sweep over 0.5 s: largest sample step %.3f, largest of five static waves %.3f\n", nm[knob], sweep, ref);
        CHECK(sweep <= 1.15f * ref, "%s sweep has a step of %.3f against %.3f for static waves", nm[knob], sweep, ref);
        free(d);
    }
    ok("moving MUTATE, BITS and SUB while a note holds adds no clicks");
}

/* Aliasing: a wave read straight from the table (no anti-aliasing) against the real reader, at the same phases. The
 * share of energy that is not on a harmonic of the note is the alias energy: with an exactly periodic wave every
 * component is a harmonic until it folds over Nyquist. */
static double off_grid_db(const float *x, double f0)
{
    static double mag[SPEC_N / 2 + 1];
    spectrum(x, mag);
    const double bin_hz = 44100.0 / SPEC_N;
    double harm = 0.0, other = 0.0;
    for (int k = 3; k < SPEC_N / 2; k++) {
        const double h = k * bin_hz / f0;
        const double dist = fabs(h - floor(h + 0.5)) * f0 / bin_hz; /* bins from the nearest harmonic */
        if (dist < 4.0) harm += mag[k] * mag[k];
        else other += mag[k] * mag[k];
    }
    return 10.0 * log10(other / (harm + other));
}

static void test_alias_floor(void)
{
    static float xa[SPEC_N], xn[SPEC_N];
    const int notes[] = {72, 84, 96};
    const float bits[] = {0.0f, 0.1526f, 0.5f};
    double worst_gain = 1e9, worst_aa = -1e9;
    for (int bi = 0; bi < 3; bi++) {
        for (int ni = 0; ni < 3; ni++) {
            dna_params p = sustained_params();
            p.bits = bits[bi];
            p.grit = 0.0f;
            dna_t *d = new_core(44100.0f, &p);
            const double f0 = hz_of_note(notes[ni]);
            const double inc = f0 / (2.0 * 44100.0);
            double ph = 0.0;
            for (int i = 0; i < SPEC_N; i++) {
                xa[i] = (float)wave_read(&d->wave, ph, inc);
                int slot = (int)(ph * DNA_WAVE_SLOTS);
                xn[i] = (float)d->wave.a[slot > DNA_WAVE_SLOTS - 1 ? DNA_WAVE_SLOTS - 1 : slot];
                ph += inc;
                if (ph >= 1.0) ph -= 1.0;
            }
            const double aa = off_grid_db(xa, f0), naive = off_grid_db(xn, f0);
            printf("     note %d, bits knob %.2f: off-grid energy %.1f dB with the reader, %.1f dB read straight from the table\n",
                   notes[ni], bits[bi], aa, naive);
            if (naive - aa < worst_gain) worst_gain = naive - aa;
            if (aa > worst_aa) worst_aa = aa;
            free(d);
        }
    }
    CHECK(worst_gain > 6.0, "anti-aliasing gains only %.1f dB over a plain table read", worst_gain);
    printf("     smallest improvement over a plain table read: %.1f dB\n", worst_gain);
    ok("the reader keeps alias energy well under a plain table read");
}

static void test_state_and_bounds(void)
{
    /* absurd parameters and absurd notes must stay finite */
    dna_params p;
    dna_default_params(&p);
    p.mutate = 5.0f; p.bits = -3.0f; p.sub = 9.0f; p.grit = 7.0f;
    p.attack = -1.0f; p.decay = 0.0f; p.release = 100.0f; p.level = 3.0f; p.bend_range = 99.0f;
    dna_t *d = new_core(44100.0f, &p);
    dna_note_on(d, -5, 127);
    dna_note_on(d, 400, 255);
    dna_note_on(d, 0, 1);
    dna_note_on(d, 127, 1);
    dna_pitch_bend(d, 9.0f);
    dna_mod_wheel(d, 9.0f);
    for (int i = 0; i < 40; i++) {
        render_n(d, bl, br, 1000);
        CHECK(finite_all(bl, 1000) && finite_all(br, 1000) && peak_of(bl, 1000) <= 1.0f, "bad output with absurd inputs");
    }
    free(d);
    /* tiny and odd block sizes, odd sample rates */
    for (int sr = 8000; sr <= 384000; sr = sr * 3 / 2 + 1) {
        d = new_core((float)sr, NULL);
        dna_note_on(d, 60, 100);
        for (int i = 0; i < 200; i++)
            render_n(d, bl, br, 1 + (i % 7));
        CHECK(finite_all(bl, 7), "sr %d bad", sr);
        free(d);
    }
    CHECK(fabsf(dna_time_norm(dna_time_seconds(0.37f)) - 0.37f) < 1e-4f, "time mapping does not round trip");
    CHECK(fabsf(dna_time_seconds(0.0f) - 0.001f) < 1e-6f && fabsf(dna_time_seconds(1.0f) - 5.0f) < 1e-3f, "time mapping ends");
    CHECK(dna_bits_value(0.0f) == 2.0f && dna_bits_value(1.0f) == 128.0f, "bits mapping ends");
    ok("absurd parameters, notes, rates and block sizes stay finite and bounded");
}

static void bench(void)
{
    dna_params p;
    dna_default_params(&p);
    p.grit = 0.3f;
    dna_t *d = new_core(44100.0f, &p);
    for (int i = 0; i < 8; i++)
        dna_note_on(d, 48 + i * 4, 100);
    render_n(d, bl, br, 4096);
    const int blocks = 4000;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < blocks; i++)
        render_n(d, bl, br, 128);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / blocks;
    printf("     host timing (not the device, and this build has sanitizers): %.1f us per 128-frame block, 8 voices, grit 0.3 (budget 2902 us)\n", us);
    free(d);
}

int main(void)
{
    printf("DNA core tests\n");
    test_reader_matches_brute_force();
    test_pitch();
    test_bend_and_modwheel();
    test_polyphony_and_headroom();
    test_release_to_silence();
    test_retrigger_and_steal_do_not_click();
    test_knobs_change_the_sound();
    test_envelope_knobs();
    test_determinism_and_split_invariance();
    test_knob_motion_is_smooth();
    test_alias_floor();
    test_state_and_bounds();
    bench();
    printf("%d checks, %d failed\n", g_checks, g_fail);
    printf(g_fail ? "FAILED\n" : "PASSED\n");
    return g_fail ? 1 : 0;
}
