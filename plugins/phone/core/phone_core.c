#include "phone_core.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f

/* A crossing is a rise through +CROSS_T after the signal has been below -CROSS_T. About -60 dBFS. */
#define CROSS_T 0.001f
/* The level follower reaches full scale for a mono level of 1/GATE_GAIN, about -48 dBFS. */
#define GATE_GAIN 256.0f
/* Wet soft ceiling: linear up to CEIL_T, then a tanh knee that reaches 1.0 with matching slope. */
#define CEIL_T 0.8f

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float flush(float v)
{
    return fabsf(v) < 1e-20f ? 0.0f : v;
}

static inline float soft_ceiling(float x)
{
    float a = fabsf(x);
    if (a <= CEIL_T)
        return x;
    float k = 1.0f - CEIL_T;
    float z = (a - CEIL_T) / k;
    /* Pade tanh, input clamped to its accurate range */
    z = z > 3.0f ? 3.0f : z;
    float z2 = z * z;
    float t = z * (27.0f + z2) / (27.0f + 9.0f * z2);
    float y = CEIL_T + k * t;
    return x < 0.0f ? -y : y;
}

float phone_tone_ratio(float norm)
{
    float v = clampf(norm, 0.0f, 1.0f);
    return clampf(16.0f * v * v, 1.0f / 16.0f, 16.0f);
}

float phone_tone_semitones(float norm)
{
    return 12.0f * log2f(phone_tone_ratio(norm));
}

/* Windowed-sinc interpolation table. Row i is the 8 tap weights for an output point i/PHONE_PHASES of the way from tap 3
 * to tap 4. Each row is normalized to sum to 1 so a constant signal stays constant. */
static void build_table(phone_t *p)
{
    for (int i = 0; i <= PHONE_PHASES; i++) {
        const float phi = (float)i / (float)PHONE_PHASES;
        float sum = 0.0f;
        for (int j = 0; j < PHONE_TAPS; j++) {
            const float d = (float)(j - 3) - phi;
            const float s = fabsf(d) < 1e-6f ? 1.0f : sinf(PI_F * d) / (PI_F * d);
            const float w = 0.42f + 0.5f * cosf(PI_F * d / 4.0f) + 0.08f * cosf(PI_F * d / 2.0f);
            p->table[i][j] = s * w;
            sum += s * w;
        }
        for (int j = 0; j < PHONE_TAPS; j++)
            p->table[i][j] /= sum;
    }
}

void phone_reset(phone_t *p)
{
    memset(p->buf, 0, sizeof p->buf);
    memset(p->ring, 0, sizeof p->ring);
    memset(p->dc_lp, 0, sizeof p->dc_lp);
    memset(p->lp1, 0, sizeof p->lp1);
    memset(p->lp2, 0, sizeof p->lp2);
    p->s_log2_ratio = p->t_log2_ratio;
    p->s_mix = p->target.telematic;
    p->ratio_valid = 0;
    p->env = 0.0f;
    p->armed = 0;
    p->synced = 0;
    p->wbuf = 2;
    p->spare = 1;
    p->rbuf = 0;
    p->ready = 0;
    p->wpos = 0;
    p->cycles = 0;
    p->rpos = 0;
    p->reps = 0;
    for (int b = 0; b < 3; b++)
        p->len[b] = p->min_len;
    p->phase = 0.0f;
    p->head = 0;
    p->act_peak = 0.0f;
    p->swaps = 0;
}

