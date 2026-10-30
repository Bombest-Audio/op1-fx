/* Cwo DSP core. Portable C99, no allocation, no dependencies beyond libm.
 *
 * A new take on the OP-1 CWO effect: a stereo delay line with a single-sideband frequency shifter in its
 * feedback path, and four controls (FREQ, DELAY, FDBACK, SIDEBAND). Written from scratch as float DSP. It is
 * not a bit-exact copy of the OP-1 fixed-point code and contains no firmware code or tables.
 *
 * One instance processes one stereo stream. Every function is real-time safe.
 */
#ifndef CWO_CORE_H
#define CWO_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Delay memory per channel, in samples. A power of two. The longest delay is CWO_MAX_DELAY_MS, or
 * CWO_BUF_SIZE - 16 samples when the sample rate is too high for that to fit (above about 218 kHz). */
#define CWO_BUF_SIZE 131072
#define CWO_MAX_DELAY_MS 600.0f
#define CWO_MIN_DELAY_MS 1.0f
#define CWO_MAX_SHIFT_HZ 4000.0f
#define CWO_MAX_FEEDBACK 0.92f

/* Control values, all normalized 0..1. */
typedef struct {
    float freq;     /* shift amount, 0 Hz to CWO_MAX_SHIFT_HZ, exponential */
    float delay;    /* delay time, CWO_MIN_DELAY_MS to CWO_MAX_DELAY_MS */
    float fdback;   /* feedback gain, 0 to CWO_MAX_FEEDBACK */
    float sideband; /* 0 = lower sideband (shift down), 0.5 = ring modulation, 1 = upper sideband (shift up) */
} cwo_params;

/* OP-1 factory default (raw knob values 5000, 8192, 16384, 32767 out of 32767). */
#define CWO_DEFAULT_FREQ 0.1526f
#define CWO_DEFAULT_DELAY 0.25f
#define CWO_DEFAULT_FDBACK 0.5f
#define CWO_DEFAULT_SIDEBAND 1.0f

/* One allpass section of the Hilbert pair: two samples of input and output history. */
typedef struct {
    float x1, x2, y1, y2;
} cwo_ap;

typedef struct {
    float buf[CWO_BUF_SIZE];
    cwo_ap ap_i[4]; /* in-phase chain */
    cwo_ap ap_q[4]; /* quadrature chain */
    float i_delay;  /* one-sample delay that lines the two chains up at 90 degrees */
    double phase;   /* shifter oscillator, cycles, 0..1 */
} cwo_channel;

typedef struct {
    float sr;
    int max_delay_samples;
    int min_delay_samples;
    int write_pos;
    cwo_params target;
    /* smoothed controls */
    float s_freq, s_fdback, s_side;
    float delay_now; /* delay in samples after slew limiting */
    float smooth_coef, delay_coef;
    float act_peak; /* loudest wet sample since the last cwo_take_activity() */
    cwo_channel ch[2];
} cwo_t;

/* Control curves. */
float cwo_shift_hz(float norm);
float cwo_delay_ms(float norm);
float cwo_feedback_gain(float norm);

void cwo_init(cwo_t *p, float sample_rate);
void cwo_reset(cwo_t *p);
void cwo_set_params(cwo_t *p, const cwo_params *params);

/* Peak level (0..about 1.5) of the delayed, shifted signal since the last call, then cleared. For meters and
 * animation only; it does not affect the audio. Call from the audio thread that runs cwo_process. */
float cwo_take_activity(cwo_t *p);

/* Process in place. Needs two separate buffers; the same pointer must not be passed for both channels. */
void cwo_process(cwo_t *p, float *left, float *right, int frames);

#ifdef __cplusplus
}
#endif
#endif
