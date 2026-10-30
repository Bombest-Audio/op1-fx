/* Delay DSP core. Portable C99, no allocation, no dependencies beyond libm.
 *
 * A new take on the OP-1 Delay effect: four controls (TIME, SPEED, FDB, INPUT) and a tape-style
 * feedback delay. Written from scratch as float DSP. It is not a bit-exact copy of the OP-1
 * fixed-point code and contains no firmware code or tables.
 *
 * The delay line memory is owned by the caller (the core never allocates): ask
 * delay_buffer_frames() how many floats each channel needs, hand two such buffers to delay_init().
 * One instance processes one stereo stream (two independent delay lines with shared controls).
 * Every function is real-time safe.
 */
#ifndef DELAY_CORE_H
#define DELAY_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Longest delay the controls can reach, in seconds. */
#define DELAY_MAX_SECONDS 8.0f

/* Control values, all normalized 0..1. */
typedef struct {
    float time;  /* loop length at full tape speed, 31 ms to 1 s, exponential */
    float speed; /* tape speed. Delay time is loop length divided by speed; slower tape is darker */
    float fdb;   /* feedback, 0 to 1 */
    float input; /* level of the signal sent into the tape, 0 to 1 */
} delay_params;

/* OP-1 factory default (raw knob values 8000, 8000, 8000, 8000 out of 32767). */
#define DELAY_DEFAULT_TIME 0.244f
#define DELAY_DEFAULT_SPEED 0.244f
#define DELAY_DEFAULT_FDB 0.244f
#define DELAY_DEFAULT_INPUT 0.244f

typedef struct {
    float sr;
    delay_params target;
    float *buf[2]; /* caller owned, size floats each */
    int size;
    int w; /* write index, shared by both channels */
    /* smoothed controls */
    float s_time, s_speed, s_fdb, s_input;
    float smooth_coef;
    float d_coef;         /* follow coefficient of the delay time */
    double d_target, d_cur; /* delay in samples; double because float cannot hold a 0.01 sample step at 8 s and 192 kHz */
    double d_max;           /* longest delay in samples that fits the buffer */
    float a_lp;            /* coefficient of the two loop low-pass poles */
    int kcount;            /* samples until the next control-rate update */
    float lp1[2], lp2[2];  /* low-pass states per channel */
    float act_peak;        /* strongest echo level since the last delay_take_activity() */
} delay_t;

/* Floats needed per channel for a sample rate. */
int delay_buffer_frames(float sample_rate);

/* Loop length in seconds for a normalized TIME value. */
float delay_base_seconds(float time);
/* Tape speed as a fraction of full speed (0.03 to 1) for the SPEED value, after the 8 s ceiling is applied. */
float delay_speed_ratio(float speed, float time);
/* Delay time in seconds once the controls have settled. */
float delay_time_seconds(float time, float speed);
/* How long the echoes ring after the input stops, in seconds (capped at 30). For host tail reporting. */
float delay_tail_seconds(const delay_params *params);

/* buf_l and buf_r each hold delay_buffer_frames(sample_rate) floats. They are cleared by delay_init. */
void delay_init(delay_t *d, float sample_rate, float *buf_l, float *buf_r);
/* Clear the tape and all state, jump the smoothed controls to their targets. */
void delay_reset(delay_t *d);
void delay_set_params(delay_t *d, const delay_params *params);

/* Current (smoothed) delay in seconds. For tests and meters. */
float delay_current_seconds(const delay_t *d);

/* Peak echo level (0 to about 1) seen since the last call, then cleared. For meters and animation only; it
 * does not affect the audio. Call from the audio thread that runs delay_process. */
float delay_take_activity(delay_t *d);

/* Process in place. Needs two separate buffers; the same pointer must not be passed for both channels. */
void delay_process(delay_t *d, float *left, float *right, int frames);

#ifdef __cplusplus
}
#endif
#endif
