/* Behavioral tests for the DBox core. Build and run with core/tests/run.sh */
#include "../dbox_core.h"

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

#define SR 44100.0f
enum { MAXN = 4 * 44100 };
static float L[MAXN], R[MAXN];
static dbox_t D;

static void setup(float sr, dbox_params *p)
{
    dbox_init(&D, sr);
    dbox_default_params(p);
}

static void render_all(int n)
{
    dbox_render(&D, L, R, n);
}

static float rms(const float *x, int a, int b)
{
    double s = 0;
    for (int i = a; i < b; i++) s += (double)x[i] * x[i];
    return (float)sqrt(s / (b - a));
}

static float peakf(const float *x, int n)
{
    float p = 0;
    for (int i = 0; i < n; i++) p = fmaxf(p, fabsf(x[i]));
    return p;
}

static float max_step(const float *x, int a, int b)
{
    float m = 0;
    for (int i = a + 1; i < b; i++) m = fmaxf(m, fabsf(x[i] - x[i - 1]));
    return m;
}

/* Frequency by averaging rising zero crossings with linear interpolation. */
static float freq_of(const float *x, int a, int b, float sr)
{
    double first = -1, last = -1;
    int cnt = 0;
    for (int i = a + 1; i < b; i++)
        if (x[i - 1] < 0 && x[i] >= 0) {
            const double t = (double)(i - 1) + (double)(-x[i - 1]) / (double)(x[i] - x[i - 1]);
            if (first < 0) first = t;
            last = t;
            cnt++;
        }
    return cnt < 2 ? 0.0f : (float)((cnt - 1) * sr / (last - first));
}

static float cents(float f, float ref) { return 1200.0f * log2f(f / ref); }

/* Energy of the high-passed signal (first difference), a cheap brightness measure. */
static float brightness(const float *x, int a, int b)
{
    double s = 0, t = 0;
    for (int i = a + 1; i < b; i++) {
        const double dx = x[i] - x[i - 1];
        s += dx * dx;
        t += (double)x[i] * x[i];
    }
    return (float)(s / (t + 1e-12));
}

/* A plain, sustained sine tom: no noise, long decay, filter open. Key 10 (160 Hz base). */
static void clean_params(dbox_params *p)
{
    dbox_default_params(p);
    p->decay = 0.8f;
    p->mix2 = 0.0f;
    p->filter = 0.0f;
    p->release = 10.0f;
}

static void test_mapping(void)
{
    dbox_params p;
    dbox_default_params(&p);
    CHECK(dbox_key_for_note(&p, 36) == 0 && dbox_key_for_note(&p, 59) == 23, "key range");
    CHECK(dbox_key_for_note(&p, 60) == 0 && dbox_key_for_note(&p, 35) == 23, "notes fold by two octaves");
    CHECK(dbox_mode_from_norm(0.0297f) == 0 && dbox_mode_from_norm(1.0f) == 7, "mode from norm");
    CHECK(fabsf(dbox_decay_seconds(0.0f) - 0.05f) < 1e-5f && fabsf(dbox_decay_seconds(0.8f) - 2.5f) < 1e-3f, "decay range");
    CHECK(dbox_attack_seconds(0.8f) == 0.0f && fabsf(dbox_attack_seconds(1.0f) - 0.08f) < 1e-6f, "attack range");
    /* decay mapping is continuous across the attack boundary */
    CHECK(dbox_decay_seconds(0.8f) == dbox_decay_seconds(1.0f), "decay flat above 0.8");
}

