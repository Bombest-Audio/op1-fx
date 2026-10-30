/* String DSP core. Portable C99, no allocation, no dependencies beyond libm.
 *
 * A new take on the OP-1 string synth engine: an 8-voice plucked string built from a Karplus-Strong
 * loop (delay line, loop low-pass, dispersion all-pass stages, fractional delay tuned per note).
 * Written from scratch as float DSP. It is not a bit-exact copy of the OP-1 fixed-point code and
 * contains no firmware code or tables.
 *
 * One instance is one polyphonic instrument with a stereo output. Every function is real-time safe.
 * The functions are not thread safe against each other: call them all from the audio thread.
 * The prefix is opst_ because identifiers starting with str are reserved by the C library.
 */
#ifndef STRING_CORE_H
#define STRING_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define OPST_VOICES 8
#define OPST_BUF 16384 /* delay line length per voice, a power of two. Holds a 20 Hz string up to 327 kHz. */

/* Control values. pick, damp, stiff and decay are normalized 0..1 (the four OP-1 screen knobs).
 * The envelope times are seconds, sustain and level are linear. */
typedef struct {
    float pick;  /* where the string is plucked: 0 near the bridge (bright) to 1 at the middle (hollow) */
    float damp;  /* loop low-pass: 0 dark and short-lived highs, 1 bright and ringing */
    float stiff; /* dispersion: 0 an ideal string, 1 stretched, bell-like partials */
    float decay; /* ring time of the string, 0.08 s to 24 s (see opst_decay_seconds) */
    float env_attack;
    float env_decay;
    float env_sustain;
    float env_release;
    float level; /* linear output gain */
} opst_params;

/* OP-1 factory default for the engine, raw knob values 1516, 16704, 0, 15168 out of 32767. */
#define OPST_DEFAULT_PICK 0.046f
#define OPST_DEFAULT_DAMP 0.510f
#define OPST_DEFAULT_STIFF 0.0f
#define OPST_DEFAULT_DECAY 0.463f
/* The envelope and level defaults are mine (the OP-1 keeps the envelope in the patch, not in the engine). */
#define OPST_DEFAULT_ATTACK 0.001f
#define OPST_DEFAULT_ENV_DECAY 1.0f
#define OPST_DEFAULT_SUSTAIN 1.0f
#define OPST_DEFAULT_RELEASE 0.4f
#define OPST_DEFAULT_LEVEL 1.0f

/* Voice states. */
enum { OPST_IDLE = 0, OPST_ON, OPST_REL, OPST_STEAL };

typedef struct {
    int state;
    int note;
    int pedal_held; /* note-off arrived while the sustain pedal was down */
    unsigned age;   /* note-on order, for voice stealing */
    int pend_note, pend_vel; /* the note that takes over when a stolen voice has faded out */

    float freq;     /* last tuned frequency in Hz */
    float d_cur, d_tgt, d_coef;
    float lp_z, ap_z[3];
    float a;        /* dispersion all-pass coefficient for this note (STIFF, limited so the delay stays positive) */
    float b;        /* loop low-pass coefficient for this note (DAMP, cutoff follows the pitch) */
    float g;        /* loop gain, from DECAY and the note */
    int w;          /* write position */
    int quiet;      /* consecutive samples below the silence threshold */

    float env, att_inc, dec_coef, rel_coef, sus;
    int stage;
    float fade, fade_step;

    float pan_l, pan_r;
    float peak;     /* strongest output since opst_voice_info() */
    float buf[OPST_BUF];
} opst_voice;

typedef struct {
    float sr;
    opst_params target;
    /* smoothed knobs (updated once per control tick) */
    float s_damp, s_stiff, s_decay;
    float s_bend, s_mod, bend_target, mod_target;
    float ctrl_coef;   /* one-pole coefficient for a control tick */
    float a_ap;        /* dispersion all-pass coefficient derived from s_stiff */
    float vib;         /* vibrato offset in semitones from the mod wheel */
    float level_cur, level_coef;
    float lfo_phase;
    int ctrl_left;
    int pedal;
    unsigned age_counter;
    unsigned rng;
    unsigned seed;
    float out_peak;
    opst_voice voice[OPST_VOICES];
} opst_t;

/* Ring time (seconds to -60 dB) for a normalized DECAY value, and the pluck position as a fraction of the string. */
float opst_decay_seconds(float norm);
float opst_pick_position(float norm);

void opst_init(opst_t *s, float sample_rate);
/* Silence everything and restart the noise generator from its seed, keep the parameters. */
void opst_reset(opst_t *s);
void opst_set_seed(opst_t *s, unsigned seed);
void opst_set_params(opst_t *s, const opst_params *params);

/* Notes. note is 0..127, vel 1..127 (vel 0 is a note-off). They take effect at the next rendered sample, so
 * a host splits its block at each event for sample-accurate timing. */
void opst_note_on(opst_t *s, int note, int vel);
void opst_note_off(opst_t *s, int note);
/* Pitch bend in semitones (the host scales the wheel by its range) and mod wheel 0..1 (vibrato depth). */
void opst_pitch_bend(opst_t *s, float semitones);
void opst_mod_wheel(opst_t *s, float amount);
void opst_sustain(opst_t *s, int down);
/* All notes off: release every voice (the sound rings out through its release). */
void opst_all_off(opst_t *s);

/* Render. Overwrites left and right, which must be separate buffers. Any frame count from 1 up. */
void opst_render(opst_t *s, float *left, float *right, int frames);

/* For meters and animation only, they do not affect the audio. */
float opst_take_level(opst_t *s);                                     /* peak output since the last call, then cleared */
int opst_voice_info(opst_t *s, int index, int *note, float *level);   /* 1 if the voice sounds; level is its peak since the last call, then cleared */
int opst_active_voices(const opst_t *s);

#ifdef __cplusplus
}
#endif
#endif
