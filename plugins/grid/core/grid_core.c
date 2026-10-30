#include "grid_core.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f

/* The mesh stores two time planes. A pass visits every interior cell once and advances the mesh by one time step:
 * new = g * (0.5 * (sum of the four neighbours in the other plane) - old). That is the 2D waveguide mesh update with a
 * loss factor g. One pass takes (W * R) samples, so the mesh is a delay line whose taps are coupled to their neighbours
 * one sample apart (left and right cell) and half a row apart (the cells above and below).
 *
 * Two heads scan in step. Head A walks the odd rows, head B the even rows, B half a row ahead of A. Both are weighted by a
 * half-sine over the row, so the pair always sums to unit power and the readout is continuous at the row ends. The write
 * heads D1 and D2 sit three rows in front of A and B and inject the input into the cells those heads reach next. */

#define LEAD_ROWS 3
#define RESIZE_STEP 4  /* most cells an edge may change by in one resize */
#define SAT_T 0.5f     /* cells are linear up to this level ... */
#define SAT_MAX 0.7f   /* ... and ease into this ceiling, so two heads sum to at most about 1 */

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float cell_sat(float x)
{
    const float a = fabsf(x);
    if (a <= SAT_T)
        return a < 1e-18f ? 0.0f : x; /* flush denormals */
    const float k = SAT_MAX - SAT_T;
    float z = (a - SAT_T) / k;
    z = z > 3.0f ? 3.0f : z;
    const float z2 = z * z;
    const float y = SAT_T + k * (z * (27.0f + z2) / (27.0f + 9.0f * z2)); /* Pade tanh */
    return x < 0.0f ? -y : y;
}

/* One mesh cell: the 2D waveguide update with loss, g = window * Z FDB^2. q is the plane holding the previous time step,
 * p the plane holding the one before it, which this cell overwrites. */
static inline float update_cell(const float *q, float *p, int idx, int stride, float g)
{
    const float n = cell_sat(g * (0.5f * (q[idx - stride] + q[idx + stride] + q[idx - 1] + q[idx + 1]) - p[idx]));
    p[idx] = n;
    return n;
}

static int even_round(float v)
{
    return 2 * (int)floorf(v * 0.5f + 0.5f);
}

static float sample_rate_scale(float sr)
{
    return sqrtf(sr / 44100.0f);
}

static int max_cells(float scale)
{
    const int n = even_round((float)GRID_MAX_CELLS * scale);
    return n < GRID_MIN_CELLS ? GRID_MIN_CELLS : n;
}

int grid_cells_from_norm(float v)
{
    /* Units of 2 cells: 3..127. The OP-1 raw value shifted right by 7 is the unit count, and its knob is taken
     * to span 0..16383, so the normalized value times 128 is that count. */
    int u = (int)floorf(clampf(v, 0.0f, 1.0f) * 128.0f);
    u = u < 3 ? 3 : (u > 127 ? 127 : u);
    return 2 * u;
}

float grid_norm_from_cells(int cells)
{
    int u = cells / 2;
    u = u < 3 ? 3 : (u > 127 ? 127 : u);
    return ((float)u + 0.5f) / 128.0f;
}

float grid_pass_ms(int cells_x, int cells_y)
{
    const int w = cells_x - 2, r = (cells_y - 2) / 2;
    return 1000.0f * (float)(w * r) / 44100.0f;
}

size_t grid_memory_floats(float sample_rate)
{
    const float sr = clampf(sample_rate, 8000.0f, 384000.0f);
    const size_t n = (size_t)max_cells(sample_rate_scale(sr));
    return (n + 1) + 4u * n * n;
}

static int scaled_cells(const grid_t *g, float norm)
{
    int n = even_round((float)grid_cells_from_norm(norm) * g->scale);
    n = n < GRID_MIN_CELLS ? GRID_MIN_CELLS : n;
    return n > g->stride ? g->stride : n;
}

