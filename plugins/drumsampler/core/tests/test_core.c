/* Behavioral tests for the DrumSampler core. Build and run with core/tests/run.sh */
#include "../drumsampler_core.h"
#include "../drumsampler_kit.h"

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

/* ---- helpers ---- */

#define MAX_KEEP 256
static void *kept[MAX_KEEP];
static int nkept = 0;

static void *keep(size_t bytes)
{
    void *p = calloc(1, bytes);
    if (nkept < MAX_KEEP)
        kept[nkept++] = p;
    return p;
}

static void free_kept(void)
{
    for (int i = 0; i < nkept; i++)
        free(kept[i]);
    nkept = 0;
}

static ds_t *new_core(float sr)
{
    ds_t *c = (ds_t *)malloc(sizeof *c);
    ds_init(c, sr);
    return c;
}

/* A mono sine slot. mode and the rest start from the defaults. Returns the sample buffer. */
static float *load_sine(ds_t *c, int slot, double freq, double seconds, float rate, float amp)
{
    const int n = (int)(seconds * rate);
    float *s = (float *)keep((size_t)n * sizeof(float));
    for (int i = 0; i < n; i++)
        s[i] = amp * (float)sin(2.0 * PI * freq * (double)i / (double)rate);
    ds_set_sample(c, slot, s, NULL, n, rate);
    return s;
}

static void set_mode(ds_t *c, int slot, int mode)
{
    ds_slot_params sp = c->slot_params[slot];
    sp.mode = mode;
    ds_set_slot_params(c, slot, &sp);
}

static void render(ds_t *c, float *l, float *r, int n, int block)
{
    for (int pos = 0; pos < n; pos += block) {
        const int k = n - pos < block ? n - pos : block;
        ds_render(c, l + pos, r + pos, k);
    }
}

/* Frequency from upward zero crossings with linear interpolation. */
static double measure_freq(const float *y, int n, double sr)
{
    double first = -1.0, last = -1.0;
    int count = 0;
    for (int i = 1; i < n; i++) {
        if (y[i - 1] < 0.0f && y[i] >= 0.0f) {
            const double frac = (double)(-y[i - 1]) / (double)(y[i] - y[i - 1]);
            const double t = ((double)(i - 1) + frac) / sr;
            if (first < 0.0)
                first = t;
            last = t;
            count++;
        }
    }
    return count < 2 ? 0.0 : (double)(count - 1) / (last - first);
}

static double rms(const float *y, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; i++)
        s += (double)y[i] * (double)y[i];
    return n > 0 ? sqrt(s / (double)n) : 0.0;
}

static double peak_abs(const float *y, int n)
{
    double p = 0.0;
    for (int i = 0; i < n; i++)
        if (fabs((double)y[i]) > p)
            p = fabs((double)y[i]);
    return p;
}

static double max_step(const float *y, int from, int to)
{
    double m = 0.0;
    for (int i = from < 1 ? 1 : from; i < to; i++) {
        const double d = fabs((double)y[i] - (double)y[i - 1]);
        if (d > m)
            m = d;
    }
    return m;
}

static int all_finite(const float *y, int n)
{
    for (int i = 0; i < n; i++)
        if (!isfinite(y[i]))
            return 0;
    return 1;
}

static double cents(double f, double ref)
{
    return 1200.0 * log2(f / ref);
}

static int last_above(const float *y, int n, float thr)
{
    for (int i = n - 1; i >= 0; i--)
        if (fabsf(y[i]) > thr)
            return i;
    return -1;
}

/* ---- tests ---- */

static void test_defaults_and_mappings(void)
{
    ds_params p;
    ds_default_params(&p);
    CHECK(p.sustain == 1.0f && p.level > 0.5f, "default sustain should be 1 so the envelope is transparent");
    CHECK(fabsf(ds_pitch_value(0.5f)) < 1e-6f, "pitch norm 0.5 is 0 semitones");
    CHECK(fabsf(ds_pitch_value(0.0f) + 24.0f) < 1e-5f && fabsf(ds_pitch_value(1.0f) - 24.0f) < 1e-5f, "pitch range is +-24");
    CHECK(fabsf(ds_pitch_norm(ds_pitch_value(0.3f)) - 0.3f) < 1e-6f, "pitch mapping round trips");
    CHECK(fabsf(ds_level_gain(0.5f) - 0.25f) < 1e-6f, "level gain is squared");
    ds_slot_params s;
    ds_default_slot(&s);
    CHECK(s.mode == DS_MODE_ONESHOT && s.in == 0.0f && s.out == 1.0f && s.pitch == 0.0f && s.gain == 1.0f && !s.reverse,
          "slot defaults");
    ds_t *c = new_core(44100.0f);
    ds_slot_params bad = {99.0f, -1.0f, 5.0f, 9, 7, 9.0f};
    ds_set_slot_params(c, 3, &bad);
    CHECK(c->slot_params[3].pitch == 24.0f && c->slot_params[3].in == 0.0f && c->slot_params[3].out == 1.0f &&
              c->slot_params[3].mode == DS_MODE_LOOP && c->slot_params[3].reverse == 1 && c->slot_params[3].gain == 2.0f,
          "slot params are clamped");
    ds_set_slot_params(c, -1, &bad);
    ds_set_slot_params(c, DS_SLOTS, &bad); /* out of range slots are ignored, not written */
    free(c);
}

