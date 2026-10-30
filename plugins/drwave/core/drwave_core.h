/* DrWave DSP core. Portable C99, no allocation after init, no dependencies beyond libm.
 *
 * A new take on the OP-1 "dr wave" synth engine, designed from scratch: a band-limited wavetable oscillator with
 * a bank of eight single-cycle waves, four screen knobs (WAVE, BLEND, MOTION, FOLD), a tone control (the fifth
 * engine value of the OP-1 factory preset), eight voices, an amp envelope and an output level.
 * The firmware decompile has no recoverable DSP for this engine, so the sound design is mine. docs/DSP.md says
 * what came from the firmware and what did not.
 *
 * One instance is one stereo synth. Every function is real-time safe. Nothing here is thread safe: call it from
 * one thread (the audio thread), including note_on, note_off and set_params.
 */
#ifndef DRWAVE_CORE_H
#define DRWAVE_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define DW_VOICES 8
#define DW_WAVES 8                 /* single-cycle waves in the bank */
#define DW_TABLE_BITS 11
#define DW_TABLE_SIZE (1 << DW_TABLE_BITS)
#define DW_MIPS 9                  /* band-limited copies of each wave, one per octave of pitch */

/* Control values. The first five are normalized 0..1. Times are in seconds, sustain is 0..1. */
typedef struct {
    float wave;       /* WAVE: position along the wave bank, see dw_wave_position() */
    float blend;      /* BLEND: crossfade from the wave to its mirror image in the bank */
    float motion;     /* MOTION: how far each note sweeps the wave position while it starts */
    float fold;       /* FOLD: sine wavefolder amount, 0 is bypassed */
    float tone;       /* TONE: low-pass, 1 is open, see dw_tone_hz() */
    float attack;     /* s */
    float decay;      /* s, time for the level to fall 60 dB of the way to the sustain level */
    float sustain;    /* 0..1 */
    float release;    /* s, time to fall 60 dB */
    float level;      /* 0..1, output level, squared */
    float bend_range; /* semitones for a full pitch bend */
} dw_params;

/* OP-1 factory default: knobs 24000, 16000, 8200, 24000 and extra 32000, all of 32767. */
#define DW_DEFAULT_WAVE 0.7324f
#define DW_DEFAULT_BLEND 0.4883f
#define DW_DEFAULT_MOTION 0.2502f
#define DW_DEFAULT_FOLD 0.7324f
#define DW_DEFAULT_TONE 0.9766f
/* Mine, not from the firmware. */
#define DW_DEFAULT_ATTACK 0.003f
#define DW_DEFAULT_DECAY 1.2f
#define DW_DEFAULT_SUSTAIN 0.6f
#define DW_DEFAULT_RELEASE 0.25f
#define DW_DEFAULT_LEVEL 0.8f
#define DW_DEFAULT_BEND_RANGE 2.0f

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
    float sweep;      /* motion sweep state, 1 at note start, relaxes to 0 */
    float pos_s;      /* slewed wave position, 0..DW_WAVES-1 */
    float pos_cur, pos_d;
    float mip_cur, mip_d;      /* band-limit level, fractional */
    float fold_cur, fold_d;    /* fold gain for this pitch */
    float mix_cur, mix_d;      /* fold mix for this pitch */
    float ph[2];      /* oscillator A and B phase, in cycles */
    float inc[2];     /* phase increment in cycles per sample */
    float lp;         /* tone filter state */
    float pan_l, pan_r;
} dw_voice;

typedef struct {
    float sr;
    dw_params target;

    /* smoothed controls (5 ms one-pole) and the per-sample ramps inside the current control tick */
    float s_wave, s_blend, s_motion, s_fold, s_tone, s_level, s_bend, s_mod;
    float wa_cur, wa_d, wb_cur, wb_d;   /* oscillator A and B weights (equal power from BLEND) */
    float tone_cur, tone_d;             /* tone filter coefficient, 1 is open */
    float level_cur, level_d;
    float c_smooth, c_slew;     /* one-pole coefficients per control tick */
    int tick_left;              /* samples left in the current control tick; 0 forces a new tick */
    float sweep_dec;            /* motion sweep decay per tick */

    /* envelope constants */
    float att_inc, dec_c, rel_c;

    float bend_norm;            /* -1..1 as received */
    float mod_wheel;            /* 0..1 as received */
    int pedal;
    unsigned serial;
    float act_peak;

    dw_voice voice[DW_VOICES];
    /* wave bank: [wave][mip][phase], one guard sample at the end so linear interpolation needs no wrap */
    float table[DW_WAVES][DW_MIPS][DW_TABLE_SIZE + 1];
    float sine[DW_TABLE_SIZE + 1];   /* plain sine, used by the wavefolder */
} dw_t;

typedef struct {
    float peak;        /* largest output sample since the last call, linear */
    int voices;        /* voices sounding now */
    unsigned notes;    /* note-on counter; changes when a new note starts */
} dw_activity;

/* Knob mappings. Exposed so the plugin and the MPC port can show the same numbers. */
float dw_wave_position(float norm);       /* 0 to 7, the position in the bank */
const char *dw_wave_name(int index);      /* name of wave 0..7 */
float dw_tone_hz(float norm);             /* 80 Hz to 20 kHz, exponential; 1.0 bypasses the filter */
float dw_level_gain(float norm);          /* linear gain, norm squared */

void dw_init(dw_t *d, float sample_rate);
void dw_reset(dw_t *d);                   /* silence everything, clear all state, keep the parameters */
void dw_set_params(dw_t *d, const dw_params *p);
void dw_default_params(dw_params *p);

void dw_note_on(dw_t *d, int note, int velocity);   /* velocity 1..127; 0 is a note off */
void dw_note_off(dw_t *d, int note);
void dw_pitch_bend(dw_t *d, float norm);            /* -1..1 */
void dw_mod_wheel(dw_t *d, float norm);             /* 0..1, pushes the wave position up */
void dw_sustain_pedal(dw_t *d, int down);
void dw_all_off(dw_t *d);                           /* release every note and lift the pedal */

/* Render and overwrite l and r. Any frame count from 1 up works, and the output does not depend on how a stretch
 * of time is split into calls. Needs two separate buffers. */
void dw_render(dw_t *d, float *l, float *r, int frames);

int dw_active_voices(const dw_t *d);
void dw_take_activity(dw_t *d, dw_activity *out);

#ifdef __cplusplus
}
#endif
#endif
