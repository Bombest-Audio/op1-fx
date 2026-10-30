/* DrumSampler default kit: 24 short drum sounds made in code, so the plugin has something to play before a user loads
 * a file and so the MPC port can ship its sample folder without any third-party audio. Every sound is synthesized
 * from sine sweeps, filtered noise and inharmonic square oscillators. Output is deterministic: the same index always
 * gives the same samples. Portable C99, libm only.
 */
#ifndef DRUMSAMPLER_KIT_H
#define DRUMSAMPLER_KIT_H

#ifdef __cplusplus
extern "C" {
#endif

#define DS_KIT_SOUNDS 24
#define DS_KIT_RATE 44100
#define DS_KIT_MAX_FRAMES 44100   /* no sound is longer than one second */

const char *ds_kit_name(int index);                      /* "Kick", "Snare", ... or "" for a bad index */
int ds_kit_frames(int index);                            /* length in frames at DS_KIT_RATE, 0 for a bad index */
/* Writes ds_kit_frames(index) mono samples at DS_KIT_RATE into out (peak 0.9, ends at zero). Returns the frame count,
 * or 0 if the index is bad or cap is too small. */
int ds_kit_render(int index, float *out, int cap);

#ifdef __cplusplus
}
#endif
#endif
