/* DSynth DSP core. Portable C99, no allocation after init, no dependencies beyond libm.
 *
 * A new take on the OP-1 dsynth engine: two oscillators (the second one phase-modulates the first), a drum-style
 * pitch drop, per-oscillator attack/decay envelopes and a seven-mode filter, eight voices. Four screen knobs
 * (MOD, WAVE, DECAY, FILTER) plus a plugin-side amp envelope and an output level. Written from scratch as float
 * DSP. It is not a bit-exact copy of the OP-1 fixed-point code and contains no firmware code or tables. See
 * docs/DSP.md for what came from the firmware and what is my own design: the firmware decompile for dsynth is
 * only three small functions, so the signal path here is designed from the OP-1 screen and the factory values.
 *
 * One instance is one stereo synth. Every function is real-time safe. Nothing here is thread safe: call it from
 * one thread (the audio thread), including note_on, note_off and set_params.
 */
#ifndef DSYNTH_CORE_H
#define DSYNTH_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define DSYNTH_VOICES 8
#define DSYNTH_TICK 16 /* control tick length in samples: envelopes' constants, filter and pitch ramps update here */
#define DSYNTH_FILTER_MODES 8

/* Control values. mod, wave, decay, osc2_*, cutoff and level are normalized 0..1. filter_mode is 0..7. Times are
 * in seconds, sustain is 0..1. The osc2_* and cutoff fields are the engine extras from the OP-1 factory preset
 * (raw values 328, 0, 12130, 11465 of 32767); the plugin and the MPC port keep them at the factory value. */
typedef struct {
    float mod;         /* oscillator 2 phase-modulates oscillator 1; also set by the mod wheel, see dsynth_mod_depth() */
    float wave;        /* oscillator 1 shape: sine at 0, square at 0.5, saw at 1, blended in between */
    float decay;       /* oscillator 1 envelope: short to long decay below 0.5, then a growing attack */
    int filter_mode;   /* 0 off, 1 low-pass 12 dB, 2 low-pass 24 dB, 3 band-pass, 4 narrow band-pass, 5 high-pass,
                          6 notch, 7 resonant low-pass */
    float osc2_pitch;  /* oscillator 2 pitch above oscillator 1, 0 to 3 octaves, see dsynth_osc2_ratio() */
    float osc2_wave;   /* oscillator 2 shape, same scale as wave */
    float osc2_decay;  /* oscillator 2 envelope, same scale as decay; it shapes the modulation depth and the pitch drop */
    float cutoff;      /* filter corner above the played note, 2 to 64 times its frequency, see dsynth_cutoff_ratio() */
    float amp_attack;  /* s */
    float amp_decay;   /* s, time for the amp level to fall 60 dB of the way to the sustain level */
    float amp_sustain; /* 0..1 */
    float amp_release; /* s, time to fall 60 dB */
    float level;       /* 0..1, output level, squared */
    float bend_range;  /* semitones for a full pitch bend */
} dsynth_params;

/* OP-1 factory default: knobs 20664, 0, 8528, 6225 of 32767, extras 328, 0, 12130, 11465. */
#define DSYNTH_DEFAULT_MOD 0.6306f
#define DSYNTH_DEFAULT_WAVE 0.0f
#define DSYNTH_DEFAULT_DECAY 0.2603f
#define DSYNTH_DEFAULT_FILTER_MODE 1 /* raw 6225 of 32767 on a 0..7 dial is 1.33, so position 1 */
#define DSYNTH_DEFAULT_OSC2_PITCH 0.0100f
#define DSYNTH_DEFAULT_OSC2_WAVE 0.0f
#define DSYNTH_DEFAULT_OSC2_DECAY 0.3702f
#define DSYNTH_DEFAULT_CUTOFF 0.3499f
/* Mine, not from the firmware. */
#define DSYNTH_DEFAULT_AMP_ATTACK 0.001f
#define DSYNTH_DEFAULT_AMP_DECAY 1.0f
#define DSYNTH_DEFAULT_AMP_SUSTAIN 1.0f
#define DSYNTH_DEFAULT_AMP_RELEASE 0.25f
#define DSYNTH_DEFAULT_LEVEL 0.8f
#define DSYNTH_DEFAULT_BEND_RANGE 2.0f

