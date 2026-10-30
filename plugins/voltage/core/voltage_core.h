/* Voltage DSP core. Portable C99, no allocation, no dependencies beyond libm.
 *
 * A new take on the OP-1 voltage synth engine (codename Slump, its screen has "volts"): four self-modulating sine
 * oscillators, an XOR of two rectified sines, a one-pole low-pass and a sync-reset ramp. Written from scratch as float
 * DSP. It is not a bit-exact copy of the OP-1 fixed-point code and contains no firmware code or tables.
 *
 * One instance is one polyphonic instrument (8 voices) with a stereo output. Every function is real-time safe.
 * The functions are not thread safe against each other: call them all from the audio thread.
 * The prefix is opvl_ so it cannot collide with the other engines in this repo.
 */
#ifndef VOLTAGE_CORE_H
#define VOLTAGE_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPVL_VOICES 8
#define OPVL_SINE_SIZE 2048 /* sine table entries, plus one guard entry for interpolation */

/* Control values. xor_amt, feedback, filter and detune are normalized 0..1 (the four OP-1 screen knobs, in the order
 * blue, green, white, orange). The envelope times are seconds, sustain and level are linear. */
typedef struct {
    float xor_amt;  /* blue: how much of the XOR of the two rectified sines is mixed in */
    float feedback; /* green: phase feedback of each sine into itself */
    float filter;   /* white: low-pass cutoff on the oscillator mix, and the slope of the sync ramp */
    float detune;   /* orange: spread of the two outer oscillators around the played pitch */
    float env_attack;
    float env_decay;
    float env_sustain;
    float env_release;
    float level; /* linear output gain */
} opvl_params;

/* OP-1 factory default for the engine, raw knob values 1516, 16704, 0, 15168 out of 32767. */
#define OPVL_DEFAULT_XOR 0.046f
#define OPVL_DEFAULT_FEEDBACK 0.510f
#define OPVL_DEFAULT_FILTER 0.0f
#define OPVL_DEFAULT_DETUNE 0.463f
/* The envelope and level defaults are mine (the OP-1 keeps the envelope in the patch, not in the engine). */
#define OPVL_DEFAULT_ATTACK 0.003f
#define OPVL_DEFAULT_ENV_DECAY 0.4f
#define OPVL_DEFAULT_SUSTAIN 0.8f
#define OPVL_DEFAULT_RELEASE 0.3f
#define OPVL_DEFAULT_LEVEL 1.0f

/* ---- the oscillator engine, one voice's worth ---- */

/* Block constants derived from the four knobs and the sample rate. Rebuild with opvl_eng_prepare() when a knob moves. */
typedef struct {
    float fb;     /* feedback, 0..1 */
    float xor_amt;
    float spread; /* detune spread as a fraction of the pitch (0 to 0.5) */
    float lp;     /* low-pass coefficient per sample */
    float ramp;   /* per-sample step of the sync ramp, scaled by the oscillator level */
    float drift;  /* release drift, cycles per sample added per sample after note-off */
    float smooth; /* coefficient of the output smoothing low-pass (corner near 11 kHz) */
} opvl_eng_params;

typedef struct {
    uint32_t ph[4]; /* sine phases: 0 carrier, 1 lower detuned, 2 upper detuned, 3 pitch twin */
    float yp[4];    /* previous output of oscillators 1..3 (index 0 unused) */
    float ramp;     /* sync-reset ramp state */
    float lp;       /* low-pass state */
    float drift;    /* accumulated release drift, cycles per sample */
    int released;
} opvl_eng_t;

/* Fill a sine table of OPVL_SINE_SIZE + 1 entries (one full cycle plus the first entry again). */
void opvl_sine_fill(float *table);
/* Start a note: phases at zero, ramp and low-pass near their settled values so the note does not start with a DC step. */
void opvl_eng_start(opvl_eng_t *e, const opvl_eng_params *ep);
/* Derive the block constants. Knobs are 0..1. */
void opvl_eng_prepare(opvl_eng_params *ep, float xor_amt, float feedback, float filter, float detune, float sample_rate);
/* Run n samples. inc0 is the pitch in cycles per sample at the first sample, dinc the change per sample. The result is
 * the mix before the output smoothing and DC removal (the voice does those). sine is a table filled by opvl_sine_fill(). */