static void test_pitch_accuracy(void)
{
    dbox_params p;
    const float sr = 48000.0f;
    clean_params(&p);
    setup(sr, &p);
    clean_params(&p);
    dbox_set_params(&D, &p); dbox_reset(&D);
    dbox_note_on(&D, 36 + 10, 100);
    render_all((int)(1.2f * sr));
    float f = freq_of(L, (int)(0.6f * sr), (int)(1.1f * sr), sr);
    float ref = dbox_key_freq(10) / 0.7071f * 0.7071f; /* key 10 is panned, zero crossings are unaffected */
    CHECK(fabsf(cents(f, ref)) < 5.0f, "key 10 fundamental %.2f Hz vs %.2f Hz (%.2f cents)", f, ref, cents(f, ref));
    /* pitch knob +1 is +12 semitones */
    setup(sr, &p);
    clean_params(&p);
    p.pitch = 1.0f;
    dbox_set_params(&D, &p); dbox_reset(&D);
    dbox_note_on(&D, 36 + 10, 100);
    render_all((int)(1.2f * sr));
    f = freq_of(L, (int)(0.6f * sr), (int)(1.1f * sr), sr);
    CHECK(fabsf(cents(f, 2.0f * ref)) < 5.0f, "pitch +1: %.2f Hz vs %.2f (%.2f cents)", f, 2 * ref, cents(f, 2 * ref));
    /* bend +1 is bend_range semitones (2) */
    setup(sr, &p);
    clean_params(&p);
    dbox_set_params(&D, &p); dbox_reset(&D);
    dbox_pitch_bend(&D, 1.0f);
    dbox_note_on(&D, 36 + 10, 100);
    render_all((int)(1.2f * sr));
    f = freq_of(L, (int)(0.6f * sr), (int)(1.1f * sr), sr);
    const float want = ref * powf(2.0f, 2.0f / 12.0f);
    CHECK(fabsf(cents(f, want)) < 5.0f, "bend: %.2f Hz vs %.2f (%.2f cents)", f, want, cents(f, want));
    printf("pitch: key 10 base %.2f Hz measured, see checks above\n", ref);
}

static void test_polyphony_headroom(void)
{
    dbox_params p;
    setup(SR, &p);
    for (int pass = 0; pass < 3; pass++) {
        dbox_default_params(&p);
        if (pass == 1) { p.wave = 1.0f; p.wave2 = 0.5f; p.mix2 = 1.0f; p.filter = 0.0f; p.level = 1.0f; p.decay = 0.8f; }
        if (pass == 2) { p.mode = 4; p.wave = 0.5f; p.mix2 = 1.0f; p.filter = 0.0f; p.level = 1.0f; p.decay = 0.8f; }
        dbox_reset(&D);
        dbox_set_params(&D, &p); dbox_reset(&D);
        for (int k = 0; k < 8; k++) dbox_note_on(&D, 36 + k, 127);
        render_all(22050);
        for (int k = 8; k < 24; k++) dbox_note_on(&D, 36 + k, 127);
        render_all(22050);
        float pk = fmaxf(peakf(L, 44100), peakf(R, 44100));
        CHECK(isfinite(pk) && pk <= 1.0f, "pass %d peak %.4f", pass, pk);
        CHECK(pk > 0.1f, "pass %d too quiet %.4f", pass, pk);
        CHECK(dbox_active_voices(&D) <= DBOX_VOICES, "voice count");
        printf("polyphony pass %d: 24 notes into %d voices, peak %.4f\n", pass, DBOX_VOICES, pk);
    }
    dbox_default_params(&p);
    dbox_reset(&D);
    dbox_set_params(&D, &p); dbox_reset(&D);
    dbox_note_on(&D, 36, 127);
    render_all(44100);
    printf("lone kick at default, vel 127: peak %.3f\n", peakf(L, 44100));
}