static void test_silence_and_ignored_notes(void)
{
    ds_t *c = new_core(44100.0f);
    load_sine(c, 0, 440.0, 0.5, 44100.0f, 0.5f);
    float l[512], r[512];
    ds_render(c, l, r, 512);
    CHECK(peak_abs(l, 512) == 0.0 && peak_abs(r, 512) == 0.0, "idle core is silent");
    ds_note_on(c, 35, 100);   /* below the first slot */
    ds_note_on(c, 60, 100);   /* above the last slot */
    ds_note_on(c, 40, 100);   /* slot 4 has no sample */
    ds_render(c, l, r, 512);
    CHECK(ds_active_voices(c) == 0 && peak_abs(l, 512) == 0.0, "notes outside the slots or on empty slots do nothing");
    ds_note_on(c, 36, 100);
    ds_render(c, l, r, 512);
    CHECK(ds_active_voices(c) == 1 && peak_abs(l, 512) > 0.01, "slot 0 answers to note 36");
    ds_all_off(c);
    free(c);
    free_kept();
}

static void test_pitch_accuracy(void)
{
    const float rates[] = {22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f};
    const float semis[] = {0.0f, 7.0f, -12.0f, 3.5f, 12.0f};
    double worst = 0.0;
    for (size_t ri = 0; ri < sizeof rates / sizeof rates[0]; ri++) {
        for (size_t si = 0; si < sizeof semis / sizeof semis[0]; si++) {
            ds_t *c = new_core(rates[ri]);
            load_sine(c, 5, 440.0, 3.0, 44100.0f, 0.5f);   /* source recorded at 44.1 kHz whatever the host rate */
            ds_slot_params sp = c->slot_params[5];
            sp.pitch = semis[si];
            ds_set_slot_params(c, 5, &sp);
            const int n = (int)(1.2f * rates[ri]);
            float *l = (float *)keep((size_t)n * sizeof(float));
            float *r = (float *)keep((size_t)n * sizeof(float));
            ds_note_on(c, 41, 127);
            render(c, l, r, n, 128);
            const int skip = (int)(0.1f * rates[ri]);
            const double f = measure_freq(l + skip, n - skip, rates[ri]);
            const double want = 440.0 * pow(2.0, (double)semis[si] / 12.0);
            const double err = fabs(cents(f, want));
            if (err > worst)
                worst = err;
            CHECK(err < 5.0, "rate %.0f pitch %+.1f: measured %.3f Hz, wanted %.3f Hz (%.2f cents)", (double)rates[ri],
                  (double)semis[si], f, want, err);
            free(c);
        }
    }
    printf("  pitch accuracy: worst error %.3f cents over 25 rate/pitch combinations\n", worst);
    free_kept();
}

static void test_bend(void)
{
    ds_t *c = new_core(44100.0f);
    load_sine(c, 0, 440.0, 3.0, 44100.0f, 0.5f);
    const int n = 44100;
    float *l = (float *)keep((size_t)n * sizeof(float));
    float *r = (float *)keep((size_t)n * sizeof(float));
    ds_pitch_bend(c, 1.0f);
    ds_note_on(c, 36, 127);
    render(c, l, r, n, 128);
    double f = measure_freq(l + 4410, n - 4410, 44100.0);
    CHECK(fabs(cents(f, 440.0 * pow(2.0, 2.0 / 12.0))) < 5.0, "bend +1 at range 2 is +2 semitones, got %.2f Hz", f);
    ds_pitch_bend(c, -0.5f);
    render(c, l, r, n / 2, 128);   /* let it settle */
    render(c, l, r, n / 2, 128);
    f = measure_freq(l, n / 2, 44100.0);
    CHECK(fabs(cents(f, 440.0 * pow(2.0, -1.0 / 12.0))) < 5.0, "bend -0.5 is -1 semitone, got %.2f Hz", f);
    free(c);
    free_kept();
}

static void test_polyphony_headroom(void)
{
    /* Worst case for level: eight voices of constant full-scale audio at the highest slot gain and level. */
    ds_t *c = new_core(48000.0f);
    const int n = 48000;
    float *dc = (float *)keep((size_t)n * sizeof(float));
    for (int i = 0; i < n; i++)
        dc[i] = 1.0f;
    ds_params p;
    ds_default_params(&p);
    p.level = 1.0f;
    ds_set_params(c, &p);
    for (int s = 0; s < 12; s++) {
        ds_set_sample(c, s, dc, NULL, n, 48000.0f);
        ds_slot_params sp = c->slot_params[s];
        sp.gain = 2.0f;
        ds_set_slot_params(c, s, &sp);
    }
    float *l = (float *)keep((size_t)n / 2 * sizeof(float));
    float *r = (float *)keep((size_t)n / 2 * sizeof(float));
    for (int s = 0; s < 12; s++)   /* twelve notes: four more than the voice limit */
        ds_note_on(c, 36 + s, 127);
    CHECK(ds_active_voices(c) <= DS_VOICES, "voice count is bounded");
    render(c, l, r, n / 2, 100);
    const double pk = fmax(peak_abs(l, n / 2), peak_abs(r, n / 2));
    CHECK(all_finite(l, n / 2) && all_finite(r, n / 2), "no NaN or inf with 12 notes");
    CHECK(pk < 1.0, "peak %.5f must stay under 0 dBFS", pk);
    CHECK(pk > 0.9, "the ceiling should actually be reached by this loud case, got %.4f", pk);
    printf("  worst-case peak with 8 voices of full-scale DC at gain 2, level 1: %.5f\n", pk);
    free(c);
    free_kept();

    /* Normal case: the default kit, all 24 slots struck at once, default settings. */
    c = new_core(44100.0f);
    for (int s = 0; s < DS_SLOTS; s++) {
        float *buf = (float *)keep(DS_KIT_MAX_FRAMES * sizeof(float));
        const int f = ds_kit_render(s, buf, DS_KIT_MAX_FRAMES);
        ds_set_sample(c, s, buf, NULL, f, (float)DS_KIT_RATE);
        set_mode(c, s, DS_MODE_ONESHOT);
        ds_note_on(c, 36 + s, 127);
    }
    float *kl = (float *)keep(44100 * sizeof(float));
    float *kr = (float *)keep(44100 * sizeof(float));
    render(c, kl, kr, 44100, 128);
    const double kp = fmax(peak_abs(kl, 44100), peak_abs(kr, 44100));
    CHECK(all_finite(kl, 44100) && kp < 1.0 && kp > 0.1, "kit chord peak %.4f", kp);
    free(c);
    free_kept();
}

