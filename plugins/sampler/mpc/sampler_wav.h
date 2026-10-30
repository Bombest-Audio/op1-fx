/* Small WAV reader for the MPC port. Allocates; never call it from the audio thread. */
#ifndef SAMPLER_WAV_H
#define SAMPLER_WAV_H

#ifdef __cplusplus
extern "C" {
#endif

/* Reads PCM 8/16/24/32 bit or 32 bit float WAV, any channel count (mixed down to mono), up to max_seconds of audio.
 * On success returns a malloc'd mono buffer (free it), and sets *frames, *rate and *root (the MIDI unity note from a
 * smpl chunk, or 60 when the file has none). Returns NULL for anything else. */
float *sampler_wav_read(const char *path, float max_seconds, int *frames, float *rate, int *root);

/* Writes mono or interleaved stereo audio as 16 or 24 bit PCM with a smpl chunk carrying the root note. Returns 0 on success. */
int sampler_wav_write(const char *path, const float *interleaved, int frames, int channels, int rate, int bits, int root);

#ifdef __cplusplus
}
#endif
#endif