static void ptr_step(grid_ptr *p, int W, int R)
{
    if (++p->c > W) {
        p->c = 1;
        if (++p->j >= R) {
            p->j = 0;
            p->par ^= 1;
        }
    }
}

static void ptr_add_rows(grid_ptr *p, int rows, int R)
{
    const int t = p->j + rows;
    p->par ^= (t / R) & 1;
    p->j = t % R;
}

/* Place every head from A for the current dimensions. Only for a mesh that holds no data yet. */
static void sync_heads(grid_t *g)
{
    const int W = g->W, R = g->R;
    g->a.c = g->a.c < 1 ? 1 : (g->a.c > W ? W : g->a.c);
    g->a.j = g->a.j >= R ? R - 1 : g->a.j;
    g->b = g->a;
    g->b.c += W / 2;
    if (g->b.c > W) {
        g->b.c -= W;
        if (++g->b.j >= R) {
            g->b.j = 0;
            g->b.par ^= 1;
        }
    }
    g->d1 = g->a;
    ptr_add_rows(&g->d1, LEAD_ROWS, R);
    g->d2 = g->b;
    ptr_add_rows(&g->d2, LEAD_ROWS, R);
    g->b_stall = 0;
}

/* Keep head B half a row ahead of A after the row length changed, without a seam. Moving B to a new cell would skip or
 * repeat cells, leaving a few of them a time step off from their neighbours, and that reads out as a click. Instead B
 * either pauses for a couple of samples (it needs to be less far ahead) or updates the cells it has to cover right now
 * (it needs to be further ahead). A is at the start of a row, so it needs no change. */
static void retime_heads(grid_t *g)
{
    const int W = g->W, R = g->R, S = g->stride;
    const int want = 1 + W / 2;
    g->a.c = 1;
    if (g->b.c > W) { /* only for very short rows */
        g->b.c = 1;
        if (++g->b.j >= R) {
            g->b.j = 0;
            g->b.par ^= 1;
        }
    }
    g->b_stall = 0;
    int delta = want - g->b.c;
    if (delta < 0) {
        g->b_stall = -delta;
    } else {
        const float zz = g->s_zfb * g->s_zfb;
        for (; delta > 0 && g->b.c < W; delta--) {
            const int idx = (2 * g->b.j + 2) * S + g->b.c;
            for (int ch = 0; ch < 2; ch++)
                update_cell(g->plane[ch][g->b.par ^ 1], g->plane[ch][g->b.par], idx, S, g->win[g->b.c] * zz);
            g->b.c++;
        }
    }
    g->d1 = g->a;
    ptr_add_rows(&g->d1, LEAD_ROWS, R);
    g->d2 = g->b;
    ptr_add_rows(&g->d2, LEAD_ROWS, R);
}

static void zero_cols(float *p, int stride, int rows, int c0, int c1)
{
    for (int r = 0; r < rows; r++)
        for (int c = c0; c <= c1; c++)
            p[r * stride + c] = 0.0f;
}

static void zero_rows(float *p, int stride, int cols, int r0, int r1)
{
    for (int r = r0; r <= r1; r++)
        for (int c = 0; c < cols; c++)
            p[r * stride + c] = 0.0f;
}

/* Switch to new dimensions. Cells that become part of the mesh are zeroed so stale values from an earlier, larger
 * mesh cannot come back. The last column and row are the boundary and must read as zero. */
static void set_dimensions(grid_t *g, int nx, int ny, int retime)
{
    const int ox = g->nx, oy = g->ny;
    for (int ch = 0; ch < 2; ch++) {
        for (int pl = 0; pl < 2; pl++) {
            float *p = g->plane[ch][pl];
            zero_cols(p, g->stride, ny, nx > ox ? ox - 1 : nx - 1, nx - 1);
            zero_rows(p, g->stride, nx, ny > oy ? oy - 1 : ny - 1, ny - 1);
        }
    }
    g->nx = nx;
    g->ny = ny;
    g->W = nx - 2;
    g->R = (ny - 2) / 2;
    for (int c = 1; c <= g->W; c++)
        g->win[c] = sinf(PI_F * ((float)c - 0.5f) / (float)g->W);
    if (retime)
        retime_heads(g);
    else
        sync_heads(g);
}

