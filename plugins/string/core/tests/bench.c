/* Host timing of opst_render: 8 voices held, 128-frame blocks at 44.1 kHz. Prints the worst and mean block time
 * as a fraction of the 2.9 ms block budget. This is a host number, not a device number. */
#include "../string_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int cmp(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

int main(void)
{
    opst_t *s = (opst_t *)calloc(1, sizeof *s);
    float l[128], r[128];
    opst_params p = {0.046f, 0.9f, 0.5f, 0.9f, 0.001f, 1.0f, 1.0f, 0.4f, 1.0f};
    opst_init(s, 44100.0f);
    opst_set_params(s, &p);
    static double times[20000];
    double worst = 0, sum = 0;
    const int blocks = 20000; /* notes retriggered every 200 blocks so all 8 voices sound throughout */
    int active_min = 8;
    for (int b = 0; b < blocks; b++) {
        if (b % 200 == 0)
            for (int i = 0; i < 8; i++)
                opst_note_on(s, 48 + i * 5, 110);
        const double t0 = now();
        opst_render(s, l, r, 128);
        const double dt = now() - t0;
        if (opst_active_voices(s) < active_min)
            active_min = opst_active_voices(s);
        times[b] = dt;
        if (b > 50) {
            sum += dt;
            if (dt > worst)
                worst = dt;
        }
    }
    qsort(times + 51, (size_t)(blocks - 51), sizeof(double), cmp);
    const double p99 = times[51 + (blocks - 51) * 99 / 100];
    const double budget = 128.0 / 44100.0;
    printf("fewest voices sounding %d, mean %.1f us (%.2f%% of %.2f ms), p99 %.1f us (%.2f%%), worst %.1f us (%.2f%%)\n", active_min,
           sum / (blocks - 51) * 1e6, sum / (blocks - 51) / budget * 100.0, budget * 1e3, p99 * 1e6, p99 / budget * 100.0, worst * 1e6,
           worst / budget * 100.0);
    free(s);
    return 0;
}