void phone_init(phone_t *p, float sample_rate)
{
    memset(p, 0, sizeof *p);
    p->sr = sample_rate > 8000.0f ? (sample_rate > PHONE_MAX_SR ? PHONE_MAX_SR : sample_rate) : 44100.0f;
    p->target.tone = PHONE_DEFAULT_TONE;
    p->target.phonic = PHONE_DEFAULT_PHONIC;
    p->target.baud = PHONE_DEFAULT_BAUD;
    p->target.telematic = PHONE_DEFAULT_TELEMATIC;

    p->smooth_coef = 1.0f - expf(-1.0f / (0.006f * p->sr));
    p->dc_a = 1.0f - expf(-2.0f * PI_F * 20.0f / p->sr);
    p->env_a = 1.0f - expf(-1.0f / (0.0099f * p->sr));
    p->cyc_len = p->sr / 60.0f;
    /* The shortest fragment is about 2.9 ms (128 samples at 44.1 kHz). */
    p->min_len = (int)(128.0f * p->sr / 44100.0f + 0.5f);
    if (p->min_len < 16)
        p->min_len = 16;
    build_table(p);
    phone_set_params(p, &p->target);
    phone_reset(p);
}

void phone_set_params(phone_t *p, const phone_params *params)
{
    p->target.tone = clampf(params->tone, 0.0f, 1.0f);
    p->target.telematic = clampf(params->telematic, 0.0f, 1.0f);
    int c = params->phonic;
    p->target.phonic = c < 1 ? 1 : (c > PHONE_MAX_CYCLES ? PHONE_MAX_CYCLES : c);
    int b = params->baud;
    p->target.baud = b < 1 ? 1 : (b > PHONE_MAX_REPEATS ? PHONE_MAX_REPEATS : b);
    p->t_log2_ratio = log2f(phone_tone_ratio(p->target.tone));

    int m = (int)((float)p->target.phonic * p->cyc_len + 0.5f);
    if (m > PHONE_BUF_FRAMES)
        m = PHONE_BUF_FRAMES;
    if (m <= p->min_len)
        m = p->min_len + 1;
    p->max_len = m;
}

float phone_take_activity(phone_t *p)
{
    const float a = p->act_peak;
    p->act_peak = 0.0f;
    return a;
}

int phone_take_swaps(phone_t *p)
{
    const int n = p->swaps;
    p->swaps = 0;
    return n;
}

/* Read one source sample from the looping fragment, low-pass it for the current rate, and push it into the ring. */
static void pull(phone_t *p, float aa)
{
    const int b = p->rbuf;
    const float s[2] = {p->buf[b][0][p->rpos], p->buf[b][1][p->rpos]};
    p->rpos++;
    if (p->rpos >= p->len[b]) {
        p->rpos = 0;
        p->reps++;
        if (p->ready && p->reps >= p->target.baud) {
            p->rbuf = p->spare;
            p->spare = b;
            p->ready = 0;
            p->reps = 0;
            p->swaps++;
        }
    }
    for (int c = 0; c < 2; c++) {
        p->lp1[c] += aa * (s[c] - p->lp1[c]);
        p->lp2[c] += aa * (p->lp1[c] - p->lp2[c]);
        p->lp1[c] = flush(p->lp1[c]);
        p->lp2[c] = flush(p->lp2[c]);
        p->ring[c][p->head] = p->lp2[c];
    }
    p->head = (p->head + 1) & (PHONE_TAPS - 1);
}

