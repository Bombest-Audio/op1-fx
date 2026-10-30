/* Makes the sample files that ship in mpc/samples/ from the core's own generators. Not part of the plugin build.
 *   cc -std=c99 -I../core gen_samples.c sampler_wav.c ../core/sampler_core.c -lm -o gen_samples && ./gen_samples samples
 * Two files, picked so the loader sees a different format and rate from each: a 32 kHz mono 16-bit bar, and a stereo
 * 24-bit pluck at 44.1 kHz with a small level difference between the channels. Both carry a root note in a smpl chunk. */
#include "sampler_core.h"
#include "sampler_wav.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "samples";
    char path[600];

    int n = sampler_demo_frames(0, 32000.0f);
    float *a = (float *)calloc((size_t)n, sizeof(float));
    sampler_demo_render(0, 32000.0f, a, n);
    snprintf(path, sizeof path, "%s/Bar 32k.wav", dir);
    if (sampler_wav_write(path, a, n, 1, 32000, 16, sampler_demo_root(0))) return 1;
    free(a);

    n = sampler_demo_frames(2, 44100.0f);
    float *m = (float *)calloc((size_t)n, sizeof(float));
    float *st = (float *)calloc((size_t)n * 2, sizeof(float));
    sampler_demo_render(2, 44100.0f, m, n);
    for (int i = 0; i < n; i++) {
        st[2 * i] = m[i];
        st[2 * i + 1] = m[i] * 0.8f;
    }
    snprintf(path, sizeof path, "%s/Pluck stereo.wav", dir);
    if (sampler_wav_write(path, st, n, 2, 44100, 24, sampler_demo_root(2))) return 1;
    free(m);
    free(st);
    return 0;
}
