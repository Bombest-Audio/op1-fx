/* Grid DSP core. Portable C99, no allocation, no dependencies beyond libm.
 *
 * A new take on the OP-1 Grid effect: four controls (X, Y, Z FDB, MIX) and a 2D wave mesh that is scanned
 * like a delay line. Written from scratch as float DSP. It is not a bit-exact copy of the OP-1 fixed-point
 * code and contains no firmware code or tables.
 *
 * The mesh needs memory proportional to the sample rate, so the caller owns it: ask grid_memory_floats() how
 * much, hand grid_init() a block that big. Everything after that is real-time safe.
 * One instance processes one stereo stream (two independent meshes that share one scan position and one
 * set of controls).
 */
#ifndef GRID_CORE_H
#define GRID_CORE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mesh edge in cells, at 44.1 kHz. Always even. Other sample rates scale it by sqrt(rate / 44100). */
#define GRID_MIN_CELLS 6
#define GRID_MAX_CELLS 254

/* Control values, all normalized 0..1. */
typedef struct {
    float x;   /* mesh width */
    float y;   /* mesh height */
    float zfb; /* feedback per pass through the mesh (applied squared) */
    float mix; /* wet send and dry level together */
} grid_params;

/* OP-1 factory default (raw knob values 8000, 8000, 18000, 18000). X and Y are raw values out of an inferred
 * 16383 range (see docs/DSP.md), Z FDB and MIX are out of 32767. */
#define GRID_DEFAULT_X 0.4883f
#define GRID_DEFAULT_Y 0.4883f
#define GRID_DEFAULT_ZFB 0.5494f
#define GRID_DEFAULT_MIX 0.5494f

/* Normalized X or Y to a mesh edge in cells (even, GRID_MIN_CELLS..GRID_MAX_CELLS, at 44.1 kHz) and back. */
int grid_cells_from_norm(float v);
float grid_norm_from_cells(int cells);

/* Length of one pass through the mesh in milliseconds for the given edges (44.1 kHz cells). The mesh takes one
 * pass to advance one time step, so this is the spacing of the repeats. */
float grid_pass_ms(int cells_x, int cells_y);

/* Floats of working memory grid_init() needs at this sample rate. */
size_t grid_memory_floats(float sample_rate);

typedef struct {
    int j, c, par; /* row-pair index, column, which of the two time planes */
} grid_ptr;

typedef struct {
    float sr;
    float scale;           /* sqrt(sr / 44100) */
    int stride;            /* floats per mesh row, also the largest edge for this rate */
    float *win;            /* window over columns 1..W, length stride + 1 */
    float *plane[2][2];    /* [channel][time plane], each stride * stride */
    grid_params target;
    int nx, ny;            /* committed mesh edges (cells, even) */
    int nx_t, ny_t;        /* wanted mesh edges */
    int W, R;              /* interior columns, interior row pairs */
    grid_ptr a, b, d1, d2; /* update heads for odd and even rows, and the write heads in front of them */
    int b_stall;           /* samples B and D2 still have to wait after a resize */
    float s_zfb, s_mix;    /* smoothed controls */
    float smooth_coef;
    float gate, gate_step; /* ducks the wet path while the mesh is re-dimensioned */
    int fade_len;          /* samples of the wet fade around a resize */
    int stage;             /* 0 steady, 1 fading out before a resize, 2 fading back in */
    int hold, hold_len;    /* samples before the next resize is allowed */
    float act_peak;        /* loudest wet sample since the last grid_take_activity() */
} grid_t;

/* `mem` must hold grid_memory_floats(sample_rate) floats. It is cleared by grid_reset(). */
void grid_init(grid_t *g, float sample_rate, float *mem);
void grid_reset(grid_t *g);
void grid_set_params(grid_t *g, const grid_params *params);

/* Loudest wet output sample (0..1.5, about 0.3 for a normal hit) since the last call, then cleared. For meters and
 * animation only; it does not affect the audio. Call from the audio thread that runs grid_process. */
float grid_take_activity(grid_t *g);

/* Process in place. Needs two separate buffers; the same pointer must not be passed for both channels. */
void grid_process(grid_t *g, float *left, float *right, int frames);

#ifdef __cplusplus
}
#endif
#endif