static void test_note_off_modes(void)
{
    const float sr = 44100.0f;
    const int n = (int)(3.0f * sr);
    float *l = (float *)keep((size_t)n * sizeof(float));
    float *r = (float *)keep((size_t)n * sizeof(float));

    /* Gate: plays while the key is down, releases on note off. */
    ds_t *c = new_core(sr);
    load_sine(c, 0, 440.0, 2.0, sr, 0.5f);
    set_mode(c, 0, DS_MODE_GATE);
    ds_note_on(c, 36, 100);
    render(c, l, r, (int)(0.3f * sr), 128);
    const double held = rms(l + (int)(0.2f * sr), (int)(0.1f * sr));
    ds_note_off(c, 36);
    render(c, l + (int)(0.3f * sr), r, n - (int)(0.3f * sr), 128);
    const int tail_from = (int)(0.3f * sr);
    CHECK(held > 0.05, "gate voice sounds while the key is down (rms %.4f)", held);
    CHECK(rms(l + tail_from + (int)(0.5f * sr), 4410) < 1e-4, "gate voice is silent 0.5 s after release (release 80 ms)");
    CHECK(ds_active_voices(c) == 0, "voice is freed after release");
    free(c);

    /* One-shot: key up changes nothing, the sample plays to its end and stops. */
    c = new_core(sr);
    load_sine(c, 0, 440.0, 0.5, sr, 0.5f);
    ds_note_on(c, 36, 100);
    render(c, l, r, (int)(0.1f * sr), 128);
    ds_note_off(c, 36);
    render(c, l + (int)(0.1f * sr), r, n - (int)(0.1f * sr), 128);
    const double mid = rms(l + (int)(0.3f * sr), (int)(0.1f * sr));
    const int end = last_above(l, n, 1e-4f);
    CHECK(mid > 0.05, "one-shot keeps playing after note off (rms %.4f)", mid);
    CHECK(end > (int)(0.49f * sr) && end < (int)(0.52f * sr), "one-shot ends with its sample, last sound at %.3f s", (double)end / sr);
    ds_note_on(c, 36, 100);
    render(c, l, r, (int)(0.1f * sr), 128);
    ds_all_off(c);
    render(c, l, r, (int)(1.0f * sr), 128);
    CHECK(rms(l + (int)(0.5f * sr), 4410) < 1e-4, "all-off releases even a one-shot");
    free(c);

    /* Loop: a 0.2 s sample sustains for 2 s while held, then releases. */
    c = new_core(sr);
    load_sine(c, 0, 440.0, 0.2, sr, 0.5f);
    set_mode(c, 0, DS_MODE_LOOP);
    ds_note_on(c, 36, 100);
    render(c, l, r, (int)(2.0f * sr), 128);
    const double late = rms(l + (int)(1.8f * sr), (int)(0.1f * sr));
    CHECK(late > 0.05, "loop sustains while held (rms %.4f at 1.8 s)", late);
    ds_note_off(c, 36);
    render(c, l, r, (int)(1.0f * sr), 128);
    CHECK(rms(l + (int)(0.6f * sr), 4410) < 1e-4, "loop voice is silent after release");

    /* Sustain pedal holds a released voice (loop mode, so the 0.2 s sample does not end first) until the pedal comes up. */
    set_mode(c, 0, DS_MODE_LOOP);
    ds_sustain_pedal(c, 1);
    ds_note_on(c, 36, 100);
    render(c, l, r, (int)(0.1f * sr), 128);
    ds_note_off(c, 36);
    render(c, l, r, (int)(0.5f * sr), 128);
    CHECK(rms(l + (int)(0.4f * sr), 4410) > 0.02, "pedal keeps the voice sounding after the key is up");
    ds_sustain_pedal(c, 0);
    render(c, l, r, (int)(0.8f * sr), 128);
    CHECK(rms(l + (int)(0.5f * sr), 4410) < 1e-4, "lifting the pedal releases it");
    free(c);
    free_kept();
}

