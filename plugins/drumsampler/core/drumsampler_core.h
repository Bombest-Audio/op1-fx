/* DrumSampler DSP core. Portable C99, no allocation after init, no dependencies beyond libm.
 *
 * A new take on the OP-1 drum sampler engine: 24 keyboard-mapped sample slots, each with its own pitch, in and out
 * point, play mode, direction and level. Written from scratch as float DSP. It is not a bit-exact copy of the OP-1
 * fixed-point code and contains no firmware code or tables. See docs/DSP.md for what came from the firmware and what
 * is my own design.
 *
 * The core never loads audio. The host layer owns the sample memory and hands the core pointers with ds_set_sample().
 * Nothing here is thread safe: call every function from one thread (the audio thread).
 */
#ifndef DRUMSAMPLER_CORE_H
#define DRUMSAMPLER_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define DS_SLOTS 24
#define DS_BASE_NOTE 36    /* MIDI note of slot 0; slot n answers to note 36 + n, so notes 36 to 59 */
#define DS_MAX_PLAYING 8   /* voices that count against polyphony */
#define DS_VOICES 16       /* 8 playing plus up to 8 that are fading out after a steal or a retrigger */
#define DS_TAIL 768        /* frames of pre-rendered fade-out kept per voice, enough for 2.7 ms at 192 kHz */

enum { DS_MODE_GATE = 0, DS_MODE_ONESHOT = 1, DS_MODE_LOOP = 2 };

/* Per slot. A note latches these when it starts; changing them later affects the next note, not the one sounding. */
typedef struct {
    float pitch;   /* semitones, -24 to 24 */
    float in;      /* start of the played region, 0..1 of the sample length */
    float out;     /* end of the played region, 0..1. If in is above out the two swap. */
    int mode;      /* DS_MODE_GATE, DS_MODE_ONESHOT or DS_MODE_LOOP */
    int reverse;   /* nonzero plays the region backwards */
    float gain;    /* linear, 0 to 2 */
} ds_slot_params;

/* Shared by all slots. Times are in seconds. */
typedef struct {
    float attack;      /* s, linear fade-in */
    float decay;       /* s, time for the level to fall 60 dB of the way to the sustain level */
    float sustain;     /* 0..1 */
    float release;     /* s, time to fall 60 dB; used when a key comes up in gate and loop modes */
    float level;       /* 0..1, output level, squared */
    float bend_range;  /* semitones for a full pitch bend */
} ds_params;

#define DS_DEFAULT_PITCH 0.0f
#define DS_DEFAULT_IN 0.0f
#define DS_DEFAULT_OUT 1.0f
#define DS_DEFAULT_MODE DS_MODE_ONESHOT
#define DS_DEFAULT_GAIN 1.0f
/* The ADSR and the level are mine, not from the firmware. The default sustain of 1 makes the envelope transparent. */
#define DS_DEFAULT_ATTACK 0.0005f
#define DS_DEFAULT_DECAY 0.5f
#define DS_DEFAULT_SUSTAIN 1.0f
#define DS_DEFAULT_RELEASE 0.08f
#define DS_DEFAULT_LEVEL 0.85f
#define DS_DEFAULT_BEND_RANGE 2.0f

typedef struct {
    const float *l, *r;   /* r equals l for a mono sample; both NULL for an empty slot */
    int frames;
    float rate;           /* sample rate of the stored audio */
} ds_sample;

typedef struct {
    int state;
    int slot, note;
    int held;             /* key is down */
    int sustained;        /* key came up while the pedal was down */
    unsigned age;         /* note-on serial number */
    const float *l, *r;
    int frames;
    int reverse, loop, oneshot;
    double a, b;          /* played region [a, b) in source frames */
    double len;           /* b - a */
    double xf;            /* loop crossfade length in source frames */
    double period;        /* loop length after the crossfade, len - xf */
    double t;             /* source frames elapsed inside the region */
    double base_inc;      /* source frames per output frame before pitch bend */
    float gain;           /* velocity gain times the slot gain */
    float env, rel_c, att_inc;
    int tail_n, tail_i;   /* pre-rendered fade-out, played back when the sample it came from was replaced */
    float tail_l[DS_TAIL], tail_r[DS_TAIL];
} ds_voice;

typedef struct {
    float sr;
    ds_params target;
    ds_slot_params slot_params[DS_SLOTS];
    ds_sample sample[DS_SLOTS];

    float att_inc, dec_c, fast_c;
    float s_level, level_cur, level_d;   /* smoothed level gain, and the per-sample ramp inside the current tick */
    int tick_left;          /* samples left in the current control tick; 0 forces a new tick */
    float bend_norm;        /* -1..1 as received */
    float bend_s;           /* smoothed, -1..1 */
    double bend_mul;        /* pitch ratio from the smoothed bend, constant within a tick */
    int pedal;
    unsigned serial;

    float act_peak;
    unsigned act_notes;
    unsigned act_hits;      /* bit n is set when slot n started a note since the last ds_take_activity */
    int act_last_slot;

    ds_voice voice[DS_VOICES];
} ds_t;

typedef struct {
    float peak;                   /* largest output sample since the last call, linear */
    int voices;                   /* voices sounding now, fading ones included */
    unsigned notes;               /* note-on counter; changes when a new note starts */
    unsigned hits;                /* bit n set: slot n started a note since the last call */
    unsigned playing;             /* bit n set: slot n has a sounding voice that is not just fading out */
    int last_slot;                /* slot of the most recent note on, -1 if none yet */
    float head[DS_SLOTS];         /* play position inside the sample of slot n, 0..1, or -1 when it is not playing */
} ds_activity;

/* Knob mappings, exposed so the plugin and the MPC port show the same numbers. */
float ds_level_gain(float norm);              /* linear gain, norm squared */
float ds_pitch_value(float norm);             /* 0..1 to -24..24 semitones, 0.5 is 0 */
float ds_pitch_norm(float semitones);         /* the inverse */

void ds_init(ds_t *c, float sample_rate);              /* zero everything, drop all samples and slot settings */
void ds_set_sample_rate(ds_t *c, float sample_rate);   /* keep samples and settings, silence the voices */
void ds_reset(ds_t *c);                                /* silence everything, keep samples and settings */
void ds_default_params(ds_params *p);
void ds_default_slot(ds_slot_params *p);
void ds_set_params(ds_t *c, const ds_params *p);
void ds_set_slot_params(ds_t *c, int slot, const ds_slot_params *p);

/* Hand the core the audio of one slot. The arrays must stay valid until the next call for the same slot returns.
 * After that call the old arrays are no longer read, so the caller may free them at once: voices that were playing the
 * old audio are given a short pre-rendered fade-out first. Pass frames 0 to empty the slot. r may be NULL for mono. */
void ds_set_sample(ds_t *c, int slot, const float *l, const float *r, int frames, float rate);

void ds_note_on(ds_t *c, int note, int velocity);   /* velocity 1..127; 0 is a note off. Notes outside the 24 slots are ignored. */
void ds_note_off(ds_t *c, int note);
void ds_pitch_bend(ds_t *c, float norm);            /* -1..1 */
void ds_sustain_pedal(ds_t *c, int down);
void ds_all_off(ds_t *c);                           /* release every voice, whatever its mode, and lift the pedal */

/* Render and overwrite l and r. Any frame count from 1 up works, and the output does not depend on how a stretch of time
 * is split into calls. Needs two separate buffers. */
void ds_render(ds_t *c, float *l, float *r, int frames);

int ds_active_voices(const ds_t *c);
void ds_take_activity(ds_t *c, ds_activity *out);

#ifdef __cplusplus
}
#endif
#endif
