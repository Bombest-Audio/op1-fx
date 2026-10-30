/* Phase DSP core. Portable C99, no allocation after init, no dependencies beyond libm.
 *
 * A new take on the OP-1 "phase" synth engine: a phase-distortion oscillator with four screen
 * knobs (A, B, R, W), eight voices, an amp envelope and a master level. Written from scratch as
 * float DSP. It is not a bit-exact copy of the OP-1 fixed-point code and contains no firmware
 * code or tables. See docs/DSP.md for what came from the firmware and what is my own design.
 *
 * One instance is one polyphonic stereo instrument. Every function is real-time safe. Voice
 * allocation lives here so the JUCE plugin and the MPC port share it.
 */
#ifndef PHASE_CORE_H
#define PHASE_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define PHASE_VOICES 8
#define PHASE_GHOSTS 8   /* stolen voices that are fading out over a few ms */
#define PHASE_CHUNK 64   /* internal render chunk; output does not depend on how the host splits blocks */

/* Control values. a, b, r, w, sustain and level are normalized 0..1. Times are in seconds. */
typedef struct {
    float a;          /* bend of the positive half cycle */
    float b;          /* bend of the negative half cycle */
    float r;          /* resonance: amount and carrier ratio of the resonant partial */
    float w;          /* window: how far the whole cycle fades out towards its end */
    float attack;     /* 0.001 .. 5 s (values below 1 ms are raised to 1 ms) */
    float decay;      /* 0.005 .. 5 s, time to settle most of the way to the sustain level */
    float sustain;    /* 0..1 */
    float release;    /* 0.005 .. 8 s, time to fall 60 dB */
    float level;      /* 0..1 master */
    float bend_range; /* pitch bend range in semitones, 0..12 */
} phase_params;

/* OP-1 factory default for the four knobs: raw 8000, 0, 0, 0 out of 32767. The other four raw values are 0. */
#define PHASE_DEFAULT_A 0.2442f
#define PHASE_DEFAULT_B 0.0f
#define PHASE_DEFAULT_R 0.0f
#define PHASE_DEFAULT_W 0.0f
#define PHASE_DEFAULT_ATTACK 0.005f
#define PHASE_DEFAULT_DECAY 0.35f
#define PHASE_DEFAULT_SUSTAIN 0.7f
#define PHASE_DEFAULT_RELEASE 0.25f
#define PHASE_DEFAULT_LEVEL 0.8f
#define PHASE_DEFAULT_BEND_RANGE 2.0f

typedef struct {
    int stage;      /* 0 off, 1 attack, 2 decay and sustain, 3 release, 4 fade (stolen) */
    int note;
    int sustained;  /* released by the keys while the pedal was down */
    unsigned long age;
    float vel, freq, phase, env;
    float gl, gr;
} phase_voice;

typedef struct {
    float sr;
    phase_params target;
    /* smoothed controls */
    float s_a, s_b, s_r, s_w, s_level;
    float bend, bend_s;  /* -1..1 and its smoothed value */
    float mod, mod_s;    /* 0..1 */
    float smooth_coef, bend_coef, mod_coef;
    /* envelope coefficients, recomputed by phase_set_params */
    float att_inc, dec_c, rel_c, fade_c;
    int pedal;
    unsigned long counter;
    phase_voice voice[PHASE_VOICES];
    phase_voice ghost[PHASE_GHOSTS];
    /* output DC blockers */
    float dc_x[2], dc_y[2], dc_r;
    /* for meters and animation */
    float act_peak;
    int ui_voices;
} phase_t;

/* Defaults for every field of phase_params. */
void phase_default_params(phase_params *p);

void phase_init(phase_t *p, float sample_rate);
void phase_reset(phase_t *p);  /* silence everything and snap smoothed controls to their targets */
void phase_set_params(phase_t *p, const phase_params *params);

/* velocity 0..1. note 0..127. Takes effect at the next sample rendered. */
void phase_note_on(phase_t *p, int note, float velocity);
void phase_note_off(phase_t *p, int note);
void phase_pitch_bend(phase_t *p, float bend);  /* -1..1, times bend_range semitones */
void phase_mod_wheel(phase_t *p, float value);  /* 0..1, adds up to 0.5 to both bends */
void phase_sustain(phase_t *p, int down);       /* sustain pedal */
void phase_all_off(phase_t *p);                 /* release every note and lift the pedal (CC 123) */

/* Overwrite left and right with the next `frames` samples. Any frame count works, including 1. */
void phase_render(phase_t *p, float *left, float *right, int frames);

/* Peak output since the last call (0..1), then cleared, and the number of sounding voices after the last render.
 * For meters and animation only. Call from the thread that calls phase_render. */
float phase_take_activity(phase_t *p, int *voices);

/* One cycle of the oscillator at phase ph (0..1) for knob values a, b, r, w, with no bandlimiting. Used by the
 * editor to draw the wave, and by the tests. Output is within -1..1 and is 0 at ph = 0. */
float phase_shape(float ph, float a, float b, float r, float w);

/* The half-cycle phase warp the oscillator uses: u in 0..1 in, 0..1 out, exponent gamma >= 1 (2^(3a) for knob a).
 * Equal to u at gamma = 1, slope 1 at both ends. Monotonic up to gamma 2.1; above that it can step back by under
 * 0.001 of the half cycle. Exposed for the editor and the tests. */
float phase_warp(float u, float gamma);

/* MIDI note to Hz. */
float phase_note_hz(int note);

#ifdef __cplusplus
}
#endif
#endif
