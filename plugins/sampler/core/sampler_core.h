/* Sampler DSP core. Portable C99, no allocation after init, no dependencies beyond libm.
 *
 * A new take on the OP-1 sampler synth engine: eight voices that play one mono sample across the keyboard, four screen
 * knobs (START, END, TUNE, LOOP) plus an amp envelope and an output level. Written from scratch as float DSP. It is not
 * a bit-exact copy of the OP-1 fixed-point code and contains no firmware code or tables. See docs/DSP.md for what came
 * from the firmware and what is my own design.
 *
 * The core never owns sample memory. The caller hands it a read-only view with sampler_set_sample() and keeps that
 * memory valid until the next sampler_set_sample() call (or until sampler_all_off() and a render have run, see the
 * lifetime note there). Everything here is real-time safe. Nothing is thread safe: call it from one thread (the audio
 * thread), including note_on, note_off, set_sample and set_params.
 */
#ifndef SAMPLER_CORE_H
#define SAMPLER_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLER_VOICES 8
#define SAMPLER_TICK 16 /* frames between control updates (smoothing, vibrato) */

/* Read-only mono sample view. data stays owned by the caller. rate is the sample rate the data was recorded at. */
typedef struct {
    const float *data;
    int frames;
    float rate;
} sampler_sample;

/* Control values. start, end, loop, sustain and level are 0..1. tune is in semitones. Times are in seconds. */
typedef struct {
    float start;      /* where playback begins, as a fraction of the sample length */
    float end;        /* where the played region ends, as a fraction of the sample length */
    float tune;       /* semitones, -12..+12, added to the note */
    float loop;       /* 0 plays once. Above 0 loops start..end and the value sets the crossfade length, see below */
    float attack;     /* s */
    float decay;      /* s, time for the level to fall 60 dB of the way to the sustain level */
    float sustain;    /* 0..1 */
    float release;    /* s, time to fall 60 dB */
    float level;      /* 0..1, output level, squared */
    float root;       /* MIDI note at which the sample plays at its recorded pitch */
    float bend_range; /* semitones for a full pitch bend */
} sampler_params;

/* OP-1 has no factory sampler row in synth_types, so these are my own choices. */
#define SAMPLER_DEFAULT_START 0.0f
#define SAMPLER_DEFAULT_END 1.0f
#define SAMPLER_DEFAULT_TUNE 0.0f
#define SAMPLER_DEFAULT_LOOP 0.0f
#define SAMPLER_DEFAULT_ATTACK 0.002f
#define SAMPLER_DEFAULT_DECAY 1.5f
#define SAMPLER_DEFAULT_SUSTAIN 1.0f
#define SAMPLER_DEFAULT_RELEASE 0.25f
#define SAMPLER_DEFAULT_LEVEL 0.8f
#define SAMPLER_DEFAULT_ROOT 60.0f
#define SAMPLER_DEFAULT_BEND_RANGE 2.0f

#define SAMPLER_TUNE_RANGE 12.0f /* tune spans +- this many semitones */

typedef struct {
    int state;        /* 0 idle, 1 attack, 2 decay and sustain, 3 release */
    int note;
    int held;         /* key is down */
    int sustained;    /* key came up while the pedal was down */
    unsigned age;     /* note-on serial number */
    float vel;        /* 0..1 */
    float env;
    double pos;       /* read position in source frames */
    double base_inc;  /* source frames per output frame at the note's pitch, before tune and bend */
    int start, end;   /* played region in source frames, end exclusive */
    int loop;         /* looping on */
    int xf;           /* crossfade length in source frames */
    int fade;         /* length of the fade at the end of a one-shot, source frames */
    float last;       /* last output sample, for click-free stealing */
} sampler_voice;

typedef struct {
    float sr;
    sampler_params target;
    sampler_sample sample;

    float s_semi, s_level;      /* smoothed tune+bend (semitones) and level */
    float pm_cur, pm_d;         /* pitch multiplier for the current tick and its per-sample ramp */
    float level_cur, level_d;
    float c_smooth;
    int tick_left;
    float vib_phase;

    float att_inc, dec_c, rel_c;
    float resid, resid_c;       /* decaying remainder of a stolen or cut voice */

    float bend_norm;
    float mod_wheel;
    int pedal;
    unsigned serial;
    float act_peak;
    float act_head;

    sampler_voice voice[SAMPLER_VOICES];
} sampler_t;

typedef struct {
    float peak;        /* largest output sample since the last call, linear */
    int voices;        /* voices sounding now */
    unsigned notes;    /* note-on counter; changes when a new note starts */
    float head;        /* read position of the newest sounding voice as a fraction of the sample, -1 when silent */
} sampler_activity;

/* Knob mappings, shared by the plugin and the MPC port. */
float sampler_level_gain(float norm);              /* linear gain, norm squared */
float sampler_tune_from_norm(float norm);          /* 0..1 knob to -12..+12 semitones, 0.5 is 0 */
float sampler_norm_from_tune(float semitones);

void sampler_init(sampler_t *s, float sample_rate);
void sampler_reset(sampler_t *s);                  /* silence everything, clear all state, keep params and sample */
void sampler_set_params(sampler_t *s, const sampler_params *p);
void sampler_default_params(sampler_params *p);

/* Swap the sample. Voices are cut at once (their last output decays over about 2 ms, so there is no click). The old
 * memory is no longer read after this returns, so the caller may free it. A NULL sample or fewer than 2 frames
 * means no sample: notes are accepted and stay silent. */
void sampler_set_sample(sampler_t *s, const sampler_sample *smp);

void sampler_note_on(sampler_t *s, int note, int velocity);   /* velocity 1..127; 0 is a note off */
void sampler_note_off(sampler_t *s, int note);
void sampler_pitch_bend(sampler_t *s, float norm);            /* -1..1 */
void sampler_mod_wheel(sampler_t *s, float norm);             /* 0..1, vibrato depth */
void sampler_sustain_pedal(sampler_t *s, int down);
void sampler_all_off(sampler_t *s);                           /* release every note and lift the pedal */

/* Render and overwrite l and r. Any frame count from 1 up works, and the output does not depend on how a stretch of
 * time is split into calls. Needs two separate buffers. */
void sampler_render(sampler_t *s, float *l, float *r, int frames);

int sampler_active_voices(const sampler_t *s);
void sampler_take_activity(sampler_t *s, sampler_activity *out);

/* Built-in generated samples, so there is something to play without any file. All are mine, made from sines, a little
 * deterministic noise and decay curves, no recordings. kind 0 is a struck bar (C4), kind 1 a loopable pad that repeats
 * exactly once per second (about C4), kind 2 a plucked low string (C2). Use sampler_demo_frames() to size the buffer,
 * then sampler_demo_render(). root is the MIDI note each one is tuned to. */
#define SAMPLER_DEMO_KINDS 3
int sampler_demo_frames(int kind, float rate);
void sampler_demo_render(int kind, float rate, float *out, int frames);
int sampler_demo_root(int kind);
const char *sampler_demo_name(int kind);

#ifdef __cplusplus
}
#endif
#endif
