/* Phone DSP core. Portable C99, no allocation, no dependencies beyond libm.
 *
 * A new take on the OP-1 Phone effect: four controls (TONE, PHONIC, BAUD, TELEMATIC). The input is chopped into
 * fragments of whole waveform cycles, the newest fragment is looped a few times at a different playback rate, and
 * the result is gated by the input level and mixed back over the dry signal. Written from scratch as float DSP. It
 * is not a bit-exact copy of the OP-1 fixed-point code and contains no firmware code or tables.
 *
 * One instance processes one stereo stream. Every function is real-time safe.
 */
#ifndef PHONE_CORE_H
#define PHONE_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define PHONE_MAX_CYCLES 15
#define PHONE_MAX_REPEATS 31
#define PHONE_MAX_SR 192000.0f
/* Longest fragment is PHONE_MAX_CYCLES cycles of a 60 Hz wave, so 0.25 s. Sized for PHONE_MAX_SR. */
#define PHONE_BUF_FRAMES 48128
#define PHONE_TAPS 8
#define PHONE_PHASES 64

/* Control values. tone and telematic are normalized 0..1. phonic is 1..PHONE_MAX_CYCLES, baud is 1..PHONE_MAX_REPEATS. */
typedef struct {
    float tone;
    int phonic;
    int baud;
    float telematic;
} phone_params;

/* OP-1 factory default (raw knob values 8000, 8000, 8000, 16000 out of 32767). The raw value of PHONIC is shifted
 * right by 11 and the raw value of BAUD by 10 in the firmware, which gives 3 and 7. */
#define PHONE_DEFAULT_TONE 0.2441f
#define PHONE_DEFAULT_PHONIC 3
#define PHONE_DEFAULT_BAUD 7
#define PHONE_DEFAULT_TELEMATIC 0.4883f

typedef struct {
    float sr;
    phone_params target;

    /* smoothed controls: playback rate in log2 units, and the mix */
    float s_log2_ratio, t_log2_ratio, s_mix;
    float smooth_coef;
    float ratio, aa; /* playback rate and anti-alias coefficient for s_log2_ratio, recomputed when it moves */
    int ratio_valid;

    /* input conditioning and level follower */
    float dc_a, env_a;
    float dc_lp[2];
    float env;

    /* fragment capture */
    int min_len, max_len;
    float cyc_len; /* samples in 1/60 s */
    int armed;  /* the signal has been below -threshold since the last counted crossing */
    int synced; /* capture is running and its first sample is a crossing */
    int wbuf, spare, ready;
    int wpos, cycles;

    /* playback */
    int rbuf, rpos, reps;
    int len[3];
    float phase;
    int head;
    float ring[2][PHONE_TAPS];
    float lp1[2], lp2[2];

    /* meters for animation only */
    float act_peak;
    int swaps;

    float table[PHONE_PHASES + 1][PHONE_TAPS];
    float buf[3][2][PHONE_BUF_FRAMES];
} phone_t;

/* Playback rate for a normalized TONE value: 16 * v^2, clamped to 1/16..16. Unity is at v = 0.25. */
float phone_tone_ratio(float norm);
/* The same rate in semitones. */
float phone_tone_semitones(float norm);

void phone_init(phone_t *p, float sample_rate);
void phone_reset(phone_t *p);
void phone_set_params(phone_t *p, const phone_params *params);

/* Level of the loop gate (0..1) seen since the last call, then cleared. For meters and animation only; it does not
 * affect the audio. Call from the audio thread that runs phone_process. */
float phone_take_activity(phone_t *p);
/* Number of times playback moved on to a newer fragment since the last call, then cleared. Same rules. */
int phone_take_swaps(phone_t *p);

/* Process in place. Needs two separate buffers; the same pointer must not be passed for both channels. */
void phone_process(phone_t *p, float *left, float *right, int frames);

#ifdef __cplusplus
}
#endif
#endif