typedef struct {
    int alive;
    int note;
    int held;           /* key is down */
    int sustained;      /* key came up while the pedal was down */
    unsigned age;       /* note-on serial number */
    /* a steal in progress: the old sound fades out over about 2 ms, then the pending note starts */
    int stealing;
    float fade, fade_step;
    int pend_note, pend_vel, pend_held;
    float vel;          /* 0..1 */
    float f0;           /* played note in Hz, no bend */
    int st1, st2;       /* oscillator envelopes: 1 attack, 2 decay */
    float e1, e2;
    int ast;            /* amp envelope: 1 attack, 2 decay and sustain, 3 release */
    float ae;
    float p1, p2;       /* phases in cycles */
    float inc1_0, dinc1, inc2_0, dinc2; /* phase increment at the tick start and its change per sample */
    float a1, a2, a3;   /* filter coefficients for this tick */
    float f1_ic1, f1_ic2, f2_ic1, f2_ic2; /* two filter stages */
    float pan_l, pan_r;
} dsynth_voice;

typedef struct {
    float sr;
    dsynth_params target;

    /* controls smoothed once per tick (5 ms one-pole); the derived values below are rebuilt from them every tick */
    float s_mod, s_wave, s_wave2, s_osc2_pitch, s_cutoff, s_level, s_bend, s_wheel;
    float s_w[5];       /* filter mix: dry, low-pass 1, band-pass (unity peak), high-pass, low-pass 2 */
    float s_k;          /* filter damping, 1/Q */
    float c_smooth;     /* one-pole coefficient per tick */

    int tick_pos;       /* samples already rendered in the current tick, 0..DSYNTH_TICK-1 */
    float w0[5], dw[5]; /* mix weights at the tick start and their change per sample */
    float m0, dm;       /* master gain at the tick start and its change per sample */
    float ainc1, dc1, ainc2, dc2;       /* per-sample attack step and decay factor of the two oscillator envelopes */
    float aa_inc, ad_coef, ar_coef, a_sus; /* amp envelope */
    float ratio2, cut_rel, idx_base, bend_mul, wave1, wave2, fade_step;

    float bend_norm;    /* -1..1 as received */
    float wheel;        /* 0..1 as received */
    int pedal;
    unsigned serial;
    float act_peak;

    dsynth_voice voice[DSYNTH_VOICES];
} dsynth_t;

typedef struct {
    float peak;        /* largest output sample since the last call, linear */
    int voices;        /* voices sounding now */
    unsigned notes;    /* note-on counter; changes when a new note starts */
} dsynth_activity;

/* Knob mappings. Exposed so the plugin and the MPC port can show the same numbers. */
float dsynth_decay_seconds(float knob);   /* 0.015 to 2 s over the first half of the knob, then 2 s */
float dsynth_attack_seconds(float knob);  /* 0.001 s up to the middle of the knob, then rising to 0.5 s */
float dsynth_mod_depth(float norm);       /* radians of peak phase modulation, 0 to 9 */
float dsynth_osc2_ratio(float norm);      /* 1 to 8 */
float dsynth_cutoff_ratio(float norm);    /* 2 to 64 times the note frequency */
float dsynth_level_gain(float norm);      /* linear gain before the output limiter */
const char *dsynth_filter_name(int mode); /* "OFF", "LP12", ... */

void dsynth_init(dsynth_t *d, float sample_rate);
void dsynth_reset(dsynth_t *d);           /* silence everything, clear all state, keep the parameters */
void dsynth_set_params(dsynth_t *d, const dsynth_params *p);
void dsynth_default_params(dsynth_params *p);

void dsynth_note_on(dsynth_t *d, int note, int velocity);   /* velocity 1..127; 0 is a note off */
void dsynth_note_off(dsynth_t *d, int note);
void dsynth_pitch_bend(dsynth_t *d, float norm);            /* -1..1 */
void dsynth_mod_wheel(dsynth_t *d, float norm);             /* 0..1, adds to MOD */
void dsynth_sustain_pedal(dsynth_t *d, int down);
void dsynth_all_off(dsynth_t *d);                           /* release every note and lift the pedal */

/* Render and overwrite l and r. Any frame count from 1 up works, and the output does not depend on how
 * a stretch of time is split into calls. Needs two separate buffers. */
void dsynth_render(dsynth_t *d, float *l, float *r, int frames);

int dsynth_active_voices(const dsynth_t *d);
void dsynth_take_activity(dsynth_t *d, dsynth_activity *out);

#ifdef __cplusplus
}
#endif
#endif
