/* DNA DSP core. Portable C99, no allocation after init, no dependencies beyond libm.
 *
 * A new take on the OP-1 dna synth engine: a 128-bit genome played as a one-bit waveform, eight voices,
 * four screen knobs (MUTATE, BITS, SUB, GRIT) plus an amp envelope and an output level. Written from
 * scratch as float/double DSP. It is not a bit-exact copy of the OP-1 fixed-point code and contains no
 * firmware code or tables. See docs/DSP.md for what came from the firmware and what is my own design.
 *
 * One instance is one stereo synth. Every function is real-time safe. Nothing here is thread safe:
 * call it from one thread (the audio thread), including note_on, note_off and set_params.
 */
#ifndef DNA_CORE_H
#define DNA_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define DNA_VOICES 8
#define DNA_GENES 128               /* bits in the genome */
#define DNA_CYCLE_SLOTS 128         /* slots in one cycle of the waveform */
#define DNA_WAVE_SLOTS 256          /* the table covers two cycles, see the SUB knob */

/* Control values. mutate is -1..1, bits, sub, grit and level are 0..1. Times are in seconds, sustain is 0..1.
 * The four knob defaults are the OP-1 factory values (raw -16000, 5000, 0, 1024 of 32767, the first one signed). */
typedef struct {
    float mutate;     /* -1..1: how far the genome has drifted from the original, toward a different mutant each way */
    float bits;       /* 0..1: how many genome bits make up one cycle, see dna_bits_value() */
    float sub;        /* 0..1: how far the second cycle departs from the first (sub-octave energy) */
    float grit;       /* 0..1: random bit errors, redrawn as the cycles go round */
    float attack;     /* s */
    float decay;      /* s, time for the level to fall 60 dB of the way to the sustain level */
    float sustain;    /* 0..1 */
    float release;    /* s, time to fall 60 dB */
    float level;      /* 0..1, output gain */
    float bend_range; /* semitones for a full pitch bend */
} dna_params;

#define DNA_DEFAULT_MUTATE (-0.4883f)  /* -16000 / 32768 */
#define DNA_DEFAULT_BITS 0.1526f       /* 5000 / 32767 */
#define DNA_DEFAULT_SUB 0.0f           /* 0 */
#define DNA_DEFAULT_GRIT 0.0313f       /* 1024 / 32767 */
/* Mine, not from the firmware. */
#define DNA_DEFAULT_ATTACK 0.002f
#define DNA_DEFAULT_DECAY 0.8f
#define DNA_DEFAULT_SUSTAIN 0.7f
#define DNA_DEFAULT_RELEASE 0.25f
#define DNA_DEFAULT_LEVEL 0.8f
#define DNA_DEFAULT_BEND_RANGE 2.0f

/* A band-limited wave: the two-cycle slot table plus its running integrals, see the .c file. */
typedef struct {
    double a[DNA_WAVE_SLOTS];   /* slot values, zero mean */
    double i1[DNA_WAVE_SLOTS];  /* first integral at the start of each slot */
    double j2[DNA_WAVE_SLOTS];  /* second integral at the start of each slot */
    double j2_total;            /* second integral over the whole table */
} dna_wave;

typedef struct {
    int state;          /* 0 idle, 1 attack, 2 decay and sustain, 3 release */
    int note;
    int held;           /* key is down (also true for a note waiting out a steal fade) */
    int sustained;      /* key came up while the pedal was down */
    int fading;         /* fading out before a new note takes this voice over */
    int fresh;          /* first control tick of a new voice */
    int pend_note;
    float pend_vel;
    unsigned age;       /* note-on serial number */
    unsigned rng;       /* this voice's own noise generator */
    float vel;
    float env;
    float fade;         /* 1 normally, ramps to 0 while fading */
    double ph;          /* position in the two-cycle table, 0..1 */
    double inc, inc_d;  /* phase step per sample and its per-sample ramp */
    int own;            /* the voice uses wave_own (grit on) instead of the shared wave */
    unsigned wave_ver;  /* shared wave version the own wave was built from */
    int since_build;    /* samples since wave_own was built */
    unsigned char flip[DNA_GENES]; /* genome bits currently corrupted */
    float pan_l, pan_r;
    dna_wave wave_own;
} dna_voice;

typedef struct {
    float sr;
    dna_params target;

    /* smoothed controls (5 ms one-pole at the control rate) */
    float s_mutate, s_bits, s_sub, s_grit, s_level, s_bend, s_mod;
    float c_smooth;
    float level_cur, level_d;   /* per-sample ramp of the output gain inside a control tick */
    int dirty;          /* a knob that shapes the wave moved: rebuild it at the next tick */
    double semi_off;    /* pitch bend plus vibrato in semitones, as of the last tick */
    int tick_left;      /* samples left in the current control tick; 0 forces a new tick */
    float att_inc, dec_c, rel_c, fade_inc;
    float lfo_ph;       /* vibrato phase, cycles */

    float bend_norm, mod_wheel;
    int pedal;
    unsigned serial;
    unsigned rng;
    float act_peak;

    /* the genome and what the knobs make of it */
    unsigned char genome[3][DNA_GENES]; /* 0 is the original, 1 and 2 are the mutants at MUTATE -1 and +1 */
    float order[DNA_GENES];             /* 0..1, the point along MUTATE at which each bit starts to change */
    double gene[DNA_GENES];             /* the genome as it reads now, -1..1 per bit */
    double nbits;                       /* the BITS knob as a bit count */
    double sub_w;
    double norm;                        /* gain that makes the shared wave peak at 1 */
    unsigned wave_ver;                  /* bumps whenever the shared wave is rebuilt */
    dna_wave wave;

    dna_voice voice[DNA_VOICES];
} dna_t;

typedef struct {
    float peak;        /* largest output sample since the last call, linear */
    int voices;        /* voices sounding now */
    unsigned notes;    /* note-on counter; changes when a new note starts */
} dna_activity;

/* Knob mappings. Exposed so the plugin and the MPC port show the same numbers. */
float dna_bits_value(float norm);         /* genome bits per cycle, 2 to 128 */
float dna_time_seconds(float norm);       /* envelope time knob, 1 ms to 5 s, exponential */
float dna_time_norm(float seconds);       /* the inverse of dna_time_seconds() */

void dna_init(dna_t *d, float sample_rate);
void dna_reset(dna_t *d);                 /* silence everything, clear all state, keep the parameters */
void dna_set_params(dna_t *d, const dna_params *p);
void dna_default_params(dna_params *p);

void dna_note_on(dna_t *d, int note, int velocity);   /* velocity 1..127; 0 is a note off */
void dna_note_off(dna_t *d, int note);
void dna_pitch_bend(dna_t *d, float norm);            /* -1..1 */
void dna_mod_wheel(dna_t *d, float norm);             /* 0..1, vibrato depth */
void dna_sustain_pedal(dna_t *d, int down);
void dna_all_off(dna_t *d);                           /* release every note and lift the pedal */

/* Render and overwrite l and r. Any frame count from 1 up works, and the output does not depend on how
 * a stretch of time is split into calls. Needs two separate buffers. */
void dna_render(dna_t *d, float *l, float *r, int frames);

int dna_active_voices(const dna_t *d);
void dna_take_activity(dna_t *d, dna_activity *out);

#ifdef __cplusplus
}
#endif
#endif
