/* Offline test of the MPC engine glue, without the wrapper: build with
 *   clang -std=gnu11 -O1 -g -fsanitize=address,undefined -I../core -I$MPC_VST/wrapper test_engine.c engine.c ../core/*.c -lm
 * and run it as ./a.out samples   (the samples folder to scan). The wrapper-level test is tools/test_port.sh. */
#include "engine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int mpc_engine_changed(void *inst, char *key, int len);

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static double peak_of(const int16_t *x, int frames)
{
    double p = 0;
    for (int i = 0; i < frames * 2; i++)
        if (fabs((double)x[i]) > p)
            p = fabs((double)x[i]);
    return p / 32768.0;
}

static void get(const mpc_engine_t *e, void *h, const char *k, char *buf, int n)
{
    buf[0] = 0;
    e->get_param(h, k, buf, n);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : NULL;
    const mpc_engine_t *e = mpc_engine();
    void *h = e->create(dir);
    CHECK(h != NULL, "create");
    static int16_t out[44100 * 2];
    char buf[9000], name[64];

    /* names and slot 1 default */
    get(e, h, "slot_display", buf, sizeof buf);
    printf("  slot 1 shows: %s\n", buf);
    CHECK(strstr(buf, "1 ") == buf && strlen(buf) > 3, "slot display");
    get(e, h, "mode_display", buf, sizeof buf);
    CHECK(!strcmp(buf, "ONE-SHOT"), "default mode shows %s", buf);

    /* a hit on every slot makes sound */
    int silent = 0;
    for (int s = 0; s < 24; s++) {
        uint8_t on[3] = {0x90, (uint8_t)(36 + s), 110};
        e->midi(h, on, 3);
        e->render(h, out, 4410);
        if (peak_of(out, 4410) < 0.02)
            silent++;
    }
    CHECK(silent == 0, "%d of 24 slots were silent", silent);

    /* notes outside the 24 slots do nothing */
    uint8_t all[3] = {0xB0, 123, 0};
    e->midi(h, all, 3);
    e->render(h, out, 44100);
    uint8_t out_of_range[3] = {0x90, 60, 100};
    e->midi(h, out_of_range, 3);
    e->render(h, out, 2048);
    CHECK(peak_of(out, 2048) == 0.0, "note 60 is not a slot");

    /* slot change reports seven keys and the controls follow the slot */
    e->set_param(h, "slot", "3");
    e->set_param(h, "pitch", "0.75");   /* +12 st on slot 3 */
    char key[32];
    int reported = 0;
    while (mpc_engine_changed(h, key, sizeof key))
        reported++;
    CHECK(reported == 7, "slot change reported %d keys", reported);
    e->set_param(h, "slot", "4");
    get(e, h, "pitch", buf, sizeof buf);
    CHECK(fabs(atof(buf) - 0.5) < 1e-6, "slot 4 pitch is its own, got %s", buf);
    e->set_param(h, "slot", "3");
    get(e, h, "pitch", buf, sizeof buf);
    CHECK(fabs(atof(buf) - 0.75) < 1e-4, "slot 3 keeps +12 st, got %s", buf);
    get(e, h, "pitch_display", buf, sizeof buf);
    CHECK(!strcmp(buf, "+12.0 st"), "pitch display %s", buf);

    /* choose another sample for slot 3, by number */
    e->set_param(h, "sample", "0");
    get(e, h, "sample_display", name, sizeof name);
    printf("  slot 3 after choosing sample 0: %s\n", name);

    /* state round trip */
    e->set_param(h, "mode", "2");
    e->set_param(h, "end", "0.5");
    e->set_param(h, "reverse", "1");
    e->set_param(h, "gain", "0.25");
    get(e, h, "state", buf, sizeof buf);
    const size_t state_len = strlen(buf);
    printf("  state chunk: %zu bytes (the wrapper holds 8192)\n", state_len);
    CHECK(state_len > 100 && state_len < 4000, "state length %zu", state_len);
    void *h2 = e->create(dir);
    e->set_param(h2, "state", buf);
    char buf2[9000];
    get(e, h2, "state", buf2, sizeof buf2);
    CHECK(!strcmp(buf, buf2), "state round trip differs:\n%s\n%s", buf, buf2);
    get(e, h2, "mode_display", name, sizeof name);
    CHECK(!strcmp(name, "LOOP"), "restored mode %s", name);
    get(e, h2, "gain_display", name, sizeof name);
    CHECK(!strcmp(name, "-6.0 dB"), "restored gain %s", name);

    /* a sample name that is gone leaves the slot empty */
    char bad[9000];
    snprintf(bad, sizeof bad, "v1;1;nonexistent file|0.000|0.0000|1.0000|1|0|1.0000");
    e->set_param(h2, "state", bad);
    get(e, h2, "sample_display", name, sizeof name);
    CHECK(!strcmp(name, "none"), "missing sample shows %s", name);
    uint8_t on[3] = {0x90, 36, 110};
    e->midi(h2, on, 3);
    e->render(h2, out, 2048);
    CHECK(peak_of(out, 2048) == 0.0, "an empty slot is silent");
    e->set_param(h2, "state", "garbage");
    e->set_param(h2, "state", "v1;99;");
    e->set_param(h2, "slot", "-5");
    e->set_param(h2, "sample", "9999");
    e->set_param(h2, "mode", "77");
    e->set_param(h2, "pitch", "9");
    e->render(h2, out, 2048);

    /* timing: 8 looping voices through the engine, 128-frame blocks */
    for (int s = 0; s < 8; s++) {
        char k[8];
        snprintf(k, sizeof k, "%d", s + 1);
        e->set_param(h, "slot", k);
        e->set_param(h, "mode", "2");
        e->set_param(h, "end", "1");
        e->set_param(h, "reverse", "0");
        uint8_t n[3] = {0x90, (uint8_t)(36 + s), 127};
        e->midi(h, n, 3);
    }
    struct timespec t0, t1;
    const int blocks = 44100 * 10 / 128;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int b = 0; b < blocks; b++)
        e->render(h, out, 128);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double ms = ((double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-6) / blocks;
    printf("  8 looping voices: %.4f ms per 128-frame block (%.2f%% of 2.9 ms) on this host\n", ms, ms / 2.9 * 100.0);

    e->destroy(h);
    e->destroy(h2);
    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("ENGINE TESTS PASSED\n");
    return 0;
}