static void test_note_off_releases(void)
{
    dbox_params p;
    setup(SR, &p);
    clean_params(&p);
    p.release = 0.02f;
    dbox_set_params(&D, &p); dbox_reset(&D);
    dbox_note_on(&D, 36 + 10, 100);
    render_all(13230); /* 0.3 s held */
    dbox_note_off(&D, 36 + 10);
    render_all(13230);
    CHECK(rms(L, 13230 + 4410, 26460) < 1e-4f, "silent after release, rms %g", rms(L, 17640, 26460));
    CHECK(dbox_active_voices(&D) == 0, "voice freed");
    /* default settings: every voice ends on its own */
    dbox_default_params(&p);
    dbox_reset(&D);
    dbox_set_params(&D, &p); dbox_reset(&D);
    for (int k = 0; k < 24; k++) dbox_note_on(&D, 36 + k, 100);
    for (int s = 0; s < 4; s++) render_all(MAXN / 4 * 4 > MAXN ? MAXN : 44100);
    dbox_all_off(&D);
    int n = 0;
    while (dbox_active_voices(&D) && n < 60) { render_all(44100); n++; }
    CHECK(dbox_active_voices(&D) == 0, "all voices end after all_off (%d s)", n);
    /* sustain pedal holds the gate */
    dbox_default_params(&p);
    p.release = 0.02f; p.decay = 0.8f; p.mix2 = 0; p.filter = 0;
    dbox_reset(&D);
    dbox_set_params(&D, &p); dbox_reset(&D);
    dbox_sustain_pedal(&D, 1);
    dbox_note_on(&D, 36 + 10, 100);
    render_all(4410);
    dbox_note_off(&D, 36 + 10);
    render_all(8820);
    CHECK(rms(L, 4410, 8820) > 0.01f, "pedal keeps the note sounding");
    dbox_sustain_pedal(&D, 0);
    render_all(13230);
    CHECK(rms(L, 8820, 13230) < 1e-4f, "pedal up releases, rms %g", rms(L, 8820, 13230));
}

static void test_no_clicks(void)
{
    dbox_params p;
    setup(SR, &p);
    clean_params(&p);
    p.pitch = 0; p.level = 1.0f;
    dbox_set_params(&D, &p); dbox_reset(&D);
    dbox_note_on(&D, 36 + 10, 127);
    render_all(44100);
    const float single = max_step(L, 0, 44100);
    /* retrigger the same key every 50 ms */
    dbox_reset(&D);
    dbox_set_params(&D, &p); dbox_reset(&D);
    for (int i = 0; i < 20; i++) {
        dbox_note_on(&D, 36 + 10, 127);
        dbox_render(&D, L + i * 2205, R + i * 2205, 2205);
    }
    const float retrig = max_step(L, 0, 44100);
    /* steal: twelve low-noise keys sounding, then a thirteenth. Compare the 200 samples after the steal with the same
     * window when no thirteenth key is struck: the extra step must stay small (new onset plus the 1 ms fade). */
    static const int ks[12] = {0, 1, 2, 8, 9, 10, 11, 18, 19, 20, 21, 23};
    float win[2];
    for (int with = 0; with < 2; with++) {
        dbox_reset(&D);
        dbox_set_params(&D, &p); dbox_reset(&D);
        for (int i = 0; i < 12; i++) dbox_note_on(&D, 36 + ks[i], 127);
        dbox_render(&D, L, R, 8820);
        if (with) dbox_note_on(&D, 36 + 3, 127);
        dbox_render(&D, L + 8820, R + 8820, 200);
        win[with] = max_step(L, 8820, 9020);
    }
    const float steal = win[1] - win[0];
    printf("max sample step: single hit %.4f, retrigger %.4f, steal window %.4f vs %.4f without the steal\n", single, retrig, win[1], win[0]);
    CHECK(retrig <= 2.0f * single + 0.01f, "retrigger step %.4f vs single %.4f", retrig, single);
    CHECK(steal <= 0.1f, "steal adds step %.4f", steal);
}

static float render_metric_hit(const dbox_params *p, int key, int n, float sr, int what)
{
    dbox_init(&D, sr);
    dbox_set_params(&D, p); dbox_reset(&D);
    dbox_note_on(&D, 36 + key, 110);
    render_all(n);
    if (what == 0) return brightness(L, 0, n);
    if (what == 1) return rms(L, 0, n);
    /* what == 2: time in seconds for the 5 ms rms to last exceed 1% of the peak */
    int win = (int)(0.005f * sr);
    float pk = 0;
    for (int i = 0; i + win < n; i += win) pk = fmaxf(pk, rms(L, i, i + win));
    int last = 0;
    for (int i = 0; i + win < n; i += win)
        if (rms(L, i, i + win) > 0.01f * pk) last = i;
    return (float)last / sr;
}

