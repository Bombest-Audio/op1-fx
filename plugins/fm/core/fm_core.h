/* Fm DSP core. Portable C99, no allocation after init, no dependencies beyond libm.
 *
 * A new take on the OP-1 FM synth engine: four sine operators, eight voices, four screen knobs
 * (RATIO, INDEX, FEEDBACK, LAYER) plus an amp envelope and an output level. Written from scratch as
 * float DSP. It is not a bit-exact copy of the OP-1 fixed-point code and contains no firmware code or
 * tables. See docs/DSP.md for what came from the firmware and what is my own design.
 *
 * One instance is one stereo synth. Every function is real-time safe. Nothing here is thread safe:
 * call it from one thread (the audio thread), including note_on, note_off and set_params.
 */
#ifndef FM_CORE_H
#define FM_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define FM_VOICES 8
#define FM_SINE_BITS 11
#define FM_SINE_SIZE (1 << FM_SINE_BITS)

/* Control values. ratio, index, feedback, layer and level are normalized 0..1. Times are in seconds,
 * sustain is 0..1. The last five fields are the engine extras from the OP-1 factory preset (raw values
 * 15000, 0, 100, 1500 of 32767); the plugin and the MPC port keep them at the factory value. */
typedef struct {
    float ratio;      /* modulator frequency ratio, see fm_ratio_value() */
    float index;      /* modulation depth, see fm_index_value() */
    float feedback;   /* modulator self-feedback, see fm_feedback_value() */
    float layer;      /* level of the second operator pair, an octave above the first */
    float attack;     /* s */
    float decay;      /* s, time for the level to fall 60 dB of the way to the sustain level */
    float sustain;    /* 0..1 */
    float release;    /* s, time to fall 60 dB */
    float level;      /* 0..1, output level, squared */
    float mod_decay;  /* 0..1, how long the index takes to relax after a note starts, see fm_mod_decay_seconds() */
    float detune;     /* -1..1, second pair detune, +-30 cents */
    float vel_index;  /* 0..1, how much velocity scales INDEX */
    float key_track;  /* 0..1, how much INDEX falls with pitch */
    float bend_range; /* semitones for a full pitch bend */
} fm_params;

/* OP-1 factory default: knobs 20000, 4500, 3000, 18000 of 32767, extras 15000, 0, 100, 1500. */
#define FM_DEFAULT_RATIO 0.6104f
#define FM_DEFAULT_INDEX 0.1373f
#define FM_DEFAULT_FEEDBACK 0.0916f
#define FM_DEFAULT_LAYER 0.5493f
#define FM_DEFAULT_MOD_DECAY 0.4578f
#define FM_DEFAULT_DETUNE 0.0f
#define FM_DEFAULT_VEL_INDEX 0.0031f
#define FM_DEFAULT_KEY_TRACK 0.0458f
/* Mine, not from the firmware. */
#define FM_DEFAULT_ATTACK 0.002f
#define FM_DEFAULT_DECAY 1.2f
#define FM_DEFAULT_SUSTAIN 0.5f
#define FM_DEFAULT_RELEASE 0.25f
#define FM_DEFAULT_LEVEL 0.8f
#define FM_DEFAULT_BEND_RANGE 2.0f

typedef struct {
    int state;        /* 0 idle, 1 attack, 2 decay and sustain, 3 release */
    int note;
    int held;         /* key is down */
    int sustained;    /* key came up while the pedal was down */
    int fresh;        /* first control tick of a new voice: jump to the target instead of sliding */
    unsigned age;     /* note-on serial number */
    float vel;        /* 0..1 */
    float gain_s;     /* slewed velocity gain */
    float env;
    float gain_cur, gain_d;
    float mx;         /* modulation envelope state, 1 at note start, decays to 0 */
    float idx_s[2];   /* slewed modulation index per pair */
    float idx_cur[2], idx_d[2];
    float ph[4];      /* carrier A, modulator A, carrier B, modulator B, in cycles */
    float inc[4];     /* phase increment in cycles per sample */
    float fb_hist[2][2];
    float b_weight;   /* fades out the second pair when its carrier would pass Nyquist */
    float pan_l, pan_r;
} fm_voice;

typedef struct {
    float sr;
    fm_params target;

    /* smoothed controls (5 ms one-pole) and the per-sample ramps inside the current control tick */
    float s_ratio, s_index, s_fb, s_layer, s_level, s_bend, s_mod;
    float fb_cur, fb_d, layer_cur, layer_d, level_cur, level_d;
    float c_smooth, c_slew;     /* one-pole coefficients per control tick */
    int tick_left;              /* samples left in the current control tick; 0 forces a new tick */
    float mdec;                 /* modulation envelope decay per tick */

    /* envelope constants */
    float att_inc, dec_c, rel_c;

    float bend_norm;            /* -1..1 as received */
    float mod_wheel;            /* 0..1 as received */
    int pedal;
    unsigned serial;
    float act_peak;

    fm_voice voice[FM_VOICES];
    float sine[FM_SINE_SIZE + 1];
} fm_t;

typedef struct {
    float peak;        /* largest output sample since the last call, linear */
    int voices;        /* voices sounding now */
    unsigned notes;    /* note-on counter; changes when a new note starts */
} fm_activity;

/* Knob mappings. Exposed so the plugin and the MPC port can show the same numbers. */
float fm_ratio_value(float norm);         /* 0.5 to 16, snapped 75% toward whole numbers above 1.5 */
float fm_index_value(float norm);         /* radians of peak phase modulation, 0 to 16 */
float fm_feedback_value(float norm);      /* radians, 0 to 2 */
float fm_mod_decay_seconds(float norm);   /* 0.04 to 4 s */
float fm_level_gain(float norm);          /* linear gain, norm squared */

void fm_init(fm_t *f, float sample_rate);
void fm_reset(fm_t *f);                   /* silence everything, clear all state, keep the parameters */
void fm_set_params(fm_t *f, const fm_params *p);
void fm_default_params(fm_params *p);

void fm_note_on(fm_t *f, int note, int velocity);   /* velocity 1..127; 0 is a note off */
void fm_note_off(fm_t *f, int note);
void fm_pitch_bend(fm_t *f, float norm);            /* -1..1 */
void fm_mod_wheel(fm_t *f, float norm);             /* 0..1 */
void fm_sustain_pedal(fm_t *f, int down);
void fm_all_off(fm_t *f);                           /* release every note and lift the pedal */

/* Render and overwrite l and r. Any frame count from 1 up works, and the output does not depend on how
 * a stretch of time is split into calls. Needs two separate buffers. */
void fm_render(fm_t *f, float *l, float *r, int frames);

int fm_active_voices(const fm_t *f);
void fm_take_activity(fm_t *f, fm_activity *out);

#ifdef __cplusplus
}
#endif
#endif
