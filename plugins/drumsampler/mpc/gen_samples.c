/* Writes the generated kit as 16 bit mono WAV files, the sample folder the MPC port ships.
 * Build and run: clang -std=c99 -O2 -I../core gen_samples.c ../core/drumsampler_kit.c -lm -o /tmp/gen_samples && /tmp/gen_samples samples
 * The files are named "01 Kick.wav" and so on, so that sorting by name keeps the slot order. */
#include "drumsampler_kit.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void w32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f); }
static void w16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "samples";
    static float buf[DS_KIT_MAX_FRAMES];
    for (int i = 0; i < DS_KIT_SOUNDS; i++) {
        const int n = ds_kit_render(i, buf, DS_KIT_MAX_FRAMES);
        char path[512];
        snprintf(path, sizeof path, "%s/%02d %s.wav", dir, i + 1, ds_kit_name(i));
        FILE *f = fopen(path, "wb");
        if (!f) {
            fprintf(stderr, "cannot write %s\n", path);
            return 1;
        }
        fwrite("RIFF", 1, 4, f);
        w32(f, 36 + (uint32_t)n * 2);
        fwrite("WAVEfmt ", 1, 8, f);
        w32(f, 16);
        w16(f, 1);
        w16(f, 1);
        w32(f, DS_KIT_RATE);
        w32(f, DS_KIT_RATE * 2);
        w16(f, 2);
        w16(f, 16);
        fwrite("data", 1, 4, f);
        w32(f, (uint32_t)n * 2);
        for (int k = 0; k < n; k++) {
            float v = buf[k] * 32767.0f;
            w16(f, (uint16_t)(int16_t)lrintf(v));
        }
        fclose(f);
    }
    return 0;
}