/* The next size on the way to the wanted one, given where head A is. Making the mesh shorter is only allowed while A and B
 * still have a row left in the shorter mesh, otherwise a head would wrap early and leave part of the mesh a time step
 * behind. */
static void plan_resize(const grid_t *g, int *nx, int *ny)
{
    int dx = g->nx_t - g->nx, dy = g->ny_t - g->ny;
    dx = dx > RESIZE_STEP ? RESIZE_STEP : (dx < -RESIZE_STEP ? -RESIZE_STEP : dx);
    dy = dy > RESIZE_STEP ? RESIZE_STEP : (dy < -RESIZE_STEP ? -RESIZE_STEP : dy);
    if (dy < 0 && g->a.j > (g->ny + dy - 2) / 2 - 2)
        dy = 0;
    *nx = g->nx + dx;
    *ny = g->ny + dy;
}

void grid_init(grid_t *g, float sample_rate, float *mem)
{
    memset(g, 0, sizeof *g);
    g->sr = clampf(sample_rate, 8000.0f, 384000.0f);
    g->scale = sample_rate_scale(g->sr);
    g->stride = max_cells(g->scale);
    const size_t plane_floats = (size_t)g->stride * (size_t)g->stride;
    g->win = mem;
    float *p = mem + (g->stride + 1);
    for (int ch = 0; ch < 2; ch++)
        for (int pl = 0; pl < 2; pl++, p += plane_floats)
            g->plane[ch][pl] = p;
    g->target.x = GRID_DEFAULT_X;
    g->target.y = GRID_DEFAULT_Y;
    g->target.zfb = GRID_DEFAULT_ZFB;
    g->target.mix = GRID_DEFAULT_MIX;
    g->smooth_coef = 1.0f - expf(-1.0f / (0.005f * g->sr));
    g->fade_len = (int)(0.0015f * g->sr);
    g->gate_step = 1.0f / (float)g->fade_len;
    g->hold_len = (int)(0.02f * g->sr);
    grid_reset(g);
}

void grid_reset(grid_t *g)
{
    const size_t plane_floats = (size_t)g->stride * (size_t)g->stride;
    memset(g->win, 0, sizeof(float) * (size_t)(g->stride + 1));
    for (int ch = 0; ch < 2; ch++)
        for (int pl = 0; pl < 2; pl++)
            memset(g->plane[ch][pl], 0, sizeof(float) * plane_floats);
    g->nx_t = scaled_cells(g, g->target.x);
    g->ny_t = scaled_cells(g, g->target.y);
    g->nx = g->nx_t;
    g->ny = g->ny_t;
    memset(&g->a, 0, sizeof g->a);
    g->a.c = 1;
    set_dimensions(g, g->nx, g->ny, 0);
    g->s_zfb = g->target.zfb;
    g->s_mix = g->target.mix;
    g->gate = 1.0f;
    g->stage = 0;
    g->hold = 0;
    g->act_peak = 0.0f;
}

void grid_set_params(grid_t *g, const grid_params *p)
{
    g->target.x = clampf(p->x, 0.0f, 1.0f);
    g->target.y = clampf(p->y, 0.0f, 1.0f);
    g->target.zfb = clampf(p->zfb, 0.0f, 1.0f);
    g->target.mix = clampf(p->mix, 0.0f, 1.0f);
    g->nx_t = scaled_cells(g, g->target.x);
    g->ny_t = scaled_cells(g, g->target.y);
}

float grid_take_activity(grid_t *g)
{
    const float a = g->act_peak;
    g->act_peak = 0.0f;
    return a;
}

static inline float approach(float s, float t, float coef)
{
    s += coef * (t - s);
    return fabsf(t - s) < 1e-6f ? t : s;
}