static void test_no_click_retrigger_and_steal(void)
{
    const float sr = 44100.0f;
    const int n = (int)(0.6f * sr);
    float *l = (float *)keep((size_t)n * sizeof(float));
    float *r = (float *)keep((size_t)n * sizeof(float));

    /* Retrigger a looping 440 Hz sine at many different phases. Loop mode makes any step in the output come from the
     * retrigger itself. The bound: one voice fading out and one fading in, each bounded by its own sine slope plus
     * the slope of its fade. I allow 4x the slope of a single voice, a hard cut would be about 16x. */
    ds_t *c = new_core(sr);
    load_sine(c, 0, 440.0, 1.0, sr, 0.5f);
    set_mode(c, 0, DS_MODE_LOOP);
    ds_note_on(c, 36, 127);
    render(c, l, r, 4410, 64);
    const double steady = max_step(l, 2000, 4410);
    double worst = 0.0;
    int at = 4410;
    for (int k = 0; k < 12; k++) {
        const int gap = 700 + 37 * k;   /* distinct phases of the 440 Hz sine, not on zero crossings */
        render(c, l + at, r + at, gap, 64);
        at += gap;
        ds_note_on(c, 36, 127);
        render(c, l + at, r + at, 300, 64);
        const double m = max_step(l, at - 1, at + 300);
        if (m > worst)
            worst = m;
        at += 300;
    }
    CHECK(all_finite(l, at), "retrigger output finite");
    CHECK(worst <= 4.0 * steady, "retrigger step %.5f vs steady slope %.5f (limit 4x)", worst, steady);
    printf("  retrigger: worst step %.5f = %.2fx the steady-state slope %.5f\n", worst, worst / steady, steady);
    free(c);

    /* Steal: nine slots with the same looping sine, struck 3 ms apart so the ninth steals the oldest. Level is low so the
     * sum of eight stays in the linear part of the output stage. */
    c = new_core(sr);
    ds_params p;
    ds_default_params(&p);
    p.level = 0.3f;
    ds_set_params(c, &p);
    for (int s = 0; s < 10; s++) {
        load_sine(c, s, 440.0, 1.0, sr, 0.5f);
        set_mode(c, s, DS_MODE_LOOP);
    }
    int pos = 0;
    for (int s = 0; s < 8; s++) {
        ds_note_on(c, 36 + s, 127);
        render(c, l + pos, r + pos, 131, 64);
        pos += 131;
    }
    render(c, l + pos, r + pos, 3000, 64);
    pos += 3000;
    const double ref8 = max_step(l, pos - 2000, pos);
    ds_note_on(c, 36 + 8, 127);   /* steal */
    render(c, l + pos, r + pos, 600, 64);
    const double after_steal = max_step(l, pos - 1, pos + 600);
    pos += 600;
    ds_note_on(c, 36 + 9, 127);   /* and again */
    render(c, l + pos, r + pos, 600, 64);
    const double after_steal2 = max_step(l, pos - 1, pos + 600);
    const double one_voice = steady * (double)(0.3f * 0.3f); /* a single voice at this level (level squared) */
    const double limit = ref8 + 4.0 * one_voice;
    CHECK(after_steal <= limit && after_steal2 <= limit, "steal steps %.5f / %.5f vs limit %.5f (steady 8-voice %.5f)", after_steal,
          after_steal2, limit, ref8);
    CHECK(ds_active_voices(c) <= DS_VOICES, "voice bound after steals");
    printf("  steal: steps %.5f and %.5f against limit %.5f (steady 8-voice slope %.5f)\n", after_steal, after_steal2, limit, ref8);
    free(c);
    free_kept();
}

static void test_sample_replaced_while_playing(void)
{
    /* The caller frees the old audio right after ds_set_sample. ASan fails this test if the core still reads it. */
    const float sr = 44100.0f;
    ds_t *c = new_core(sr);
    const int n = 22050;
    float *a = (float *)malloc((size_t)n * sizeof(float));
    for (int i = 0; i < n; i++)
        a[i] = 0.5f * (float)sin(2.0 * PI * 440.0 * (double)i / sr);
    ds_set_sample(c, 2, a, NULL, n, sr);
    set_mode(c, 2, DS_MODE_LOOP);
    ds_note_on(c, 38, 127);
    float *l = (float *)keep(8192 * sizeof(float));
    float *r = (float *)keep(8192 * sizeof(float));
    render(c, l, r, 1000, 100);
    float *b = (float *)malloc((size_t)n * sizeof(float));
    for (int i = 0; i < n; i++)
        b[i] = 0.5f * (float)sin(2.0 * PI * 880.0 * (double)i / sr);
    ds_set_sample(c, 2, b, NULL, n, sr);
    free(a);
    render(c, l + 1000, r + 1000, 3000, 100);
    CHECK(all_finite(l, 4000), "output finite after swapping the sample under a playing voice");
    const double steady = max_step(l, 200, 1000);
    const double swap = max_step(l, 999, 1100);
    CHECK(swap <= 3.0 * steady, "swap step %.5f vs steady slope %.5f", swap, steady);
    /* The voice that was playing the old audio dies with its fade; a new note plays the new audio. */
    CHECK(rms(l + 1500, 2000) < 1e-3, "the old voice fades out and does not carry on");
    ds_note_on(c, 38, 127);
    render(c, l + 4000, r + 4000, 4000, 100);
    const double f = measure_freq(l + 4400, 3500, sr);
    CHECK(fabs(cents(f, 880.0)) < 10.0, "new note plays the new audio, %.1f Hz", f);
    ds_set_sample(c, 2, NULL, NULL, 0, sr);   /* empty the slot while it plays */
    free(b);
    render(c, l, r, 2000, 100);
    ds_note_on(c, 38, 127);
    ds_render(c, l, r, 128);
    CHECK(peak_abs(l, 128) == 0.0 || ds_active_voices(c) == 0, "an emptied slot makes no sound");
    free(c);
    free_kept();
}