void opvl_eng_run(opvl_eng_t *e, const opvl_eng_params *ep, const float *sine, float inc0, float dinc, float *out, int n);

/* ---- the instrument ---- */

enum { OPVL_IDLE = 0, OPVL_ON, OPVL_REL, OPVL_STEAL };

typedef struct {
    int state;
    int note;
    int pedal_held; /* note-off arrived while the sustain pedal was down */
    unsigned age;   /* note-on order, for voice stealing */
    int pend_note, pend_vel; /* the note that takes over when a stolen voice has faded out */

    float vel;
    float base_inc; /* cycles per sample at the played pitch, without bend or vibrato */
    float inc_cur;  /* pitch increment now, cycles per sample */
    float dinc;     /* change of the pitch increment per sample over the current control tick */
    opvl_eng_t eng;
    float smooth;   /* output smoothing state */
    float dc_x, dc_y; /* DC blocker state */
    int primed;

    float env, att_inc, dec_coef, rel_coef, sus;
    int stage;
    float fade, fade_step;
    float peak; /* strongest output since opvl_voice_info() */
} opvl_voice;

typedef struct {
    float sr;
    opvl_params target;
    /* smoothed knobs, updated once per control tick */
    float s_xor, s_fb, s_filter, s_detune;
    float s_bend, s_mod, bend_target, mod_target;
    float ctrl_coef; /* one-pole coefficient for a control tick */
    float hp_coef;   /* DC blocker pole */
    float lfo_phase;
    float level_cur, level_coef;
    opvl_eng_params ep;
    int ctrl_left;
    int pedal;
    unsigned age_counter;
    float out_peak;
    float sine[OPVL_SINE_SIZE + 1];
    opvl_voice voice[OPVL_VOICES];
} opvl_t;

/* Pitch of a MIDI note in Hz (A4 = 440). */
float opvl_note_hz(float note);
/* Low-pass cutoff in Hz at 44.1 kHz for a normalized FILTER value (the cutoff stops rising near 17 kHz). */
float opvl_filter_hz(float norm);
/* Detune spread in percent of the pitch for a normalized DETUNE value (the two outer oscillators sit at +-this). */
float opvl_detune_percent(float norm);

void opvl_init(opvl_t *s, float sample_rate);
/* Silence everything, keep the parameters. */
void opvl_reset(opvl_t *s);
void opvl_set_params(opvl_t *s, const opvl_params *params);

/* Notes. note is 0..127, vel 1..127 (vel 0 is a note-off). They take effect at the next rendered sample, so a host
 * splits its block at each event for sample-accurate timing. */
void opvl_note_on(opvl_t *s, int note, int vel);
void opvl_note_off(opvl_t *s, int note);
/* Pitch bend in semitones (the host scales the wheel by its range) and mod wheel 0..1 (vibrato depth). */
void opvl_pitch_bend(opvl_t *s, float semitones);
void opvl_mod_wheel(opvl_t *s, float amount);
void opvl_sustain(opvl_t *s, int down);
/* All notes off: release every voice (the sound rings out through its release). */
void opvl_all_off(opvl_t *s);

/* Render. Overwrites left and right, which must be separate buffers. Any frame count from 1 up. */
void opvl_render(opvl_t *s, float *left, float *right, int frames);

/* For meters and animation only, they do not affect the audio. */
float opvl_take_level(opvl_t *s);                                   /* peak output since the last call, then cleared */
int opvl_voice_info(opvl_t *s, int index, int *note, float *level); /* 1 if the voice sounds; level is its peak since the last call, then cleared */
int opvl_active_voices(const opvl_t *s);

#ifdef __cplusplus
}
#endif
#endif
