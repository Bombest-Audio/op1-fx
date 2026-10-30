/* Spring DSP core. Portable C99, no allocation, no dependencies beyond libm.
 *
 * A new take on the OP-1 Spring effect: four controls (TONE, TURNS, DAMP, LEVEL) driving three
 * coupled spring loops. Each loop is a chain of dispersive all-pass sections (the chirp) plus a
 * delay line, with a low-pass and a loss in the feedback path. Written from scratch as float DSP.
 * It is not a bit-exact copy of the OP-1 fixed-point code and contains no firmware code or tables.
 *
 * One instance processes one stereo stream. Every function is real-time safe.
 */
#ifndef SPRING_CORE_H
#define SPRING_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define SPRING_COUNT 3          /* springs in the tank */
#define SPRING_STAGES 32        /* dispersive all-pass sections per spring */
#define SPRING_MAX_M 18         /* largest all-pass stretch, enough for 192 kHz */
#define SPRING_MAX_DELAY 8600   /* largest delay line in samples, enough for 192 kHz */
#define SPRING_MIN_SR 8000.0f
#define SPRING_MAX_SR 192000.0f

/* Control values, all normalized 0..1. */
typedef struct {
    float tone;   /* brightness of the trail: loop low-pass corner */
    float turns;  /* how dispersive the springs are: strength of the chirp */
    float damp;   /* how fast the tail dies (cubic: most of the range is long tails) */
    float level;  /* how hard the springs are driven (squared); the dry signal always passes at unity */
} spring_params;

/* OP-1 factory default (raw knob values 8000, 15000, 2000, 20000 out of 32767). */
#define SPRING_DEFAULT_TONE 0.244f
#define SPRING_DEFAULT_TURNS 0.458f
#define SPRING_DEFAULT_DAMP 0.061f
#define SPRING_DEFAULT_LEVEL 0.610f

/* One dispersive chain: SPRING_STAGES stretched all-pass sections H(z) = (a + z^-M) / (1 + a z^-M).
 * Exposed so the tests can measure the chirp directly. */
typedef struct {
    float st[SPRING_STAGES * SPRING_MAX_M];
    int m;
    int pos;
} spring_disp;

void spring_disp_init(spring_disp *d, int m);
/* One sample through the chain. a is the section coefficient, |a| < 1. */
float spring_disp_step(spring_disp *d, float x, float a);
/* The same for SPRING_COUNT chains at once, d[0..2], x[0..2] in and y[0..2] out. The chains do not depend on each
 * other, so running them side by side lets the CPU overlap them. Gives exactly the same samples as three calls
 * to spring_disp_step. x and y may be the same array. */
void spring_disp_step3(spring_disp *d, const float *x, float *y, float a);

typedef struct {
    float sr;
    spring_params target;
    float s_tone, s_turns, s_damp, s_level; /* smoothed controls */
    float smooth_coef;
    int ctl_count; /* samples until the next coefficient update */
    int m;         /* all-pass stretch in samples */

    /* derived coefficients, refreshed every SPRING_CTL_PERIOD samples */
    float a;                      /* section coefficient, negative */
    float lp_k;                   /* loop low-pass coefficient */
    float gain[SPRING_COUNT];     /* loop gain per spring (sets the decay time) */
    float send_gain;

    int delay[SPRING_COUNT];
    int wpos[SPRING_COUNT];
    float line[SPRING_COUNT][SPRING_MAX_DELAY];
    spring_disp disp[SPRING_COUNT];

    float fb[SPRING_COUNT];       /* feedback sample waiting to enter each spring */
    float lp[SPRING_COUNT];       /* loop low-pass state */
    float dc_x[SPRING_COUNT], dc_y[SPRING_COUNT]; /* loop DC blocker state */
    float dc_r;
    float in_hp;                  /* send high-pass state */
    float in_hp_k;
    float flip;                   /* alternating noise floor, keeps the tank out of denormals */

    float tail;                   /* decaying peak of the tank output, for the bypass gate */
    float act_peak;               /* strongest tank output since the last spring_take_activity() */
} spring_t;

/* Normalized value to loop low-pass corner in Hz, and to the decay time (RT60, seconds) of the bass. */
float spring_tone_hz(float norm);
float spring_rt60(float damp);

void spring_init(spring_t *s, float sample_rate);
void spring_reset(spring_t *s);
void spring_set_params(spring_t *s, const spring_params *params);

/* Tank output level (0 to about 1) seen since the last call, then cleared. For meters and animation only; it does
 * not affect the audio. Call from the audio thread that runs spring_process. */
float spring_take_activity(spring_t *s);

/* Process in place. Needs two separate buffers; the same pointer must not be passed for both channels.
 * The tank is fed by the mono sum and the wet signal is added to the dry one. With LEVEL at 0 and an
 * empty tank the buffers are left bit-identical. */
void spring_process(spring_t *s, float *left, float *right, int frames);

#ifdef __cplusplus
}
#endif
#endif