void phone_process(phone_t *p, float *left, float *right, int frames)
{
    const float sm = p->smooth_coef;

    for (int i = 0; i < frames; i++) {
        /* control smoothing, snapped to the target once it is close so a fully dry setting is reached exactly */
        float d = p->t_log2_ratio - p->s_log2_ratio;
        if (fabsf(d) < 1e-5f) {
            if (p->s_log2_ratio != p->t_log2_ratio) {
                p->s_log2_ratio = p->t_log2_ratio;
                p->ratio_valid = 0;
            }
        } else {
            p->s_log2_ratio += sm * d;
            p->ratio_valid = 0;
        }
        d = p->target.telematic - p->s_mix;
        p->s_mix = fabsf(d) < 1e-5f ? p->target.telematic : p->s_mix + sm * d;
        if (!p->ratio_valid) {
            p->ratio = exp2f(p->s_log2_ratio);
            /* Above unity rate the source is low-passed at 0.45 / rate cycles per source sample. One-pole
             * x / (1 + x) form of the coefficient, no exp needed. */
            const float x = 2.0f * PI_F * 0.45f / (p->ratio > 1.0f ? p->ratio : 1.0f);
            p->aa = x / (1.0f + x);
            p->ratio_valid = 1;
        }

        const float xl = left[i];
        const float xr = right[i];

        /* High-pass at 20 Hz so DC does not stop the crossing detector, and so loops carry no offset. */
        float hl = xl, hr = xr;
        if (!(hl > -1e4f && hl < 1e4f))
            hl = 0.0f;
        if (!(hr > -1e4f && hr < 1e4f))
            hr = 0.0f;
        p->dc_lp[0] += p->dc_a * (hl - p->dc_lp[0]);
        p->dc_lp[1] += p->dc_a * (hr - p->dc_lp[1]);
        p->dc_lp[0] = flush(p->dc_lp[0]);
        p->dc_lp[1] = flush(p->dc_lp[1]);
        hl -= p->dc_lp[0];
        hr -= p->dc_lp[1];
        const float m = 0.5f * (hl + hr);

        /* level follower, about 10 ms; it gates the loop so the last fragment does not ring on after the input stops */
        float tgt = GATE_GAIN * fabsf(m);
        tgt = tgt > 1.0f ? 1.0f : tgt;
        p->env = flush(p->env + p->env_a * (tgt - p->env));
        if (p->env > p->act_peak)
            p->act_peak = p->env;

        /* fragment capture */
        int crossing = 0;
        if (m < -CROSS_T) {
            p->armed = 1;
        } else if (m > CROSS_T && p->armed) {
            p->armed = 0;
            crossing = 1;
        }
        if (!p->synced && crossing) {
            p->synced = 1;
            p->wpos = 0;
            p->cycles = 0;
        }
        if (p->synced) {
            if (crossing && p->wpos >= p->min_len) {
                p->cycles++;
                if (p->cycles >= p->target.phonic) {
                    p->len[p->wbuf] = p->wpos;
                    const int t = p->wbuf;
                    p->wbuf = p->spare;
                    p->spare = t;
                    p->ready = 1;
                    p->wpos = 0;
                    p->cycles = 0;
                }
            }
            if (p->wpos >= p->max_len) {
                /* no crossing found in time: drop the partial fragment and wait for the next crossing */
                p->synced = 0;
                p->wpos = 0;
                p->cycles = 0;
            } else {
                p->buf[p->wbuf][0][p->wpos] = hl;
                p->buf[p->wbuf][1][p->wpos] = hr;
                p->wpos++;
            }
        }

        /* playback at the smoothed rate */
        p->phase += p->ratio;
        while (p->phase >= 1.0f) {
            p->phase -= 1.0f;
            pull(p, p->aa);
        }

        if (p->target.telematic <= 0.0f && p->s_mix <= 0.0f)
            continue; /* fully dry, leave the buffers untouched */

        const float pos = p->phase * (float)PHONE_PHASES;
        int idx = (int)pos;
        if (idx > PHONE_PHASES - 1)
            idx = PHONE_PHASES - 1;
        const float fr = pos - (float)idx;
        float wet[2];
        for (int c = 0; c < 2; c++) {
            float acc = 0.0f;
            for (int j = 0; j < PHONE_TAPS; j++) {
                const float w = p->table[idx][j] + fr * (p->table[idx + 1][j] - p->table[idx][j]);
                acc += p->ring[c][(p->head + j) & (PHONE_TAPS - 1)] * w;
            }
            wet[c] = flush(soft_ceiling(acc * p->env));
        }

        const float mix = p->s_mix;
        left[i] = xl * (1.0f - mix) + wet[0] * mix;
        right[i] = xr * (1.0f - mix) + wet[1] * mix;
    }
}
