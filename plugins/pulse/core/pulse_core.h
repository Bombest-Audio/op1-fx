/* Pulse DSP core. Portable C99, no allocation after init, no dependencies beyond libm.
 *
 * A new take on the OP-1 Pulse synth engine: a polyphonic pulse-width-modulated synth with four
 * engine controls (SHAPE, SWEEP, RATE, DETUNE), an amp envelope and an output level. Written from
 * scratch as float DSP. It is not a bit-exact copy of the OP-1 fixed-point code and contains no
 * firmware code or tables. Voice allocation lives here so the JUCE plugin and the MPC port share it.
 *
 * One instance is one stereo instrument. Every function is real-time safe. Not thread safe: call all
 * of it from the audio thread (the plugin hands MIDI and parameters over on that thread).
 */
#ifndef PULSE_CORE_H
#define PULSE_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define PULSE_VOICES 8

/* Control values. shape, sweep, rate, attack, decay, sustain, release and level are normalized 0..1.
 * detune is bipolar -1..1. bend_range is in semitones (1..12). */
typedef struct {
    float shape;    /* resting pulse width: 0 is a square wave, 1 is a thin pulse */
    float sweep;    /* depth of the pulse-width LFO */
    float rate;     /* speed of the pulse-width LFO, exponential 0.2 to 25.6 Hz */
    float detune;   /* spread between the two oscillators of a voice, +-100 cents at the ends */
    float attack;   /* 0.5 ms to 4 s, exponential */
    float decay;    /* 5 ms to 4 s (time to fall 60 dB toward the sustain level), exponential */
    float sustain;  /* 0..1 */
    float release;  /* 5 ms to 6 s (time to fall 60 dB), exponential */
    float level;    /* output gain, 0..1 */
    float bend_range;
} pulse_params;

/* OP-1 factory defaults for the four engine knobs (raw 20000, 16000, 4000, 3000 of 32767). */
#define PULSE_DEFAULT_SHAPE 0.61038f
#define PULSE_DEFAULT_SWEEP 0.48829f
#define PULSE_DEFAULT_RATE 0.12207f
#define PULSE_DEFAULT_DETUNE 0.09155f
/* The envelope, level and bend range are not from the OP-1 factory table: my own starting point. */
#define PULSE_DEFAULT_ATTACK 0.15f
#define PULSE_DEFAULT_DECAY 0.55f
#define PULSE_DEFAULT_SUSTAIN 0.8f
#define PULSE_DEFAULT_RELEASE 0.55f
#define PULSE_DEFAULT_LEVEL 0.8f
#define PULSE_DEFAULT_BEND_RANGE 2.0f

#define PULSE_CTRL 16 /* samples between control-rate updates (per-sample smoothing runs in between) */

typedef struct {
    int state; /* 0 idle, 1 attack, 2 decay, 3 sustain, 4 release */
    int note;
    int gate;      /* key is down (or held by the pedal) */
    int pedal;     /* key is up but the sustain pedal is holding the voice */
    int fading;    /* in the short fade-out before a steal or a kill */
    int pending;   /* a note waits for the fade to finish */
    int pend_note;
    int pend_gate; /* 0 if that note's key already went up */
    float pend_gain;
    float gain;    /* velocity gain of the sounding note */
    float env;
    float fade;    /* 1 normally, ramps to 0 while fading */
    float base;    /* note frequency / sample rate */
    float inc_a, inc_b; /* phase increments of the two oscillators */
    float ph_a, ph_b, ph_lfo;
    unsigned long age;
} pulse_voice;

typedef struct {
    float sr;
    pulse_params target;
    /* smoothed controls */
    float s_wbase, s_sweep, s_rate, s_detune, s_level, s_bend;
    float smooth_coef; /* per-sample one-pole coefficient, about 5 ms */
    float bend_in;     /* pitch wheel, -1..1 */
    float mod_in;      /* mod wheel, 0..1 */
    int pedal_down;
    int ctrl_pos;      /* samples already used in the current control interval */
    /* per-interval derived values */
    float bend_ratio, ratio_a, ratio_b, lfo_inc;
    /* envelope constants */
    float att_inc, dec_coef, rel_coef;
    float fade_dec;
    unsigned long clock;
    float note_base[128];
    float act_peak;
    pulse_voice v[PULSE_VOICES];
} pulse_t;

/* Map normalized values to physical units. Used by the plugin displays and the tests. */
float pulse_attack_s(float norm);
float pulse_decay_s(float norm);
float pulse_release_s(float norm);
float pulse_rate_hz(float norm);
float pulse_detune_cents(float norm);  /* total spread between the two oscillators */
float pulse_duty_pct(float shape);     /* resting duty cycle in percent (50 for a square wave) */

void pulse_init(pulse_t *p, float sample_rate); /* also resets */
void pulse_reset(pulse_t *p);                   /* clears every voice and snaps smoothed controls to their targets */
void pulse_set_params(pulse_t *p, const pulse_params *params);

/* Events apply at the next sample rendered, so call them between render calls to place them
 * sample-accurately. note is 0..127. velocity is 1..127 (0 acts as note_off). */
void pulse_note_on(pulse_t *p, int note, int velocity);
void pulse_note_off(pulse_t *p, int note);
void pulse_pitch_bend(pulse_t *p, float value);  /* -1..1, scaled by bend_range */
void pulse_mod_wheel(pulse_t *p, float value);   /* 0..1, pushes SWEEP toward its maximum */
void pulse_sustain_pedal(pulse_t *p, int down);
void pulse_all_off(pulse_t *p);                  /* release every voice (MIDI all notes off) */
void pulse_all_sound_off(pulse_t *p);            /* fade every voice out in about 2.5 ms (MIDI all sound off) */

/* Write `frames` stereo samples (overwrites; any count from 1 up). The result does not depend on how the
 * stream is split into render calls. The two buffers must be distinct. */
void pulse_render(pulse_t *p, float *left, float *right, int frames);

/* Peak output level seen since the last call (0..1), then cleared. For meters and animation only. */
float pulse_take_activity(pulse_t *p);
int pulse_active_voices(const pulse_t *p);

#ifdef __cplusplus
}
#endif
#endif
