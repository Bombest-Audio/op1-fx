#include "sampler_wav.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILE_BYTES (256u * 1024u * 1024u)

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

float *sampler_wav_read(const char *path, float max_seconds, int *frames, float *rate, int *root)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 44 || (unsigned long)size > MAX_FILE_BYTES) {
        fclose(f);
        return NULL;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);

    float *out = NULL;
    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) goto done;

    int fmt_tag = 0, channels = 0, bits = 0;
    uint32_t sr = 0;
    const uint8_t *data = NULL;
    uint32_t data_len = 0;
    int unity = 60;
    size_t pos = 12;
    while (pos + 8 <= (size_t)size) {
        const uint32_t len = rd32(buf + pos + 4);
        const uint8_t *body = buf + pos + 8;
        const size_t avail = (size_t)size - (pos + 8);
        const uint32_t clen = len > avail ? (uint32_t)avail : len;
        if (!memcmp(buf + pos, "fmt ", 4) && clen >= 16) {
            fmt_tag = rd16(body);
            channels = rd16(body + 2);
            sr = rd32(body + 4);
            bits = rd16(body + 14);
            if (fmt_tag == 0xFFFE && clen >= 26) fmt_tag = rd16(body + 24); /* extensible: the first two bytes of the sub-format */
        } else if (!memcmp(buf + pos, "data", 4)) {
            data = body;
            data_len = clen;
        } else if (!memcmp(buf + pos, "smpl", 4) && clen >= 16) {
            const uint32_t u = rd32(body + 12);
            if (u <= 127) unity = (int)u;
        }
        pos += 8 + (size_t)len + (len & 1u);
    }
    if (!data || channels < 1 || channels > 16 || sr < 1000 || sr > 768000) goto done;
    if (!((fmt_tag == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (fmt_tag == 3 && bits == 32))) goto done;

    const int bytes = bits / 8;
    long n = (long)(data_len / (uint32_t)(bytes * channels));
    const long cap = (long)(max_seconds * (float)sr);
    if (n > cap) n = cap;
    if (n < 2) goto done;
    out = (float *)malloc(sizeof(float) * (size_t)n);
    if (!out) goto done;
    for (long i = 0; i < n; i++) {
        float sum = 0.0f;
        for (int c = 0; c < channels; c++) {
            const uint8_t *p = data + ((size_t)i * (size_t)channels + (size_t)c) * (size_t)bytes;
            float v;
            if (fmt_tag == 3) {
                uint32_t u = rd32(p);
                float fv;
                memcpy(&fv, &u, 4);
                v = isfinite(fv) ? fv : 0.0f;
            } else if (bits == 8) {
                v = ((float)p[0] - 128.0f) / 128.0f;
            } else if (bits == 16) {
                v = (float)(int16_t)rd16(p) / 32768.0f;
            } else if (bits == 24) {
                int32_t s = (int32_t)(((uint32_t)p[0] << 8) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 24)) >> 8;
                v = (float)s / 8388608.0f;
            } else {
                v = (float)((double)(int32_t)rd32(p) / 2147483648.0);
            }
            sum += v;
        }
        out[i] = sum / (float)channels;
    }
    *frames = (int)n;
    *rate = (float)sr;
    *root = unity;
done:
    free(buf);
    return out;
}

static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

int sampler_wav_write(const char *path, const float *x, int frames, int channels, int rate, int bits, int root)
{
    if ((bits != 16 && bits != 24) || channels < 1 || channels > 2) return 1;
    const int bytes = bits / 8;
    const uint32_t data_len = (uint32_t)(frames * channels * bytes);
    const uint32_t smpl_len = 36;
    uint8_t head[12 + 8 + 16 + 8 + 36 + 8];
    uint8_t *h = head;
    memcpy(h, "RIFF", 4);
    wr32(h + 4, 4 + 8 + 16 + 8 + smpl_len + 8 + data_len);
    memcpy(h + 8, "WAVE", 4);
    h += 12;
    memcpy(h, "fmt ", 4); wr32(h + 4, 16);
    wr16(h + 8, 1); wr16(h + 10, (uint16_t)channels); wr32(h + 12, (uint32_t)rate);
    wr32(h + 16, (uint32_t)(rate * channels * bytes)); wr16(h + 20, (uint16_t)(channels * bytes)); wr16(h + 22, (uint16_t)bits);
    h += 24;
    memcpy(h, "smpl", 4); wr32(h + 4, smpl_len);
    memset(h + 8, 0, smpl_len);
    wr32(h + 8 + 8, (uint32_t)(1000000000.0 / rate)); /* sample period in ns */
    wr32(h + 8 + 12, (uint32_t)root);
    h += 8 + smpl_len;
    memcpy(h, "data", 4); wr32(h + 4, data_len);
    h += 8;

    FILE *f = fopen(path, "wb");
    if (!f) return 2;
    int ok = fwrite(head, 1, (size_t)(h - head), f) == (size_t)(h - head);
    for (int i = 0; i < frames * channels && ok; i++) {
        float v = x[i] < -1.0f ? -1.0f : (x[i] > 1.0f ? 1.0f : x[i]);
        uint8_t b[3];
        if (bits == 16) {
            wr16(b, (uint16_t)(int16_t)lrintf(v * 32767.0f));
        } else {
            const int32_t s = (int32_t)lrintf(v * 8388607.0f);
            b[0] = (uint8_t)s; b[1] = (uint8_t)(s >> 8); b[2] = (uint8_t)(s >> 16);
        }
        ok = fwrite(b, 1, (size_t)bytes, f) == (size_t)bytes;
    }
    fclose(f);
    return ok ? 0 : 3;
}
