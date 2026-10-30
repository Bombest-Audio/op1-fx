/* Behavioral tests for the Grid core. Build and run with core/tests/run.sh */
#include "../grid_core.h"

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

static unsigned rng_state = 12345u;
static float frand(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) / (float)(1u << 24) * 2.0f - 1.0f;
}

/* One grid plus its memory. */
typedef struct {
    grid_t g;
    float *mem;
} rig_t;

static void rig_open(rig_t *r, float sr, const grid_params *p)
{
    r->mem = (float *)malloc(sizeof(float) * grid_memory_floats(sr));
    grid_init(&r->g, sr, r->mem);
    grid_set_params(&r->g, p);
    grid_reset(&r->g);
}

static void rig_close(rig_t *r)
{
    free(r->mem);
}

static void run_blocks(grid_t *g, float *l, float *r, int n, int block)
{
    for (int i = 0; i < n; i += block) {
        const int c = n - i < block ? n - i : block;
        grid_process(g, l + i, r + i, c);
    }
}

static grid_params defaults(void)
{
    grid_params p = {GRID_DEFAULT_X, GRID_DEFAULT_Y, GRID_DEFAULT_ZFB, GRID_DEFAULT_MIX};
    return p;
}

static float rms(const float *x, int n)
{
    double s = 0;
    for (int i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return (float)sqrt(s / (n > 0 ? n : 1));
}

static float peak(const float *x, int n)
{
    float p = 0;
    for (int i = 0; i < n; i++)
        p = fmaxf(p, fabsf(x[i]));
    return p;
}

static void test_mapping(void)
{
    CHECK(grid_cells_from_norm(0.0f) == GRID_MIN_CELLS, "min edge %d", grid_cells_from_norm(0.0f));
    CHECK(grid_cells_from_norm(1.0f) == GRID_MAX_CELLS, "max edge %d", grid_cells_from_norm(1.0f));
    CHECK(grid_cells_from_norm(GRID_DEFAULT_X) == 124, "default edge %d", grid_cells_from_norm(GRID_DEFAULT_X));
    int prev = 0;
    for (int i = 0; i <= 1000; i++) {
        const int n = grid_cells_from_norm((float)i / 1000.0f);
        CHECK(n % 2 == 0 && n >= GRID_MIN_CELLS && n <= GRID_MAX_CELLS, "edge %d is not even or out of range", n);
        CHECK(n >= prev, "edge map is not monotonic at %d", i);
        prev = n;
    }
    for (int n = GRID_MIN_CELLS; n <= GRID_MAX_CELLS; n += 2)
        CHECK(grid_cells_from_norm(grid_norm_from_cells(n)) == n, "round trip failed for %d", n);
    /* one pass at the default size is a bit under 170 ms */
    CHECK(fabsf(grid_pass_ms(124, 124) - 168.75f) < 0.1f, "pass length %.2f ms", (double)grid_pass_ms(124, 124));
}

static void test_mix_zero_is_bit_exact(void)
{
    enum { N = 20000 };
    static float l[N], r[N], l0[N], r0[N];
    for (int i = 0; i < N; i++) {
        l0[i] = l[i] = frand();
        r0[i] = r[i] = frand() * 0.5f;
    }
    grid_params p = defaults();
    p.mix = 0.0f;
    rig_t rig;
    rig_open(&rig, 44100.0f, &p);
    run_blocks(&rig.g, l, r, N, 128);
    CHECK(memcmp(l, l0, sizeof l) == 0 && memcmp(r, r0, sizeof r) == 0, "MIX 0 changed the audio");
    rig_close(&rig);
}

/* The mesh must be a delay line: the first thing after an impulse is a quieter, inverted copy about three rows later,
 * and the next group of repeats arrives one pass later. */
static void test_impulse_structure(void)
{
    const float sr = 44100.0f;
    const int N = (int)(sr * 1.5f);
    float *l = (float *)calloc((size_t)N, sizeof(float));
    float *r = (float *)calloc((size_t)N, sizeof(float));
    const int at = 100;
    l[at] = r[at] = 1.0f;
    grid_params p = defaults();
    rig_t rig;
    rig_open(&rig, sr, &p);
    const int W = rig.g.W, R = rig.g.R;
    run_blocks(&rig.g, l, r, N, 64);

    const float dry = 2.0f * (1.0f - p.mix);
    CHECK(fabsf(l[at] - (dry > 1.0f ? 1.0f : dry)) < 1e-6f, "dry level %f", (double)l[at]);

    /* first echo */
    int first = -1;
    float first_v = 0;
    for (int i = at + 1; i < at + 3 * W + 3 * W; i++)
        if (fabsf(l[i]) > 0.01f) {
            first = i - at;
            first_v = l[i];
            break;
        }
    const float zz = p.zfb * p.zfb;
    CHECK(first == 3 * W + 1 || (first >= 3 * W - 2 * W && first <= 3 * W + 2 * W), "first echo at %d, rows are %d", first, W);
    CHECK(first_v < 0.0f, "first echo is not inverted (%f)", (double)first_v);
    CHECK(fabsf(first_v) > 0.5f * p.mix * zz && fabsf(first_v) < 1.05f * p.mix * zz, "first echo %f, expected about %f",
          (double)first_v, (double)(p.mix * zz));

    /* repeats one pass later: the energy in the window around 1 pass is clearly above the gap before it */
    const int T = W * R;
    const int a0 = at + T - 4 * W, a1 = at + T + 4 * W;
    const int gap0 = at + 6 * W, gap1 = at + T - 6 * W;
    CHECK(rms(l + a0, a1 - a0) > 3.0f * rms(l + gap0, gap1 - gap0), "no repeat group near one pass (%f vs %f)",
          (double)rms(l + a0, a1 - a0), (double)rms(l + gap0, gap1 - gap0));
    for (int i = 0; i < N; i++)
        CHECK(l[i] == r[i], "channels differ at %d", i);
    free(l);
    free(r);
    rig_close(&rig);
}

static float tail_rms(float zfb, int from_pass, int to_pass)
{
    const float sr = 44100.0f;
    grid_params p = defaults();
    p.zfb = zfb;
    rig_t rig;
    rig_open(&rig, sr, &p);
    const int T = rig.g.W * rig.g.R;
    const int N = T * (to_pass + 1);
    float *l = (float *)calloc((size_t)N, sizeof(float));
    float *r = (float *)calloc((size_t)N, sizeof(float));
    for (int i = 0; i < 2000; i++)
        l[i] = r[i] = 0.5f * frand();
    run_blocks(&rig.g, l, r, N, 128);
    const float v = rms(l + T * from_pass, T * (to_pass - from_pass));
    free(l);
    free(r);
    rig_close(&rig);
    return v;
}

static void test_feedback_controls_tail(void)
{
    const float none = tail_rms(0.0f, 2, 6);
    const float mid = tail_rms(0.55f, 2, 6);
    const float high = tail_rms(0.95f, 2, 6);
    CHECK(none < 1e-6f, "Z FDB 0 still repeats (%g)", (double)none);
    CHECK(mid > 1e-4f, "Z FDB 0.55 has no tail (%g)", (double)mid);
    CHECK(high > 4.0f * mid, "Z FDB 0.95 tail %g is not clearly longer than 0.55 tail %g", (double)high, (double)mid);
}

/* The heads have to be a real leapfrog. Seed two planes with random data, run whole passes with no input, and compare with a
 * plain full-grid update new = g(c) * (0.5 * neighbours - old). */
static void test_scan_matches_leapfrog(void)
{
    const float sr = 44100.0f;
    grid_params p = defaults();
    p.x = grid_norm_from_cells(30);
    p.y = grid_norm_from_cells(22);
    p.mix = 0.0f;
    p.zfb = 0.9f;
    rig_t rig;
    rig_open(&rig, sr, &p);
    grid_t *g = &rig.g;
    const int nx = g->nx, ny = g->ny, S = g->stride, W = g->W, R = g->R, T = W * R;
    CHECK(nx == 30 && ny == 22, "dimensions %d x %d", nx, ny);

    double *s_prev = (double *)calloc((size_t)(S * S), sizeof(double)); /* S(-1) */
    double *s_cur = (double *)calloc((size_t)(S * S), sizeof(double));  /* S(0) */
    double *s_next = (double *)calloc((size_t)(S * S), sizeof(double));
    for (int ch = 0; ch < 2; ch++)
        for (int r = 1; r <= ny - 2; r++)
            for (int c = 1; c <= nx - 2; c++) {
                const float a = 0.02f * frand(), b = 0.02f * frand();
                g->plane[ch][0][r * S + c] = ch == 0 ? a : g->plane[0][0][r * S + c];
                g->plane[ch][1][r * S + c] = ch == 0 ? b : g->plane[0][1][r * S + c];
                if (ch == 0) {
                    s_prev[r * S + c] = a;
                    s_cur[r * S + c] = b;
                }
            }
    const double zz = (double)g->s_zfb * (double)g->s_zfb;
    /* Head B starts half a row into the first even row, so in a running mesh the first half of that row has already been
     * advanced to the next time step. Do the same here so the seed is a state the heads could really be in. */
    for (int c = 1; c < 1 + W / 2; c++) {
        const double sum = s_cur[1 * S + c] + s_cur[3 * S + c] + s_cur[2 * S + c - 1] + s_cur[2 * S + c + 1];
        const float v = (float)((double)g->win[c] * zz * (0.5 * sum - s_prev[2 * S + c]));
        g->plane[0][0][2 * S + c] = v;
        g->plane[1][0][2 * S + c] = v;
    }
    float *l = (float *)calloc((size_t)T, sizeof(float));
    float *rr = (float *)calloc((size_t)T, sizeof(float));
    double worst = 0.0;
    for (int pass = 0; pass < 6; pass++) {
        run_blocks(g, l, rr, T, 37);
        for (int r = 1; r <= ny - 2; r++)
            for (int c = 1; c <= nx - 2; c++) {
                const double sum = s_cur[(r - 1) * S + c] + s_cur[(r + 1) * S + c] + s_cur[r * S + c - 1] + s_cur[r * S + c + 1];
                s_next[r * S + c] = (double)g->win[c] * zz * (0.5 * sum - s_prev[r * S + c]);
            }
        double *t = s_prev;
        s_prev = s_cur;
        s_cur = s_next;
        s_next = t;
        /* after pass k the plane k mod 2 holds S(k+1) */
        const float *plane = g->plane[0][pass & 1];
        for (int r = 1; r <= ny - 2; r++)
            for (int c = 1; c <= nx - 2; c++)
                worst = fmax(worst, fabs((double)plane[r * S + c] - s_cur[r * S + c]));
    }
    CHECK(worst < 1e-6, "scan differs from a plain leapfrog by %g", worst);
    printf("  scan vs leapfrog worst difference %.3g\n", worst);
    free(s_prev);
    free(s_cur);
    free(s_next);
    free(l);
    free(rr);
    rig_close(&rig);
}

/* With no input, the undriven mesh never gains energy, even with Z FDB at maximum. */
static void test_undriven_mesh_never_grows(void)
{
    const float sr = 44100.0f;
    grid_params p = defaults();
    p.zfb = 1.0f;
    p.mix = 0.0f;
    p.x = grid_norm_from_cells(50);
    p.y = grid_norm_from_cells(40);
    rig_t rig;
    rig_open(&rig, sr, &p);
    grid_t *g = &rig.g;
    const int S = g->stride;
    for (int pl = 0; pl < 2; pl++)
        for (int r = 1; r <= g->ny - 2; r++)
            for (int c = 1; c <= g->nx - 2; c++)
                g->plane[0][pl][r * S + c] = 0.05f * frand();
    const int T = g->W * g->R;
    float *l = (float *)calloc((size_t)T, sizeof(float));
    float *rr = (float *)calloc((size_t)T, sizeof(float));
    double e0 = 0, worst = 0;
    for (int pass = 0; pass < 60; pass++) {
        run_blocks(g, l, rr, T, 128);
        double e = 0;
        for (int pl = 0; pl < 2; pl++)
            for (int r = 1; r <= g->ny - 2; r++)
                for (int c = 1; c <= g->nx - 2; c++) {
                    const double v = g->plane[0][pl][r * S + c];
                    e += v * v;
                }
        if (pass == 0)
            e0 = e;
        worst = fmax(worst, e);
    }
    CHECK(worst <= 1.5 * e0, "mesh energy grew from %g to %g", e0, worst);
    free(l);
    free(rr);
    rig_close(&rig);
}

static void test_noise_is_finite_and_bounded(void)
{
    const float rates[] = {22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f};
    const float corners[][4] = {{0, 0, 1, 1}, {1, 1, 1, 1}, {0, 1, 1, 0.5f}, {1, 0, 0.9f, 1}, {0.49f, 0.49f, 1, 1},
                                {0.3f, 0.8f, 1, 0.75f}, {1, 1, 0, 0}};
    for (size_t k = 0; k < sizeof rates / sizeof rates[0]; k++) {
        const float sr = rates[k];
        const int N = (int)(sr * 1.5f);
        float *l = (float *)malloc(sizeof(float) * (size_t)N);
        float *r = (float *)malloc(sizeof(float) * (size_t)N);
        for (size_t c = 0; c < sizeof corners / sizeof corners[0]; c++) {
            grid_params p = {corners[c][0], corners[c][1], corners[c][2], corners[c][3]};
            rig_t rig;
            rig_open(&rig, sr, &p);
            for (int i = 0; i < N; i++) {
                l[i] = frand();
                r[i] = frand();
            }
            run_blocks(&rig.g, l, r, N, 100);
            int bad = 0;
            for (int i = 0; i < N; i++)
                bad += !isfinite(l[i]) || !isfinite(r[i]);
            /* full-scale noise in: the wet part is capped near 1 and the dry part is at most 1 */
            CHECK(bad == 0, "NaN or Inf at %.0f Hz, corner %zu", (double)sr, c);
            CHECK(peak(l, N) <= 2.1f && peak(r, N) <= 2.1f, "peak %f at %.0f Hz, corner %zu", (double)peak(l, N), (double)sr, c);
            rig_close(&rig);
        }
        free(l);
        free(r);
    }
}

static void test_block_size_does_not_matter(void)
{
    const int N = 30000;
    float *a = (float *)malloc(sizeof(float) * (size_t)N), *ar = (float *)malloc(sizeof(float) * (size_t)N);
    float *b = (float *)malloc(sizeof(float) * (size_t)N), *br = (float *)malloc(sizeof(float) * (size_t)N);
    for (int i = 0; i < N; i++) {
        a[i] = b[i] = 0.4f * frand();
        ar[i] = br[i] = 0.4f * frand();
    }
    grid_params p = defaults();
    rig_t r1, r2;
    rig_open(&r1, 44100.0f, &p);
    rig_open(&r2, 44100.0f, &p);
    /* move the controls part way through so the resize path is covered as well */
    grid_params q = p;
    q.x = 0.2f;
    q.y = 0.8f;
    q.zfb = 0.7f;
    run_blocks(&r1.g, a, ar, 12000, 1);
    grid_set_params(&r1.g, &q);
    run_blocks(&r1.g, a + 12000, ar + 12000, N - 12000, 1);
    run_blocks(&r2.g, b, br, 12000, 128);
    grid_set_params(&r2.g, &q);
    run_blocks(&r2.g, b + 12000, br + 12000, N - 12000, 128);
    CHECK(memcmp(a, b, sizeof(float) * (size_t)N) == 0 && memcmp(ar, br, sizeof(float) * (size_t)N) == 0,
          "block size 1 and 128 give different audio");
    free(a);
    free(ar);
    free(b);
    free(br);
    rig_close(&r1);
    rig_close(&r2);
}

static float max_step(const float *x, int n)
{
    float m = 0;
    for (int i = 1; i < n; i++)
        m = fmaxf(m, fabsf(x[i] - x[i - 1]));
    return m;
}

static void sweep_run(float *l, float *r, int n, float sr, int sweep_geometry, int sweep_levels)
{
    grid_params p = defaults();
    rig_t rig;
    rig_open(&rig, sr, &p);
    for (int i = 0; i < n; i += 64) {
        const float t = (float)i / (float)n;
        if (sweep_geometry) {
            p.x = 0.65f + 0.35f * sinf(t * 40.0f);
            p.y = 0.65f + 0.35f * sinf(t * 23.0f + 1.0f);
        }
        if (sweep_levels) {
            p.zfb = 0.5f + 0.5f * sinf(t * 17.0f);
            p.mix = 0.5f + 0.5f * sinf(t * 11.0f + 2.0f);
        }
        grid_set_params(&rig.g, &p);
        const int c = n - i < 64 ? n - i : 64;
        grid_process(&rig.g, l + i, r + i, c);
    }
    rig_close(&rig);
}

/* Sweep X and Y while a sine plays, with the other controls at their defaults. The same sine through a static mesh steps
 * by 0.011 per sample at most. A resize that moved a head to another cell would add steps of 0.07 and more. */
static void test_geometry_sweeps_do_not_click(void)
{
    const float sr = 44100.0f;
    const int N = (int)(sr * 6.0f);
    float *l = (float *)malloc(sizeof(float) * (size_t)N), *r = (float *)malloc(sizeof(float) * (size_t)N);
    for (int i = 0; i < N; i++)
        l[i] = r[i] = 0.3f * sinf(2.0f * 3.14159265f * 220.0f * (float)i / sr);
    sweep_run(l, r, N, sr, 1, 0);
    const float step = max_step(l + 2000, N - 2000);
    CHECK(step < 0.03f, "largest step %f while sweeping X and Y", (double)step);
    printf("  X and Y sweep max step %.4f\n", (double)step);
    free(l);
    free(r);
}

/* Z FDB and MIX move every sample through a smoother. A full-range sweep at full feedback is the roughest case for the
 * mesh itself (a static mesh at Z FDB 1 and MIX 1 steps by 0.07), so the limit here is only a little above that. */
static void test_level_sweeps_do_not_click(void)
{
    const float sr = 44100.0f;
    const int N = (int)(sr * 6.0f);
    float *l = (float *)malloc(sizeof(float) * (size_t)N), *r = (float *)malloc(sizeof(float) * (size_t)N);
    for (int i = 0; i < N; i++)
        l[i] = r[i] = 0.3f * sinf(2.0f * 3.14159265f * 220.0f * (float)i / sr);
    sweep_run(l, r, N, sr, 0, 1);
    const float step = max_step(l + 2000, N - 2000);
    CHECK(step < 0.1f, "largest step %f while sweeping Z FDB and MIX", (double)step);
    printf("  Z FDB and MIX sweep max step %.4f\n", (double)step);
    free(l);
    free(r);
}

static void test_shrink_and_grow_leaves_no_stale_cells(void)
{
    const float sr = 44100.0f;
    grid_params big = defaults();
    big.x = big.y = 1.0f;
    big.zfb = 0.95f;
    big.mix = 1.0f;
    rig_t rig;
    rig_open(&rig, sr, &big);
    const int N = (int)(sr * 4.0f);
    float *l = (float *)calloc((size_t)N, sizeof(float));
    float *r = (float *)calloc((size_t)N, sizeof(float));
    for (int i = 0; i < 20000; i++)
        l[i] = r[i] = 0.4f * frand();
    run_blocks(&rig.g, l, r, 20000, 128);
    grid_params small = big;
    small.x = small.y = 0.0f;
    small.zfb = 0.2f;
    grid_set_params(&rig.g, &small);
    memset(l, 0, sizeof(float) * (size_t)N);
    memset(r, 0, sizeof(float) * (size_t)N);
    run_blocks(&rig.g, l, r, N, 128); /* the small mesh rings out */
    grid_set_params(&rig.g, &big);
    memset(l, 0, sizeof(float) * (size_t)N);
    memset(r, 0, sizeof(float) * (size_t)N);
    run_blocks(&rig.g, l, r, N, 128);
    CHECK(rig.g.nx == 254 && rig.g.ny == 254, "mesh did not grow back (%d x %d)", rig.g.nx, rig.g.ny);
    CHECK(peak(l, N) < 1e-4f, "stale cells came back after growing, peak %g", (double)peak(l, N));
    free(l);
    free(r);
    rig_close(&rig);
}

static void test_reset_clears_everything(void)
{
    const float sr = 44100.0f;
    grid_params p = defaults();
    p.zfb = 0.95f;
    rig_t rig;
    rig_open(&rig, sr, &p);
    float l[20000], r[20000];
    for (int i = 0; i < 20000; i++)
        l[i] = r[i] = frand();
    run_blocks(&rig.g, l, r, 20000, 128);
    grid_reset(&rig.g);
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    run_blocks(&rig.g, l, r, 20000, 128);
    CHECK(peak(l, 20000) == 0.0f && peak(r, 20000) == 0.0f, "audio after reset is not silent (%g)", (double)peak(l, 20000));
    CHECK(grid_take_activity(&rig.g) == 0.0f, "activity after reset");
    rig_close(&rig);
}

static void test_channels_are_independent(void)
{
    const int N = 40000;
    float *l = (float *)malloc(sizeof(float) * (size_t)N), *r = (float *)calloc((size_t)N, sizeof(float));
    for (int i = 0; i < N; i++)
        l[i] = 0.3f * frand();
    grid_params p = defaults();
    rig_t rig;
    rig_open(&rig, 44100.0f, &p);
    run_blocks(&rig.g, l, r, N, 128);
    CHECK(peak(r, N) == 0.0f, "left input leaked into the silent right channel (%g)", (double)peak(r, N));
    CHECK(peak(l, N) > 0.1f, "left channel is silent");
    free(l);
    free(r);
    rig_close(&rig);
}

/* One pass takes the same time at every sample rate, within the rounding of an even edge length. */
static void test_pass_time_follows_sample_rate(void)
{
    const float rates[] = {22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f};
    for (size_t k = 0; k < sizeof rates / sizeof rates[0]; k++) {
        grid_params p = defaults();
        rig_t rig;
        rig_open(&rig, rates[k], &p);
        const float ms = 1000.0f * (float)(rig.g.W * rig.g.R) / rates[k];
        CHECK(fabsf(ms - 168.75f) < 12.0f, "pass is %.1f ms at %.0f Hz", (double)ms, (double)rates[k]);
        rig_close(&rig);
    }
}

static void test_activity_meter(void)
{
    grid_params p = defaults();
    rig_t rig;
    rig_open(&rig, 44100.0f, &p);
    float l[2000] = {0}, r[2000] = {0};
    l[10] = r[10] = 1.0f;
    CHECK(grid_take_activity(&rig.g) == 0.0f, "activity before any audio");
    run_blocks(&rig.g, l, r, 2000, 128);
    const float a = grid_take_activity(&rig.g);
    CHECK(a > 0.05f && a < 0.5f, "activity after an impulse is %f", (double)a);
    CHECK(grid_take_activity(&rig.g) == 0.0f, "activity was not cleared");
    rig_close(&rig);
}

static void test_bad_input_does_not_poison_the_mesh(void)
{
    grid_params p = defaults();
    rig_t rig;
    rig_open(&rig, 44100.0f, &p);
    float l[4000] = {0}, r[4000] = {0};
    l[5] = NAN;
    r[7] = INFINITY;
    run_blocks(&rig.g, l, r, 4000, 128);
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    run_blocks(&rig.g, l, r, 4000, 128);
    int bad = 0;
    for (int i = 0; i < 4000; i++)
        bad += !isfinite(l[i]) || !isfinite(r[i]);
    CHECK(bad == 0, "mesh kept a NaN or Inf");
    rig_close(&rig);
}

int main(void)
{
    test_mapping();
    test_mix_zero_is_bit_exact();
    test_impulse_structure();
    test_feedback_controls_tail();
    test_scan_matches_leapfrog();
    test_undriven_mesh_never_grows();
    test_noise_is_finite_and_bounded();
    test_block_size_does_not_matter();
    test_geometry_sweeps_do_not_click();
    test_level_sweeps_do_not_click();
    test_shrink_and_grow_leaves_no_stale_cells();
    test_reset_clears_everything();
    test_channels_are_independent();
    test_pass_time_follows_sample_rate();
    test_activity_meter();
    test_bad_input_does_not_poison_the_mesh();
    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("all Grid core tests passed\n");
    return 0;
}
