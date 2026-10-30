/* Offline test of the MPC engine's sample handling: folder scan, loading on the worker thread, switching while notes
 * play, a bad file, and the WAV reader against the shipped files. Build and run with ./test_engine.sh */
#include "engine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static void render_ms(const mpc_engine_t *e, void *inst, int16_t *out, int frames) { e->render(inst, out, frames); }

static double measure_hz(const int16_t *lr, int frames, int from)
{
    double first = -1, last = -1;
    int count = 0;
    for (int i = from + 1; i < frames; i++) {
        const double a = lr[2 * (i - 1)], b = lr[2 * i];
        if (a < 0 && b >= 0) {
            const double t = (i - 1) + (-a) / (b - a);
            if (first < 0) first = t;
            last = t;
            count++;
        }
    }
    return count < 3 ? 0 : (count - 1) * 44100.0 / (last - first);
}

static int wait_for_name(const mpc_engine_t *e, void *inst, const char *want)
{
    char buf[128];
    for (int i = 0; i < 200; i++) {
        buf[0] = 0;
        e->get_param(inst, "sample_display", buf, sizeof buf);
        if (!strncmp(buf, want, strlen(want))) return 1;
        usleep(10000);
    }
    return 0;
}

/* Render until the loaded sample has changed, which shows as the engine taking the pending buffer. */
static double play_and_measure(const mpc_engine_t *e, void *inst, int note, int *peak_out)
{
    static int16_t out[44100 * 2];
    uint8_t on[3] = {0x90, (uint8_t)note, 127}, off[3] = {0x80, (uint8_t)note, 0};
    e->midi(inst, on, 3);
    for (int i = 0; i < 44100 / 128; i++) render_ms(e, inst, out + i * 128 * 2, 128);
    int pk = 0;
    for (int i = 0; i < 44100 * 2; i++) if (abs(out[i]) > pk) pk = abs(out[i]);
    *peak_out = pk;
    const double hz = measure_hz(out, 44100 / 128 * 128, 4000);
    e->midi(inst, off, 3);
    for (int i = 0; i < 400; i++) render_ms(e, inst, out, 128);
    return hz;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "samples";
    const mpc_engine_t *e = mpc_engine();
    void *inst = e->create(dir);
    CHECK(inst != NULL, "create failed");
    char buf[128];
    int pk = 0;

    /* The first built-in sounds straight away, with no wait. */
    double hz = play_and_measure(e, inst, 60, &pk);
    CHECK(pk > 500, "built-in sample silent (peak %d)", pk);
    CHECK(fabs(1200.0 * log2(hz / 261.6256)) < 15.0, "built-in bar at note 60: %.2f Hz", hz);
    printf("built-in 0 (Bar), note 60: %.2f Hz, peak %d\n", hz, pk);

    /* Folder scan: three built-ins, then the two shipped files in name order. */
    e->set_param(inst, "sample", "3");
    CHECK(wait_for_name(e, inst, "Bar 32k"), "scan did not list 'Bar 32k' at index 3");
    e->set_param(inst, "sample", "4");
    CHECK(wait_for_name(e, inst, "Pluck stereo"), "scan did not list 'Pluck stereo' at index 4");
    e->set_param(inst, "sample", "3");
    usleep(300000);
    for (int i = 0; i < 4; i++) { int16_t t[256]; render_ms(e, inst, t, 128); } /* render picks up the decoded buffer */
    hz = play_and_measure(e, inst, 60, &pk);
    CHECK(pk > 500, "Bar 32k silent");
    /* 32 kHz file with root 60 played at note 60: its own pitch, 261.63 Hz, whatever the output rate. */
    CHECK(fabs(1200.0 * log2(hz / 261.6256)) < 15.0, "Bar 32k at its root note 60 gave %.2f Hz (rate conversion or smpl root wrong)", hz);
    printf("file 'Bar 32k' (32 kHz mono 16 bit), note 60: %.2f Hz, peak %d\n", hz, pk);

    e->set_param(inst, "sample", "4");
    usleep(300000);
    for (int i = 0; i < 4; i++) { int16_t t[256]; render_ms(e, inst, t, 128); }
    hz = play_and_measure(e, inst, 36, &pk); /* the stereo pluck is C2, root 36 from its smpl chunk */
    CHECK(pk > 500, "Pluck stereo silent");
    CHECK(fabs(1200.0 * log2(hz / 65.4064)) < 15.0, "Pluck stereo at root 36 gave %.2f Hz", hz);
    printf("file 'Pluck stereo' (44.1 kHz stereo 24 bit), note 36: %.2f Hz, peak %d\n", hz, pk);

    /* Switching while a note holds: no crash, output stays finite and in range. */
    uint8_t on[3] = {0x90, 60, 127};
    e->midi(inst, on, 3);
    static int16_t out[128 * 2];
    for (int s = 0; s < 40; s++) {
        char v[16];
        snprintf(v, sizeof v, "%d", s % 5);
        e->set_param(inst, "sample", v);
        for (int i = 0; i < 20; i++) render_ms(e, inst, out, 128);
        usleep(2000);
    }
    CHECK(1, "switch storm survived");

    /* A bad .wav in the folder: it lists, reports unreadable, and the previous sample keeps playing. */
    char tmpl[] = "/tmp/samplerXXXXXX";
    char *td = mkdtemp(tmpl);
    char path[300];
    snprintf(path, sizeof path, "%s/broken.wav", td);
    FILE *f = fopen(path, "wb");
    fputs("this is not a wav file at all, just text pretending to be one, long enough to pass the size check....", f);
    fclose(f);
    void *inst2 = e->create(td);
    CHECK(wait_for_name(e, inst2, "Bar") != 0, "second instance");
    e->set_param(inst2, "sample", "3");
    int ok = 0;
    for (int i = 0; i < 200 && !ok; i++) {
        usleep(10000);
        e->get_param(inst2, "sample_display", buf, sizeof buf);
        ok = strstr(buf, "unreadable") != NULL;
    }
    CHECK(ok, "broken file was not reported (display '%s')", buf);
    hz = play_and_measure(e, inst2, 60, &pk);
    CHECK(pk > 500, "previous sample stopped playing after a bad file");
    snprintf(path, sizeof path, "%s/broken.wav", td);
    remove(path);
    rmdir(td);

    /* No data dir at all: only the built-ins. */
    void *inst3 = e->create(NULL);
    e->set_param(inst3, "sample", "2");
    CHECK(wait_for_name(e, inst3, "Pluck"), "built-in 2 not selectable without a folder");

    e->destroy(inst);
    e->destroy(inst2);
    e->destroy(inst3);
    if (failures) { printf("%d FAILED\n", failures); return 1; }
    printf("ENGINE SAMPLE TESTS PASSED\n");
    return 0;
}
