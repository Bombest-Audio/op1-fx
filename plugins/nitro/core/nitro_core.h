/* Nitro DSP core. Portable C99, no allocation, no dependencies beyond libm.
 *
 * A new take on the OP-1 Nitro effect: four controls (FREQ, RES, SHAPE, DRIVE) on a four-stage
 * filter cascade whose outputs are mixed into low-pass, band-pass, notch and high-pass shapes.
 * Written from scratch as float DSP. It is not a bit-exact copy of the OP-1 fixed-point code and
 * contains no firmware code or tables.
 *
 * One instance processes one stereo stream, or one mono stream. Every function is real-time safe.
 */
#ifndef NITRO_CORE_H
#define NITRO_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define NITRO_SHAPE_ROWS 12

/* Control values, all normalized 0..1. */
typedef struct {
    float freq;
    float res;
    float shape;
    float drive;
} nitro_params;

/* OP-1 factory default (raw knob values 500, 0, 10500, 16000 out of 32767).
 * FREQ, RES and DRIVE are the raw value over 32767. SHAPE is the position that lands on the same filter shape the
 * factory value does (raw 10500 over 2048 is 5.13 steps into the shape table, and this table spans 11 steps over the
 * full knob), so 5.127 / 11 = 0.46609. */
#define NITRO_DEFAULT_FREQ 0.01526f
#define NITRO_DEFAULT_RES 0.0f
#define NITRO_DEFAULT_SHAPE 0.46609f
#define NITRO_DEFAULT_DRIVE 0.48831f

typedef struct {
    float s[4];  /* trapezoidal one-pole states of the four stages */
    float env;   /* peak follower on the dry input */
} nitro_channel;

typedef struct {
    float sr;
    nitro_params target;
    float s_freq, s_res, s_shape, s_drive; /* smoothed controls */
    float smooth_coef;
    float env_decay;   /* per-sample decay of the peak follower */
    float act_peak;    /* strongest follower value since the last nitro_take_activity() */
    nitro_channel ch[2];
} nitro_t;

/* Cutoff in Hz for a normalized FREQ value (about 14 Hz to 16 kHz, exponential), and the inverse. */
float nitro_freq_hz(float norm);
float nitro_freq_to_norm(float hz);

/* Name of the shape nearest to a normalized SHAPE value: LP 24, LP 18, LP 12, LP 6, THRU, HP 6+, NOTCH, BAND,
 * HP 6, HP 12, HP 18, HP 24. The returned string is static. */
const char *nitro_shape_name(float norm);

void nitro_init(nitro_t *n, float sample_rate);
void nitro_reset(nitro_t *n);
void nitro_set_params(nitro_t *n, const nitro_params *params);

/* Input level seen since the last call (0..1, a peak follower with about a 190 ms release), then cleared.
 * For meters and animation only; it does not affect the audio. Call from the audio thread that runs nitro_process. */
float nitro_take_activity(nitro_t *n);

/* Process in place. right may be NULL for a mono stream. Left and right must be separate buffers. */
void nitro_process(nitro_t *n, float *left, float *right, int frames);

#ifdef __cplusplus
}
#endif
#endif
