/* Cluster DSP core. Portable C99, no allocation, no dependencies beyond libm.
 *
 * A new take on the OP-1 cluster synth engine: a bundle of up to 8 sine oscillators with self-feedback
 * phase modulation, all playing the same note and each wandering slightly out of tune. Written from scratch
 * as float DSP. It is not a bit-exact copy of the OP-1 fixed-point code and contains no firmware code or tables.
 *
 * One instance is a complete 8-voice instrument with its own amp envelope. Voice allocation, note handling and the
 * MIDI controllers live here so the plugin and the MPC port share them. Every function is real-time safe.
 */
#ifndef CLUSTER_CORE_H
#define CLUSTER_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLUSTER_VOICES 8
#define CLUSTER_OSCS 8
#define CLUSTER_SINE_SIZE 4096
#define CLUSTER_MAX_TICK 1024

/* Controls. The four engine knobs follow the OP-1 screen (blue, green, white, orange):
 *   oscs      1..8 oscillators in the cluster
 *   feedback  0..1, sets the feedback amount and how it drifts over the note (see docs/DSP.md)
 *   spread    0..1, how long each oscillator's detune lasts before it settles back toward the played pitch
 *   wander    0..1, how hard the detune is kicked around at random
 * attack, decay and release are normalized 0..1 and map to 1 ms .. 5 s through cluster_env_time(). */
typedef struct {
    int oscs;
    float feedback, spread, wander;
    float attack, decay, sustain, release;
    float level;       /* 0..1, output gain is 2 * level^2 so 0.707 is unity */
    float bend_range;  /* semitones, 0..12 */
} cluster_params;

/* OP-1 factory default for the four knobs (raw 4000, 4000, 8000, 100 of 32767). The raw first knob 4000 selects 1 oscillator. */
#define CLUSTER_DEFAULT_OSCS 1
#define CLUSTER_DEFAULT_FEEDBACK 0.1221f
#define CLUSTER_DEFAULT_SPREAD 0.2441f
#define CLUSTER_DEFAULT_WANDER 0.00305f
/* My own defaults for the rest: 5 ms attack, 400 ms decay to 0.8, 250 ms release. */
#define CLUSTER_DEFAULT_ATTACK 0.189f
#define CLUSTER_DEFAULT_DECAY 0.703f
#define CLUSTER_DEFAULT_SUSTAIN 0.8f
#define CLUSTER_DEFAULT_RELEASE 0.648f
#define CLUSTER_DEFAULT_LEVEL 0.7f
#define CLUSTER_DEFAULT_BEND 2.0f

typedef struct {
    int stage;            /* see VS_* in cluster_core.c */
    int note;
    int held;             /* key is down */
    int sustained;        /* key is up but the pedal holds the note */
    int released;         /* in the release stage; the engine settles down while released */
    int pending_note;     /* note to start once a stolen voice has faded out, -1 if none */
    float pending_vel;
    uint32_t age;
    float vel;
    float env, steal_dec;
    float acc;            /* feedback drift accumulator, -1..1 */
    float norm;           /* smoothed level scale for the oscillator count */
    uint32_t phase[CLUSTER_OSCS];
    float y[CLUSTER_OSCS];
    float g[CLUSTER_OSCS];       /* smoothed oscillator gate */
    float rnd[CLUSTER_OSCS];     /* detune as a fraction of the played frequency */
    float det_cur[CLUSTER_OSCS], det_next[CLUSTER_OSCS], det_step[CLUSTER_OSCS];
    float amp_cur[CLUSTER_OSCS], amp_next[CLUSTER_OSCS], amp_step[CLUSTER_OSCS];
    float fb_cur, fb_next, fb_step;
} cluster_voice;

typedef struct {
    float sr;
    cluster_params target;
    int tick_len, tick_pos;
    float bend, bend_s, mod;
    int pedal;
    uint32_t rng, age_counter;
    float gain_s, gain_coef;
    float tick_seconds;
    float pan_l[CLUSTER_OSCS], pan_r[CLUSTER_OSCS];
    float out_peak;
    float osc_act[CLUSTER_OSCS];
    cluster_voice voice[CLUSTER_VOICES];
    float sine[CLUSTER_SINE_SIZE + 1];
    float vl[CLUSTER_MAX_TICK], vr[CLUSTER_MAX_TICK], venv[CLUSTER_MAX_TICK];
} cluster_t;

/* Time in seconds for a normalized attack, decay or release value (1 ms to 5 s, exponential) and back. */
float cluster_env_time(float norm);
float cluster_env_norm(float seconds);

/* The two curves the FEEDBACK knob draws, for tests and docs: A is the starting feedback, B the drift direction. */
void cluster_feedback_path(float knob, float *a, float *b);
/* Time constant in seconds of the detune memory for a SPREAD value. */
float cluster_spread_tau(float spread);

void cluster_init(cluster_t *c, float sample_rate);
/* Silence everything at once and clear all voice state. Keeps the parameters and the sample rate. */
void cluster_reset(cluster_t *c);
void cluster_seed(cluster_t *c, uint32_t seed);
void cluster_set_params(cluster_t *c, const cluster_params *p);

void cluster_note_on(cluster_t *c, int note, int velocity); /* velocity 0 is a note off */
void cluster_note_off(cluster_t *c, int note);
void cluster_pitch_bend(cluster_t *c, float value);  /* -1..1 */
void cluster_mod_wheel(cluster_t *c, float value);   /* 0..1, adds to the feedback */
void cluster_sustain(cluster_t *c, int down);
void cluster_all_off(cluster_t *c);                  /* release every note (the pedal is lifted too) */
/* Parse one 2 or 3 byte MIDI message (any channel): notes, pitch bend, CC 1, 64, 120, 121, 123. */
void cluster_midi(cluster_t *c, const uint8_t *msg, int len);

/* Overwrites left and right with `frames` samples. Two separate buffers, any frame count from 1 up. */
void cluster_render(cluster_t *c, float *left, float *right, int frames);

/* For meters and animation only, call from the audio thread. Peak output since the last call, then cleared. */
float cluster_take_level(cluster_t *c);
/* Per-oscillator strength (0..1) seen since the last call, then cleared. Returns the number of sounding voices. */
int cluster_take_activity(cluster_t *c, float act[CLUSTER_OSCS]);

#ifdef __cplusplus
}
#endif
#endif