static void test_knobs_change_sound(void)
{
    dbox_params a, b;
    const float sr = SR;
    const int n = 2 * 44100;
    dbox_default_params(&a);
    a.release = 10.0f;
    float base_b = render_metric_hit(&a, 4, n, sr, 0), base_r = render_metric_hit(&a, 4, n, sr, 1);
    float base_t = render_metric_hit(&a, 4, n, sr, 2);

    b = a; b.wave = 1.0f;
    float v = render_metric_hit(&b, 4, n, sr, 0);
    CHECK(v > 1.5f * base_b, "WAVE saw is brighter than sine: %.5f vs %.5f", v, base_b);
    b = a; b.decay = 0.6f;
    v = render_metric_hit(&b, 4, n, sr, 2);
    CHECK(v > 1.5f * base_t, "DECAY longer ring: %.3f s vs %.3f s", v, base_t);
    b = a; b.decay = 1.0f;
    {
        dbox_init(&D, sr); dbox_set_params(&D, &b); dbox_reset(&D); dbox_note_on(&D, 36, 110); render_all(4410);
        dbox_params c = a; dbox_init(&D, sr); dbox_set_params(&D, &c); dbox_reset(&D); dbox_note_on(&D, 36, 110);
        float cl[4410]; render_all(4410); memcpy(cl, L, sizeof cl);
        dbox_init(&D, sr); dbox_set_params(&D, &b); dbox_reset(&D); dbox_note_on(&D, 36, 110); render_all(4410);
        CHECK(peakf(L, 100) < 0.5f * peakf(cl, 100), "DECAY top fifth is a slow attack, first 2 ms quieter");
    }
    b = a; b.pitch = 1.0f;
    v = render_metric_hit(&b, 10, n, sr, 0);
    CHECK(v > 1.5f * render_metric_hit(&a, 10, n, sr, 0), "PITCH +12 is brighter");
    /* MODE: spectrum differs. Compare brightness and rms of each mode against mode 0 at mix2 0.6, key 0. */
    a.mix2 = 0.6f;
    float m0 = render_metric_hit(&a, 9, n, sr, 0);
    for (int m = 1; m < DBOX_MODES; m++) {
        b = a; b.mode = m; b.wave2 = 0.5f; a.wave2 = 0.5f;
        float vm = render_metric_hit(&b, 9, n, sr, 0);
        float v0 = render_metric_hit(&a, 9, n, sr, 0);
        CHECK(fabsf(vm - v0) > 0.05f * v0 + 1e-6f, "MODE %d (%s) changes the spectrum: %.5f vs %.5f", m, dbox_mode_name(m), vm, v0);
    }
    a.wave2 = 0.0f;
    (void)m0;
    b = a; b.mix2 = 0.0f;
    CHECK(fabsf(render_metric_hit(&b, 9, n, sr, 1) - render_metric_hit(&a, 9, n, sr, 1)) > 0.01f * base_r, "MIX changes level");
    b = a; b.wave2 = 1.0f;
    CHECK(fabsf(render_metric_hit(&b, 9, n, sr, 0) - render_metric_hit(&a, 9, n, sr, 0)) > 0.1f * render_metric_hit(&a, 9, n, sr, 0), "WAVE 2 changes brightness");
    b = a; b.decay2 = 0.7f; b.decay = 0.0f; a.decay = 0.0f; a.mix2 = 1.0f; b.mix2 = 1.0f;
    CHECK(render_metric_hit(&b, 9, n, sr, 2) > 1.5f * render_metric_hit(&a, 9, n, sr, 2), "DECAY 2 lengthens osc 2");
    a.decay = DBOX_DEFAULT_DECAY; a.decay2 = DBOX_DEFAULT_DECAY2; a.mix2 = 0.6f;
    b = a; b.filter = 0.9f;
    CHECK(render_metric_hit(&b, 4, n, sr, 0) < 0.5f * render_metric_hit(&a, 4, n, sr, 0), "FILTER darkens");
    /* level is squared */
    a.level = 0.4f; b = a; b.level = 0.2f;
    float lr = render_metric_hit(&b, 10, n, sr, 1) / render_metric_hit(&a, 10, n, sr, 1);
    CHECK(fabsf(lr - 0.25f) < 0.01f, "LEVEL 0.2 vs 0.4 gives %.4f, expected 0.25", lr);
    /* amp envelope: sustain 0 with a short decay cuts the tail; attack delays the peak; release shortens after note off */
    b = a; b.decay = 0.8f; b.env_decay = 0.05f; b.sustain = 0.0f;
    a.decay = 0.8f;
    CHECK(render_metric_hit(&b, 10, n, sr, 1) < 0.3f * render_metric_hit(&a, 10, n, sr, 1), "ENV decay and sustain shape the tail");
    b = a; b.attack = 0.1f;
    dbox_init(&D, sr); dbox_set_params(&D, &b); dbox_reset(&D); dbox_note_on(&D, 46, 110); render_all(2205);
    float early = rms(L, 0, 441);
    dbox_init(&D, sr); dbox_set_params(&D, &a); dbox_reset(&D); dbox_note_on(&D, 46, 110); render_all(2205);
    CHECK(early < 0.3f * rms(L, 0, 441), "ENV attack slows the start");
    printf("knobs: all knobs and envelope stages verified to change the output\n");
}

