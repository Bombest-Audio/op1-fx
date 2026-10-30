/* DBox DSP core. Portable C99, no allocation after init, no dependencies beyond libm.
 *
 * A new take on the OP-1 dbox drum engine: a kit of 24 synthesized drum keys, two oscillators per hit,
 * 12 voices. Written from scratch as float DSP. It is not a bit-exact copy of the OP-1 fixed-point code
 * and contains no firmware code or tables. See docs/DSP.md for what came from the firmware and what is mine.
 *
 * One instance is one stereo drum machine. Every function is real-time safe. Nothing here is thread safe:
 * call it from one thread (the audio thread), including note_on, note_off and set_params.
 */
#ifndef DBOX_CORE_H
#define DBOX_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define DBOX_KEYS 24
#define DBOX_VOICES 12
#define DBOX_MODES 8
#define DBOX_SINE_SIZE 2048

/* Control values. Normalized 0..1 unless noted. Times are in seconds. */
typedef struct {
    float pitch;     /* -1..1, oscillator pitch offset, +-12 semitones */
    float wave;      /* oscillator 1 shape: 0 sine, 0.5 square, 1 saw */
    float decay;     /* oscillator 1 decay, see dbox_decay_seconds(); the top fifth is a slow attack instead */
    int mode;        /* 0..7, how oscillator 2 meets oscillator 1, see dbox_mode_name() */
    float mix2;      /* oscillator 2 amount (or FM depth, or noise level, depending on mode) */
    float wave2;     /* oscillator 2 shape */
    float decay2;    /* oscillator 2 decay */
    float filter;    /* 0 open, 1 closed low-pass, see dbox_filter_hz() */
    float attack;    /* s, amp envelope gate stage */
    float env_decay; /* s, time to fall 60 dB toward the sustain level */
    float sustain;   /* 0..1 */
    float release;   /* s, time to fall 60 dB after note off */
    float level;     /* 0..1, output level, squared */
    float bend_range;/* semitones for a full pitch bend */
    int root;        /* MIDI note of key 0 */
} dbox_params;

/* OP-1 factory default: knobs 0, 0, 2917, 974 and extras 7167, 0, 2592, 4607 of 32767. */
#define DBOX_DEFAULT_PITCH 0.0f
#define DBOX_DEFAULT_WAVE 0.0f
#define DBOX_DEFAULT_DECAY 0.0890f
#define DBOX_DEFAULT_MODE 0
#define DBOX_DEFAULT_MIX2 0.2187f
#define DBOX_DEFAULT_WAVE2 0.0f
#define DBOX_DEFAULT_DECAY2 0.0791f
#define DBOX_DEFAULT_FILTER 0.1406f
/* Mine, not from the firmware. The long release lets a drum ring out like a one-shot. */
#define DBOX_DEFAULT_ATTACK 0.0005f
#define DBOX_DEFAULT_ENV_DECAY 1.0f
#define DBOX_DEFAULT_SUSTAIN 1.0f
#define DBOX_DEFAULT_RELEASE 3.0f
#define DBOX_DEFAULT_LEVEL 0.8f
#define DBOX_DEFAULT_BEND_RANGE 2.0f
#define DBOX_DEFAULT_ROOT 36

typedef struct {
    int state;       /* 0 idle, 1 playing */
    int key, note;
    int held, sustained;
    int gate;        /* 0 attack, 1 decay, 2 sustain, 3 release */
    int choked;
    int pending;     /* a new note waits for this voice to fade out */
    int p_note, p_key, p_vel;
    unsigned age;
    float vel;
    float g;         /* gate envelope */
    float e1, e2, en;/* body envelopes */
    float c1, c2, cn;/* per-sample decay coefficients */
    float att, att_inc;
    float pe, pe_c;  /* pitch envelope, per control tick */
    float ph1, ph2;
    float inc1, inc2;
    float nz_lp, nz_a;
    float ic1, ic2;
    float fade;
    int kind, ratio_idx;
    float pan_l, pan_r;
} dbox_voice;

typedef struct {
    float sr;
    dbox_params target;
    float s_pitch, s_wave, s_wave2, s_mix2, s_filter, s_level, s_bend; /* smoothed, 5 ms */
    float smooth;
    int tick_left;
    float filt_g, filt_a1, filt_a2, filt_a3;
    float att_inc_gate, dec_c, rel_c;
    float bend_norm, mod_wheel;
    int pedal;
    unsigned serial, rng, notes;
    unsigned key_mask;
    float act_peak;
    dbox_voice voice[DBOX_VOICES];
    float sine[DBOX_SINE_SIZE + 1];
} dbox_t;

typedef struct {
    float peak;      /* largest output sample since the last call */
    int voices;
    unsigned notes;  /* note-on counter */
    unsigned keys;   /* bit k set when key k was struck since the last call */
} dbox_activity;

/* Knob mappings, shared with the plugin and the MPC port. */
float dbox_pitch_semitones(float norm);       /* -12..12 */
float dbox_decay_seconds(float norm);         /* 0.05 to 2.5 s over norm 0..0.8, flat above */
float dbox_attack_seconds(float norm);        /* 0 up to norm 0.8, then up to 80 ms at 1 */
float dbox_filter_hz(float norm);             /* 20 kHz open down to 60 Hz */
const char *dbox_mode_name(int mode);
int dbox_mode_from_norm(float norm);          /* 0..7 */
float dbox_key_freq(int key);                 /* base frequency of a key in Hz */
int dbox_key_for_note(const dbox_params *p, int note); /* 0..23, notes outside the two octaves fold */

void dbox_default_params(dbox_params *p);
void dbox_init(dbox_t *d, float sample_rate);
void dbox_reset(dbox_t *d);                   /* silence everything, clear state, keep parameters */
void dbox_set_params(dbox_t *d, const dbox_params *p);

void dbox_note_on(dbox_t *d, int note, int velocity);  /* 1..127; 0 is a note off */
void dbox_note_off(dbox_t *d, int note);
void dbox_pitch_bend(dbox_t *d, float norm);            /* -1..1 */
void dbox_mod_wheel(dbox_t *d, float norm);             /* 0..1, closes the filter */
void dbox_sustain_pedal(dbox_t *d, int down);
void dbox_all_off(dbox_t *d);                           /* release every note and lift the pedal */

/* Render and overwrite l and r. Any frame count from 1 up, output does not depend on how time is split. */
void dbox_render(dbox_t *d, float *l, float *r, int frames);

int dbox_active_voices(const dbox_t *d);
void dbox_take_activity(dbox_t *d, dbox_activity *out);

#ifdef __cplusplus
}
#endif
#endif