static void test_knobs(void)
{
    const float sr = 44100.0f;
    const int n = (int)(1.0f * sr);
    float *l = (float *)keep((size_t)n * sizeof(float));
    float *r = (float *)keep((size_t)n * sizeof(float));

    /* A two-part sample: 440 Hz for the first half second, 880 Hz for the second. */
    const int half = (int)(0.5f * sr);
    float *two = (float *)keep((size_t)(2 * half) * sizeof(float));
    for (int i = 0; i < half; i++) {
        two[i] = 0.5f * (float)sin(2.0 * PI * 440.0 * (double)i / sr);
        two[half + i] = 0.5f * (float)sin(2.0 * PI * 880.0 * (double)i / sr);
    }

    ds_t *c = new_core(sr);
    ds_set_sample(c, 0, two, NULL, 2 * half, sr);

    /* IN: starting at the middle skips the 440 Hz half. */
    ds_note_on(c, 36, 127);
    render(c, l, r, 8000, 128);
    double f0 = measure_freq(l + 500, 7000, sr);
    ds_reset(c);
    ds_slot_params sp = c->slot_params[0];
    sp.in = 0.5f;
    ds_set_slot_params(c, 0, &sp);
    ds_note_on(c, 36, 127);
    render(c, l, r, 8000, 128);
    double f1 = measure_freq(l + 500, 7000, sr);
    CHECK(fabs(cents(f0, 440.0)) < 10.0 && fabs(cents(f1, 880.0)) < 10.0, "IN moves the start: %.1f Hz then %.1f Hz", f0, f1);

    /* OUT: ending at the middle cuts the sound at 0.5 s. */
    ds_reset(c);
    sp.in = 0.0f;
    sp.out = 0.5f;
    ds_set_slot_params(c, 0, &sp);
    ds_note_on(c, 36, 127);
    render(c, l, r, n, 128);
    int end = last_above(l, n, 1e-4f);
    CHECK(end > half - 200 && end < half + 100, "OUT 0.5 ends the sound at 0.5 s, last sound at %d of %d", end, half);

    /* IN above OUT swaps the two. */
    ds_reset(c);
    sp.in = 0.5f;
    sp.out = 0.0f;
    ds_set_slot_params(c, 0, &sp);
    ds_note_on(c, 36, 127);
    render(c, l, r, n, 128);
    end = last_above(l, n, 1e-4f);
    CHECK(end > half - 200 && end < half + 100, "IN above OUT plays the region between them, last sound at %d", end);

    /* REVERSE: the 880 Hz half comes first. */
    ds_reset(c);
    sp.in = 0.0f;
    sp.out = 1.0f;
    sp.reverse = 1;
    ds_set_slot_params(c, 0, &sp);
    ds_note_on(c, 36, 127);
    render(c, l, r, 8000, 128);
    f1 = measure_freq(l + 500, 7000, sr);
    CHECK(fabs(cents(f1, 880.0)) < 10.0, "REVERSE starts from the end: %.1f Hz", f1);
    sp.reverse = 0;
    ds_set_slot_params(c, 0, &sp);

    /* GAIN: 2x is +6.02 dB. Low level keeps the output stage linear. */
    ds_params p;
    ds_default_params(&p);
    p.level = 0.3f;
    ds_set_params(c, &p);
    ds_reset(c);
    ds_note_on(c, 36, 127);
    render(c, l, r, 4000, 128);
    const double r1 = rms(l + 1000, 2000);
    ds_reset(c);
    sp.gain = 2.0f;
    ds_set_slot_params(c, 0, &sp);
    ds_note_on(c, 36, 127);
    render(c, l, r, 4000, 128);
    const double r2 = rms(l + 1000, 2000);
    CHECK(fabs(r2 / r1 - 2.0) < 0.02, "GAIN 2 doubles the level, ratio %.4f", r2 / r1);
    sp.gain = 1.0f;
    ds_set_slot_params(c, 0, &sp);

    /* Velocity changes the level too, from 25% at velocity 1 to 100% at 127. */
    ds_reset(c);
    ds_note_on(c, 36, 1);
    render(c, l, r, 4000, 128);
    const double rv = rms(l + 1000, 2000);
    CHECK(rv / r1 > 0.24 && rv / r1 < 0.28, "velocity 1 plays at about a quarter, ratio %.3f", rv / r1);

    /* ATTACK: a slow attack leaves the start quiet. */
    ds_default_params(&p);
    p.level = 0.5f;
    p.attack = 0.2f;
    ds_set_params(c, &p);
    ds_reset(c);
    set_mode(c, 0, DS_MODE_LOOP);
    ds_note_on(c, 36, 127);
    render(c, l, r, n, 128);
    CHECK(rms(l + 441, 441) < 0.2 * rms(l + (int)(0.5f * sr), 441), "ATTACK 0.2 s starts quiet: %.4f vs %.4f", rms(l + 441, 441),
          rms(l + (int)(0.5f * sr), 441));

    /* DECAY and SUSTAIN: the level settles at the sustain value. */
    ds_default_params(&p);
    p.level = 0.5f;
    p.decay = 0.05f;
    p.sustain = 0.25f;
    ds_set_params(c, &p);
    ds_reset(c);
    ds_note_on(c, 36, 127);
    render(c, l, r, n, 128);
    ds_params q;
    ds_default_params(&q);
    q.level = 0.5f;
    ds_set_params(c, &q);
    ds_reset(c);
    float *l2 = (float *)keep((size_t)n * sizeof(float));
    ds_note_on(c, 36, 127);
    render(c, l2, r, n, 128);
    /* the loop is a 440/880 mix, so compare envelope at one point of equal loop phase: same sample position in both runs */
    const double ratio = rms(l + (int)(0.4f * sr), 2205) / rms(l2 + (int)(0.4f * sr), 2205);
    CHECK(fabs(ratio - 0.25) < 0.03, "SUSTAIN 0.25 settles at a quarter, ratio %.4f", ratio);

    /* RELEASE: 60 dB down after the release time (gate mode, 100 ms). */
    ds_default_params(&p);
    p.level = 1.0f;
    p.release = 0.1f;
    ds_set_params(c, &p);
    ds_reset(c);
    set_mode(c, 0, DS_MODE_GATE);
    ds_note_on(c, 36, 127);
    render(c, l, r, 4410, 128);
    ds_note_off(c, 36);
    render(c, l + 4410, r + 4410, 8820, 128);
    const double before = rms(l + 4410 - 441, 441);
    const double after = rms(l + 4410 + 4410 - 220, 441);   /* 100 ms after note off, short window */
    const double db = 20.0 * log10(after / before);
    CHECK(db < -50.0 && db > -70.0, "RELEASE 100 ms is about -60 dB after 100 ms, got %.1f dB", db);
    free(c);
    free_kept();
}