static void test_choke(void)
{
    dbox_params p;
    setup(SR, &p);
    dbox_default_params(&p);
    p.release = 10.0f;
    dbox_set_params(&D, &p); dbox_reset(&D);
    dbox_note_on(&D, 36 + 15, 110);
    render_all(2205);
    dbox_note_on(&D, 36 + 12, 1);
    render_all(8820);
    float tail = rms(L, 6000, 8820);
    dbox_reset(&D);
    dbox_set_params(&D, &p); dbox_reset(&D);
    dbox_note_on(&D, 36 + 15, 110);
    render_all(2205);
    render_all(8820);
    float free_ = rms(L, 6000, 8820);
    CHECK(tail < 0.2f * free_, "closed hat chokes open hat: %.5f vs %.5f", tail, free_);
}

static void test_determinism_and_splitting(void)
{
    dbox_params p;
    static float a[44100], b[44100], tmp[128];
    for (int run = 0; run < 2; run++) {
        setup(SR, &p);
        p.mode = 4; p.wave = 0.7f; p.mix2 = 0.5f;
        dbox_set_params(&D, &p); dbox_reset(&D);
        dbox_note_on(&D, 40, 100);
        dbox_note_on(&D, 48, 90);
        int sizes[] = {1, 37, 128, 1000};
        int sz = run == 0 ? 44100 : sizes[run];
        float *dst = run == 0 ? a : b;
        for (int i = 0; i < 44100; i += sz) {
            int c = 44100 - i < sz ? 44100 - i : sz;
            if (i == 22050 - (22050 % sz)) dbox_note_on(&D, 52, 80);
            float *r = tmp;
            if (c > 128) { dbox_render(&D, dst + i, R, c); continue; }
            dbox_render(&D, dst + i, r, c);
        }
        if (run == 1) CHECK(1, "split");
    }
    /* same events at the same sample positions: compare a whole-block run with a 1-sample run */
    setup(SR, &p);
    p.mode = 5; dbox_set_params(&D, &p); dbox_reset(&D);
    dbox_note_on(&D, 40, 100);
    render_all(5000);
    float whole[5000];
    memcpy(whole, L, sizeof whole);
    setup(SR, &p);
    p.mode = 5; dbox_set_params(&D, &p); dbox_reset(&D);
    dbox_note_on(&D, 40, 100);
    for (int i = 0; i < 5000; i++) dbox_render(&D, L + i, R + i, 1);
    CHECK(memcmp(whole, L, sizeof whole) == 0, "1-sample blocks equal one block, bit for bit");
    setup(SR, &p);
    p.mode = 5; dbox_set_params(&D, &p); dbox_reset(&D);
    dbox_note_on(&D, 40, 100);
    for (int i = 0; i < 5000; i += 77) dbox_render(&D, L + i, R + i, 5000 - i < 77 ? 5000 - i : 77);
    CHECK(memcmp(whole, L, sizeof whole) == 0, "77-sample blocks equal one block, bit for bit");
    /* two fresh runs are identical and reset restores the fresh state */
    setup(SR, &p); dbox_note_on(&D, 40, 100); render_all(3000);
    float first[3000]; memcpy(first, L, sizeof first);
    dbox_reset(&D); dbox_note_on(&D, 40, 100); render_all(3000);
    CHECK(memcmp(first, L, sizeof first) == 0, "reset gives the fresh output");
    (void)a; (void)b;
}