void grid_process(grid_t *g, float *left, float *right, int frames)
{
    float *io[2] = {left, right};
    const int S = g->stride;
    const float coef = g->smooth_coef;

    for (int i = 0; i < frames; i++) {
        g->s_zfb = approach(g->s_zfb, g->target.zfb, coef);
        g->s_mix = approach(g->s_mix, g->target.mix, coef);

        /* Re-dimensioning moves the heads, which would click. A resize therefore waits for the start of a row
         * (so the scan front stays where it was), takes at most RESIZE_STEP cells at a time, and happens while the
         * wet path is faded out. The fade starts one fade-length before the row ends so the mesh changes at silence. */
        if (g->stage == 0) {
            if (g->hold > 0)
                g->hold--;
            else if (g->nx != g->nx_t || g->ny != g->ny_t) {
                int nx, ny;
                plan_resize(g, &nx, &ny);
                if (nx != g->nx || ny != g->ny)
                    g->stage = 1;
            }
        }
        if (g->stage == 1) {
            if (g->W <= g->fade_len || g->W - g->a.c + 1 <= g->fade_len || g->gate < 1.0f)
                g->gate -= g->gate_step;
            if (g->gate <= 0.0f) {
                g->gate = 0.0f;
                if (g->a.c == 1) {
                    int nx, ny;
                    plan_resize(g, &nx, &ny);
                    if (nx != g->nx || ny != g->ny)
                        set_dimensions(g, nx, ny, 1);
                    g->stage = 2;
                    g->hold = g->hold_len;
                }
            }
        } else if (g->stage == 2) {
            g->gate += g->gate_step;
            if (g->gate >= 1.0f) {
                g->gate = 1.0f;
                g->stage = 0;
            }
        }

        const float *win = g->win;
        const float zz = g->s_zfb * g->s_zfb;
        const float dry = 2.0f * (1.0f - g->s_mix) < 1.0f ? 2.0f * (1.0f - g->s_mix) : 1.0f;
        const float send = g->s_mix * g->gate;
        const float wa = win[g->a.c], wb = win[g->b.c], wd1 = win[g->d1.c], wd2 = win[g->d2.c];
        const float ga = wa * zz, gb = wb * zz;
        const int ia = (2 * g->a.j + 1) * S + g->a.c;
        const int ib = (2 * g->b.j + 2) * S + g->b.c;
        const int id1 = (2 * g->d1.j + 1) * S + g->d1.c;
        const int id2 = (2 * g->d2.j + 2) * S + g->d2.c;
        float peak = 0.0f;
        const int stall = g->b_stall > 0;
        if (stall)
            g->b_stall--;

        for (int ch = 0; ch < 2; ch++) {
            float *pa = g->plane[ch][g->a.par], *qa = g->plane[ch][g->a.par ^ 1];
            float *pb = g->plane[ch][g->b.par], *qb = g->plane[ch][g->b.par ^ 1];
            const float x = io[ch][i];
            /* keep one bad input sample from poisoning the mesh */
            const float xi = fabsf(x) <= 4.0f ? x : (x > 4.0f ? 4.0f : (x < -4.0f ? -4.0f : 0.0f));
            const float v = xi * send;
            g->plane[ch][g->d1.par][id1] += v * wd1;
            float n2 = 0.0f;
            if (!stall) {
                g->plane[ch][g->d2.par][id2] += v * wd2;
                n2 = update_cell(qb, pb, ib, S, gb);
            }
            const float n1 = update_cell(qa, pa, ia, S, ga);

            const float wet = (wa * n1 + wb * n2) * g->gate;
            const float aw = fabsf(wet);
            peak = aw > peak ? aw : peak;
            io[ch][i] = x * dry + wet;
        }
        if (peak > g->act_peak)
            g->act_peak = peak;

        ptr_step(&g->a, g->W, g->R);
        ptr_step(&g->d1, g->W, g->R);
        if (!stall) {
            ptr_step(&g->b, g->W, g->R);
            ptr_step(&g->d2, g->W, g->R);
        }
    }
}