static void test_loop_seam(void)
{
    /* A looping sine whose length is not a whole number of cycles: the crossfade must keep the seam free of steps. */
    const float sr = 44100.0f;
    ds_t *c = new_core(sr);
    const int len = 7777;
    float *s = (float *)keep((size_t)len * sizeof(float));
    for (int i = 0; i < len; i++)
        s[i] = 0.5f * (float)sin(2.0 * PI * 441.3 * (double)i / sr);
    ds_set_sample(c, 0, s, NULL, len, sr);
    set_mode(c, 0, DS_MODE_LOOP);
    ds_note_on(c, 36, 127);
    const int n = 5 * len;
    float *l = (float *)keep((size_t)n * sizeof(float));
    float *r = (float *)keep((size_t)n * sizeof(float));
    render(c, l, r, n, 128);
    const double steady = 0.5 * 2.0 * PI * 441.3 / sr * (0.85 * 0.85);
    const double m = max_step(l, 2000, n);
    CHECK(m < 1.2 * steady, "loop seam step %.5f vs single sine slope %.5f", m, steady);
    free(c);
    free_kept();
}

static void test_deterministic_and_block_size(void)
{
    const float sr = 48000.0f;
    const int total = 48000 * 2;
    float *ref_l = (float *)keep((size_t)total * sizeof(float));
    float *ref_r = (float *)keep((size_t)total * sizeof(float));
    float *tl = (float *)keep((size_t)total * sizeof(float));
    float *tr = (float *)keep((size_t)total * sizeof(float));
    const int blocks[] = {512, 1, 7, 33, 128, 4096, 48000 * 2};
    for (size_t bi = 0; bi < sizeof blocks / sizeof blocks[0]; bi++) {
        ds_t *c = new_core(sr);
        for (int s = 0; s < 6; s++) {
            float *buf = (float *)keep(DS_KIT_MAX_FRAMES * sizeof(float));
            const int f = ds_kit_render(s, buf, DS_KIT_MAX_FRAMES);
            ds_set_sample(c, s, buf, NULL, f, (float)DS_KIT_RATE);
            ds_slot_params sp = c->slot_params[s];
            sp.mode = s % 3;
            sp.pitch = (float)(s * 2 - 5);
            sp.reverse = s == 4;
            ds_set_slot_params(c, s, &sp);
        }
        float *out_l = bi == 0 ? ref_l : tl;
        float *out_r = bi == 0 ? ref_r : tr;
        /* events at fixed frames, applied between render calls, splitting calls at the event frames */
        const struct { int frame, type, a, b; } ev[] = {
            {0, 0, 36, 100}, {700, 0, 38, 90}, {5000, 3, 0, 0}, {5001, 0, 37, 127}, {9000, 2, 0, 0},
            {20000, 1, 36, 0}, {20500, 4, 1, 0}, {30001, 0, 40, 64}, {30001, 0, 41, 64}, {45000, 1, 38, 0},
            {50000, 5, 0, 0}, {60000, 0, 39, 120}, {70000, 6, 0, 0},
        };
        int cur = 0, ei = 0;
        const int nev = (int)(sizeof ev / sizeof ev[0]);
        while (cur < total) {
            while (ei < nev && ev[ei].frame <= cur) {
                switch (ev[ei].type) {
                case 0: ds_note_on(c, ev[ei].a, ev[ei].b); break;
                case 1: ds_note_off(c, ev[ei].a); break;
                case 2: ds_pitch_bend(c, 0.7f); break;
                case 3: ds_sustain_pedal(c, 1); break;
                case 4: ds_sustain_pedal(c, 0); break;
                case 5: ds_pitch_bend(c, -0.3f); break;
                case 6: ds_all_off(c); break;
                }
                ei++;
            }
            int k = total - cur;
            if (k > blocks[bi])
                k = blocks[bi];
            if (ei < nev && ev[ei].frame - cur < k)
                k = ev[ei].frame - cur;
            ds_render(c, out_l + cur, out_r + cur, k);
            cur += k;
        }
        if (bi == 0) {
            CHECK(peak_abs(ref_l, total) > 0.05, "determinism script makes sound");
        } else {
            CHECK(memcmp(ref_l, tl, (size_t)total * sizeof(float)) == 0 && memcmp(ref_r, tr, (size_t)total * sizeof(float)) == 0,
                  "output differs with block size %d", blocks[bi]);
        }
        free(c);
    }
    free_kept();
}