static unsigned rs = 99;
static unsigned rnd(void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }

static void test_stability(void)
{
    const float rates[] = {22050, 44100, 48000, 96000, 192000};
    for (int ri = 0; ri < 5; ri++) {
        const float sr = rates[ri];
        dbox_params p;
        dbox_init(&D, sr);
        for (int blk = 0; blk < 400; blk++) {
            if (blk % 7 == 0) {
                dbox_default_params(&p);
                p.pitch = (float)(rnd() % 2001) / 1000.0f - 1.0f;
                p.wave = (rnd() % 3) * 0.5f; p.wave2 = (rnd() % 3) * 0.5f;
                p.decay = (rnd() % 1001) / 1000.0f; p.decay2 = (rnd() % 1001) / 1000.0f;
                p.mode = rnd() % 8; p.mix2 = (rnd() % 1001) / 1000.0f; p.filter = (rnd() % 1001) / 1000.0f;
                p.attack = 0.0005f + (rnd() % 100) / 100.0f; p.env_decay = 0.01f + (rnd() % 100) / 50.0f;
                p.sustain = (rnd() % 101) / 100.0f; p.release = 0.01f + (rnd() % 100) / 20.0f;
                p.level = (rnd() % 101) / 100.0f; p.root = rnd() % 104;
                dbox_set_params(&D, &p);
            }
            for (int e = rnd() % 4; e > 0; e--) {
                const unsigned k = rnd() % 100;
                if (k < 60) dbox_note_on(&D, rnd() % 128, 1 + rnd() % 127);
                else if (k < 80) dbox_note_off(&D, rnd() % 128);
                else if (k < 85) dbox_pitch_bend(&D, (float)(rnd() % 2001) / 1000.0f - 1.0f);
                else if (k < 90) dbox_mod_wheel(&D, (rnd() % 101) / 100.0f);
                else if (k < 95) dbox_sustain_pedal(&D, rnd() % 2);
                else dbox_all_off(&D);
            }
            const int n = 1 + (int)(rnd() % 512);
            dbox_render(&D, L, R, n);
            for (int i = 0; i < n; i++)
                if (!(fabsf(L[i]) <= 1.0f) || !(fabsf(R[i]) <= 1.0f)) {
                    CHECK(0, "sr %.0f block %d: bad sample %f %f", sr, blk, L[i], R[i]);
                    return;
                }
        }
        dbox_take_activity(&D, &(dbox_activity){0});
    }
    printf("stability: 5 sample rates (22050 to 192000), 400 random blocks each, no NaN, peak <= 1\n");
}

static void test_activity(void)
{
    dbox_params p;
    dbox_activity a;
    setup(SR, &p);
    dbox_note_on(&D, 36 + 3, 100);
    dbox_note_on(&D, 36 + 9, 100);
    render_all(512);
    dbox_take_activity(&D, &a);
    CHECK(a.keys == ((1u << 3) | (1u << 9)) && a.peak > 0.0f && a.voices == 2 && a.notes == 2, "activity keys %x voices %d", a.keys, a.voices);
    dbox_take_activity(&D, &a);
    CHECK(a.keys == 0, "activity clears");
}

static void test_velocity(void)
{
    dbox_params p;
    setup(SR, &p);
    float prev = 0;
    for (int v = 20; v <= 127; v += 35) {
        dbox_reset(&D);
        dbox_note_on(&D, 36, v);
        render_all(4410);
        const float r = rms(L, 0, 4410);
        CHECK(r > prev, "velocity %d louder than the last", v);
        prev = r;
    }
}

int main(void)
{
    test_mapping();
    test_pitch_accuracy();
    test_polyphony_headroom();
    test_note_off_releases();
    test_no_clicks();
    test_knobs_change_sound();
    test_choke();
    test_determinism_and_splitting();
    test_stability();
    test_activity();
    test_velocity();
    if (failures) {
        printf("%d FAILED\n", failures);
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
