/* Punch DSP core. Portable C99, no allocation, no dependencies beyond libm.
 *
 * A new take on the OP-1 Punch effect: four controls (FREQ, PUNCH, ROUNDS, POWER) and a
 * cascade of transient-emphasis rounds. Written from scratch as float DSP. It is not a
 * bit-exact copy of the OP-1 fixed-point code and contains no firmware code or tables.
 *
 * One instance processes one stereo stream. Every function is real-time safe.
 */
#ifndef PUNCH_CORE_H
#define PUNCH_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define PUNCH_MAX_ROUNDS 24

/* Control values. freq, punch and power are normalized 0..1. rounds is 1..PUNCH_MAX_ROUNDS. */
typedef struct {
    float freq;
    float punch;
    int rounds;
    float power;
} punch_params;

/* OP-1 factory default (raw knob values 6000, 15000, 20000, 28000 out of 32767). */
#define PUNCH_DEFAULT_FREQ 0.183f
#define PUNCH_DEFAULT_PUNCH 0.458f
#define PUNCH_DEFAULT_ROUNDS 19
#define PUNCH_DEFAULT_POWER 0.855f

typedef struct {
    float lp;   /* sidechain low-pass state */
    float ef;   /* fast envelope */
    float es;   /* slow envelope */
    float w;    /* smoothed round enable weight, 0..1 */
} punch_round;

typedef struct {
    float sr;
    punch_params target;
    /* smoothed controls */
    float s_freq, s_punch, s_power;
    float smooth_coef;
    float w_sum; /* smoothed count of active rounds, used to split PUNCH between them */
    float act_peak; /* strongest transient seen by round 0 since the last punch_take_activity() */
    /* per-round constants */
    float af[PUNCH_MAX_ROUNDS], rf[PUNCH_MAX_ROUNDS];
    float as[PUNCH_MAX_ROUNDS], rs[PUNCH_MAX_ROUNDS];
    punch_round round[PUNCH_MAX_ROUNDS];
} punch_t;

/* Map a normalized 0..1 value to a round count 1..PUNCH_MAX_ROUNDS and back. */
int punch_rounds_from_norm(float v);
float punch_rounds_to_norm(int rounds);

/* Sidechain corner frequency in Hz for a normalized FREQ value. */
float punch_freq_hz(float norm);

void punch_init(punch_t *p, float sample_rate);
void punch_reset(punch_t *p);
void punch_set_params(punch_t *p, const punch_params *params);

/* Transient strength (0..3, about 1 for a clear hit) seen since the last call, then cleared. For meters and
 * animation only; it does not affect the audio. Call from the audio thread that runs punch_process. */
float punch_take_activity(punch_t *p);

/* Process in place. Needs two separate buffers; the same pointer must not be passed for both channels. */
void punch_process(punch_t *p, float *left, float *right, int frames);

#ifdef __cplusplus
}
#endif
#endif