static unsigned lcg(unsigned *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

static void test_sample_rates_and_extremes(void)
{
    const float rates[] = {22050.0f, 32000.0f, 44100.0f, 48000.0f, 88200.0f, 96000.0f, 192000.0f};
    for (size_t ri = 0; ri < sizeof rates / sizeof rates[0]; ri++) {
        for (int mono = 0; mono < 2; mono++) {
            const float sr = rates[ri];
            ds_t *c = new_core(sr);
            for (int s = 0; s < DS_SLOTS; s++) {
                float *buf = (float *)keep(DS_KIT_MAX_FRAMES * sizeof(float));
                const int f = ds_kit_render(s, buf, DS_KIT_MAX_FRAMES);
                float *rr = NULL;
                if (!mono) {
                    rr = (float *)keep((size_t)f * sizeof(float));
                    for (int i = 0; i < f; i++)
                        rr[i] = -buf[i];
                }
                ds_set_sample(c, s, buf, rr, f, (float)DS_KIT_RATE);
            }
            unsigned seed = 12345u + (unsigned)ri;
            const int total = (int)(2.0f * sr);
            float *l = (float *)keep((size_t)total * sizeof(float));
            float *r = (float *)keep((size_t)total * sizeof(float));
            int cur = 0;
            int ok = 1;
            double pk = 0.0;
            while (cur < total) {
                /* random events: extreme slot settings included */
                const unsigned e = lcg(&seed) % 10;
                const int slot = (int)(lcg(&seed) % DS_SLOTS);
                if (e < 5) {
                    ds_slot_params sp;
                    sp.pitch = (float)((int)(lcg(&seed) % 49) - 24);
                    sp.in = (float)(lcg(&seed) % 101) / 100.0f;
                    sp.out = (float)(lcg(&seed) % 101) / 100.0f;
                    sp.mode = (int)(lcg(&seed) % 3);
                    sp.reverse = (int)(lcg(&seed) % 2);
                    sp.gain = (float)(lcg(&seed) % 201) / 100.0f;
                    ds_set_slot_params(c, slot, &sp);
                    ds_note_on(c, 36 + slot, 1 + (int)(lcg(&seed) % 127));
                } else if (e < 7) {
                    ds_note_off(c, 36 + slot);
                } else if (e == 7) {
                    ds_pitch_bend(c, (float)((int)(lcg(&seed) % 201) - 100) / 100.0f);
                } else if (e == 8) {
                    ds_sustain_pedal(c, (int)(lcg(&seed) % 2));
                } else if (lcg(&seed) % 8 == 0) {
                    ds_all_off(c);
                }
                int k = 1 + (int)(lcg(&seed) % 400);
                if (k > total - cur)
                    k = total - cur;
                ds_render(c, l + cur, r + cur, k);
                cur += k;
            }
            ok = all_finite(l, total) && all_finite(r, total);
            pk = fmax(peak_abs(l, total), peak_abs(r, total));
            CHECK(ok, "rate %.0f %s: NaN or inf in random run", (double)sr, mono ? "mono" : "stereo");
            CHECK(pk < 1.0, "rate %.0f %s: peak %.4f must stay under 1", (double)sr, mono ? "mono" : "stereo", pk);
            CHECK(ds_active_voices(c) <= DS_VOICES, "voice count bounded");
            free(c);
            free_kept();
        }
    }
    printf("  random event runs at 7 sample rates, mono and stereo: finite, under 0 dBFS\n");

    /* block size 1 for a stretch */
    ds_t *c = new_core(44100.0f);
    float *buf = (float *)keep(DS_KIT_MAX_FRAMES * sizeof(float));
    const int f = ds_kit_render(0, buf, DS_KIT_MAX_FRAMES);
    ds_set_sample(c, 0, buf, NULL, f, (float)DS_KIT_RATE);
    ds_note_on(c, 36, 100);
    float l, r;
    double pk = 0.0;
    for (int i = 0; i < 4000; i++) {
        ds_render(c, &l, &r, 1);
        if (!isfinite(l) || !isfinite(r))
            pk = 9.0;
        if (fabs((double)l) > pk)
            pk = fabs((double)l);
    }
    CHECK(pk > 0.05 && pk < 1.0, "block size 1 renders the kit kick, peak %.3f", pk);
    free(c);
    free_kept();
}

static void test_reset_and_rate_change(void)
{
    ds_t *c = new_core(44100.0f);
    load_sine(c, 0, 440.0, 1.0, 44100.0f, 0.5f);
    set_mode(c, 0, DS_MODE_LOOP);
    ds_slot_params sp = c->slot_params[0];
    sp.pitch = 5.0f;
    ds_set_slot_params(c, 0, &sp);
    ds_note_on(c, 36, 100);
    float l[1024], r[1024];
    ds_render(c, l, r, 1024);
    ds_reset(c);
    ds_render(c, l, r, 1024);
    CHECK(peak_abs(l, 1024) == 0.0 && ds_active_voices(c) == 0, "reset silences at once");
    CHECK(c->slot_params[0].pitch == 5.0f && c->sample[0].frames > 0, "reset keeps settings and samples");
    ds_note_on(c, 36, 100);
    ds_set_sample_rate(c, 96000.0f);
    CHECK(ds_active_voices(c) == 0 && c->sample[0].frames > 0, "a sample rate change drops the voices and keeps the samples");
    ds_note_on(c, 36, 100);
    ds_render(c, l, r, 1024);
    CHECK(peak_abs(l, 1024) > 0.01, "plays after a sample rate change");
    free(c);
    free_kept();
}

static void test_activity(void)
{
    const float sr = 44100.0f;
    ds_t *c = new_core(sr);
    load_sine(c, 3, 440.0, 1.0, sr, 0.5f);
    load_sine(c, 7, 220.0, 1.0, sr, 0.5f);
    ds_activity a;
    ds_take_activity(c, &a);
    CHECK(a.voices == 0 && a.playing == 0 && a.hits == 0 && a.last_slot == -1 && a.head[3] < 0.0f, "idle activity");
    ds_note_on(c, 39, 100);
    float l[4410], r[4410];
    ds_render(c, l, r, 4410);
    ds_take_activity(c, &a);
    CHECK(a.voices == 1 && (a.playing & (1u << 3)) && a.hits == (1u << 3) && a.last_slot == 3 && a.notes == 1, "activity after a note");
    CHECK(a.peak > 0.01f && a.head[3] > 0.05f && a.head[3] < 0.15f && a.head[7] < 0.0f, "peak %.3f playhead %.3f", (double)a.peak, (double)a.head[3]);
    ds_take_activity(c, &a);
    CHECK(a.hits == 0 && a.peak == 0.0f, "hits and peak clear when read");
    ds_note_on(c, 39, 100);   /* retrigger: the old voice fades, the slot still counts as playing once */
    ds_note_on(c, 43, 100);
    ds_take_activity(c, &a);
    CHECK((a.playing & (1u << 3)) && (a.playing & (1u << 7)) && a.hits == ((1u << 3) | (1u << 7)) && a.last_slot == 7, "two slots");
    free(c);
    free_kept();
}

static void test_kit(void)
{
    static float a[DS_KIT_MAX_FRAMES], b[DS_KIT_MAX_FRAMES];
    CHECK(ds_kit_frames(-1) == 0 && ds_kit_frames(DS_KIT_SOUNDS) == 0 && ds_kit_name(99)[0] == 0, "bad kit index");
    CHECK(ds_kit_render(0, a, 10) == 0, "too small a buffer renders nothing");
    for (int i = 0; i < DS_KIT_SOUNDS; i++) {
        const int n = ds_kit_render(i, a, DS_KIT_MAX_FRAMES);
        const int m = ds_kit_render(i, b, DS_KIT_MAX_FRAMES);
        CHECK(n == ds_kit_frames(i) && n > 2000 && n <= DS_KIT_MAX_FRAMES, "kit %d (%s) length %d", i, ds_kit_name(i), n);
        CHECK(n == m && memcmp(a, b, (size_t)n * sizeof(float)) == 0, "kit %d is deterministic", i);
        CHECK(all_finite(a, n), "kit %d is finite", i);
        const double pk = peak_abs(a, n);
        CHECK(fabs(pk - 0.9) < 0.002, "kit %d (%s) peak %.4f should be 0.9", i, ds_kit_name(i), pk);
        CHECK(fabsf(a[n - 1]) < 1e-3f, "kit %d (%s) ends at zero, last sample %.5f", i, ds_kit_name(i), (double)a[n - 1]);
        CHECK(rms(a, n) > 0.01, "kit %d (%s) is not near silent: rms %.4f", i, ds_kit_name(i), rms(a, n));
        CHECK(ds_kit_name(i)[0] != 0, "kit %d has a name", i);
        for (int j = 0; j < i; j++)
            CHECK(strcmp(ds_kit_name(i), ds_kit_name(j)) != 0, "kit names %d and %d are the same", i, j);
    }
    /* the sounds are different from each other */
    for (int i = 1; i < DS_KIT_SOUNDS; i++) {
        ds_kit_render(i - 1, a, DS_KIT_MAX_FRAMES);
        ds_kit_render(i, b, DS_KIT_MAX_FRAMES);
        const int n = ds_kit_frames(i) < ds_kit_frames(i - 1) ? ds_kit_frames(i) : ds_kit_frames(i - 1);
        CHECK(memcmp(a, b, (size_t)n * sizeof(float)) != 0, "kit %d differs from %d", i, i - 1);
    }
    printf("  kit: 24 sounds, deterministic, peak 0.9, end at zero\n");
}

#ifdef DS_TIMING_ONLY
static void bench(void)
{
    ds_t *c = new_core(44100.0f);
    static float kitbuf[DS_SLOTS][DS_KIT_MAX_FRAMES];
    for (int s = 0; s < DS_SLOTS; s++) {
        const int f = ds_kit_render(s, kitbuf[s], DS_KIT_MAX_FRAMES);
        ds_set_sample(c, s, kitbuf[s], NULL, f, (float)DS_KIT_RATE);
        set_mode(c, s, DS_MODE_LOOP);
    }
    float l[128], r[128];
    const int blocks = 44100 * 20 / 128;
    for (int s = 0; s < 8; s++)
        ds_note_on(c, 36 + s, 127);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int b = 0; b < blocks; b++)
        ds_render(c, l, r, 128);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double sec = (double)(t1.tv_sec - t0.tv_sec) + 1e-9 * (double)(t1.tv_nsec - t0.tv_nsec);
    const double per_block_ms = sec * 1000.0 / blocks;
    printf("  8 looping voices: %.4f ms per 128-frame block = %.2f%% of the 2.9 ms budget\n", per_block_ms, per_block_ms / 2.9 * 100.0);
    free(c);
}
#endif

int main(void)
{
#ifdef DS_TIMING_ONLY
    printf("DrumSampler core timing (host CPU, -O2, not the MPC):\n");
    bench();
    return 0;
#else
    printf("DrumSampler core tests\n");
    test_defaults_and_mappings();
    test_silence_and_ignored_notes();
    test_pitch_accuracy();
    test_bend();
    test_polyphony_headroom();
    test_note_off_modes();
    test_no_click_retrigger_and_steal();
    test_sample_replaced_while_playing();
    test_knobs();
    test_loop_seam();
    test_deterministic_and_block_size();
    test_sample_rates_and_extremes();
    test_reset_and_rate_change();
    test_activity();
    test_kit();
    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("ALL PASSED\n");
    return 0;
#endif
}
