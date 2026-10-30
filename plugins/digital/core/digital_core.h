/* Digital DSP core. Portable C99, no allocation after init, no dependencies beyond libm.
 *
 * A new take on the OP-1 "digital" synth engine: a feedback sine oscillator, a second sine oscillator with an
 * octave ratio, detune and hard sync, and a bit-flipping XOR stage. Eight voices, four screen knobs (FEEDBACK,
 * RATIO, DETUNE, CRUNCH) plus an amp envelope and an output level. Written from scratch as float DSP. It is not
 * a bit-exact copy of the OP-1 fixed-point code and contains no firmware code or tables. See docs/DSP.md for
 * what came from the firmware and what is my own design.
 *
 * One instance is one stereo synth. Every function is real-time safe. Nothing here is thread safe:
 * call it from one thread (the audio thread), including note_on, note_off and set_params.
 */
#ifndef DIGITAL_CORE_H
#define DIGITAL_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define DIGITAL_VOICES 8
#define DIGITAL_SINE_BITS 12
#define DIGITAL_SINE_SIZE (1 << DIGITAL_SINE_BITS)

/* Control values. feedback, ratio, crunch and level are normalized 0..1, detune is -1..1 (negative detunes
 * down, positive detunes up and turns hard sync on). Times are in seconds, sustain is 0..1. */
typedef struct {
    float feedback;   /* blue: self phase modulation of the first oscillator, 0 to pi radians */
    float ratio;      /* green: second oscillator octave step (8 steps) and, inversely, its level */
    float detune;     /* white: second oscillator offset, up to +-0.0625 in ratio; above 0 it also hard syncs */
    float crunch;     /* orange: depth of the XOR stage, squared */
    float attack;     /* s */
    float decay;      /* s, time for the level to fall 60 dB of the way to the sustain level */
    float sustain;    /* 0..1 */
    float release;    /* s, time to fall 60 dB */
    float level;      /* 0..1, output level, squared */
    float bend_range; /* semitones for a full pitch bend */
} digital_params;

/* OP-1 factory default: knobs 2000, 5000, -1000, 2000 of 32767. */
#define DIGITAL_DEFAULT_FEEDBACK 0.0610f
#define DIGITAL_DEFAULT_RATIO 0.1526f
#define DIGITAL_DEFAULT_DETUNE (-0.0305f)
#define DIGITAL_DEFAULT_CRUNCH 0.0610f
/* Mine, not from the firmware. */
#define DIGITAL_DEFAULT_ATTACK 0.002f
#define DIGITAL_DEFAULT_DECAY 1.0f
#define DIGITAL_DEFAULT_SUSTAIN 0.6f
#define DIGITAL_DEFAULT_RELEASE 0.2f
#define DIGITAL_DEFAULT_LEVEL 0.8f
#define DIGITAL_DEFAULT_BEND_RANGE 2.0f

typedef struct {
    int state;         /* 0 idle, 1 attack, 2 decay and sustain, 3 release */
    int note;
    int held;          /* key is down */
    int sustained;     /* key came up while the pedal was down */
    int fresh;         /* first control tick of a new voice: jump to the target instead of sliding */
    unsigned age;      /* note-on serial number */
    float vel;         /* 0..1 */
    float gain_s;      /* slewed velocity gain */
    float env;
    float gain_cur, gain_d;   /* gain at the start of the control tick, and its change per sample */
    unsigned ph1, ph2, ph3;   /* oscillator phases, 2^32 = one cycle */
    unsigned inc1, inc2;      /* phase increments per sample, inc2 is also the XOR stage's */
    float y1;                 /* last output of the feedback oscillator */
    float zprev;              /* last output of the second oscillator */
    float xprev;              /* last output of the XOR stage */
    float pan_l, pan_r;
} digital_voice;

typedef struct {
    float sr;
    digital_params target;

    /* smoothed controls (5 ms one-pole) and the per-sample ramps inside the current control tick */
    float s_fb, s_ratio, s_detune, s_crunch, s_level, s_bend, s_mod;
    float lr2;                  /* log2 of the second oscillator ratio, glides so octave steps do not jump */
    float fb_cur, fb_d, lev2_cur, lev2_d, crunch_cur, crunch_d, level_cur, level_d;  /* value at tick start, change per sample */
    int sync_on;
    float c_smooth, c_slew, c_glide;   /* one-pole coefficients per control tick */
    int tick_left;                     /* samples left in the current control tick; 0 forces a new tick */
    int tick_pos;                      /* samples already rendered in the current control tick */

    /* envelope constants */
    float att_inc, dec_c, rel_c;

    float bend_norm;            /* -1..1 as received */
    float mod_wheel;            /* 0..1 as received */
    int pedal;
    unsigned serial;
    float act_peak;

    digital_voice voice[DIGITAL_VOICES];
    float sine[DIGITAL_SINE_SIZE + 1];
} digital_t;

typedef struct {
    float peak;        /* largest output sample since the last call, linear */
    int voices;        /* voices sounding now */
    unsigned notes;    /* note-on counter; changes when a new note starts */
} digital_activity;

/* Knob mappings. Exposed so the plugin and the MPC port show the same numbers. */
int digital_octave_step(float ratio_norm);                 /* -3 to 4: the second oscillator is 2^step times the pitch */
float digital_osc2_ratio(float ratio_norm, float detune);  /* 2^step + detune / 16 */
float digital_osc2_level(float ratio_norm);                /* 1 at the bottom of the knob, 0 at the top */
int digital_sync_active(float detune);                     /* nonzero when DETUNE is above the sync threshold */
float digital_crunch_depth(float crunch_norm);             /* XOR depth as a share of full scale, norm squared */
float digital_feedback_radians(float feedback_norm);       /* 0 to pi */
float digital_level_gain(float norm);                      /* linear gain, norm squared */

void digital_init(digital_t *d, float sample_rate);
void digital_reset(digital_t *d);                 /* silence everything, clear all state, keep the parameters */
void digital_set_params(digital_t *d, const digital_params *p);
void digital_default_params(digital_params *p);

void digital_note_on(digital_t *d, int note, int velocity);   /* velocity 1..127; 0 is a note off */
void digital_note_off(digital_t *d, int note);
void digital_pitch_bend(digital_t *d, float norm);            /* -1..1 */
void digital_mod_wheel(digital_t *d, float norm);             /* 0..1 */
void digital_sustain_pedal(digital_t *d, int down);
void digital_all_off(digital_t *d);                           /* release every note and lift the pedal */

/* Render and overwrite l and r. Any frame count from 1 up works, and the output does not depend on how a
 * stretch of time is split into calls. Needs two separate buffers. */
void digital_render(digital_t *d, float *l, float *r, int frames);

int digital_active_voices(const digital_t *d);
void digital_take_activity(digital_t *d, digital_activity *out);

#ifdef __cplusplus
}
#endif
#endif
