/* Render time for 12 voices at 44.1 kHz in 128-frame blocks, as a fraction of the 2.9 ms block budget. */
#include "../dbox_core.h"
#include <stdio.h>
#include <time.h>

int main(void)
{
    static dbox_t d;
    float l[128], r[128];
    dbox_init(&d, 44100.0f);
    dbox_params p;
    dbox_default_params(&p);
    p.decay = 0.8f; p.mode = 5; p.wave = 1.0f; p.wave2 = 0.5f; p.release = 10.0f;
    dbox_set_params(&d, &p);
    for (int k = 0; k < 12; k++) dbox_note_on(&d, 36 + k, 127);
    const int blocks = 20000;
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int i = 0; i < blocks; i++) {
        if (i % 300 == 0) for (int k = 0; k < 12; k++) dbox_note_on(&d, 36 + k, 127);
        dbox_render(&d, l, r, 128);
    }
    clock_gettime(CLOCK_MONOTONIC, &b);
    const double ms = ((b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6) / blocks;
    printf("12 voices, 128 frames: %.4f ms per block, %.2f%% of the 2.9 ms budget (voices now %d)\n", ms, ms / 2.9 * 100.0,
           dbox_active_voices(&d));
    return 0;
}
