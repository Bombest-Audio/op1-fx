#include "delay_core.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f

/* Longest and shortest loop at full tape speed (TIME 1 and 0). */
#define BASE_MAX_S 1.0f
#define BASE_MIN_S (1.0f / 32.0f)
/* SPEED never goes below this, whatever the knob says. */
#define RATE_MIN (1.0f / 32.0f)
/* Shortest delay the interpolator can read, in samples. */
#define DELAY_MIN_SAMPLES 4.0f
/* Control-rate period: tape speed, delay target and filter coefficients are recomputed every KR samples. */
#define KR 8
/* The loop low-pass has its poles at RATE * POLE_HZ_AT_FULL_SPEED. Two poles together are 3 dB down at 0.64 of that,
 * which puts the corner at about 0.4 of the Nyquist rate of a tape clocked at RATE * 44.1 kHz. */
#define POLE_HZ_AT_FULL_SPEED 27562.5f
/* When the delay time changes the read point glides, which pitch-shifts what is in flight like a tape speed change.
 * Limit that to one octave either way: the delay may grow by at most 0.5 samples per sample (reads at half speed)
 * and shrink by at most 1 sample per sample (reads at double speed). */
#define D_GROW_MAX 0.5f
#define D_SHRINK_MAX 1.0f
/* Soft ceiling for what is stored on the tape. Linear up to SAT_T, then a tanh knee that reaches 1.0 with matching slope. */
#define SAT_T 0.75f

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float soft_ceiling(float x)
{
    float a = fabsf(x);
    if (a <= SAT_T)
        return x;
    float k = 1.0f - SAT_T;
    float z = (a - SAT_T) / k;
    /* Pade tanh, input clamped to its accurate range */
    z = z > 3.0f ? 3.0f : z;
    float z2 = z * z;
    float t = z * (27.0f + z2) / (27.0f + 9.0f * z2);
    float y = SAT_T + k * t;
    return x < 0.0f ? -y : y;
}

static inline float flush(float v)
{
    return fabsf(v) < 1e-20f ? 0.0f : v;
}

static float clamp_sr(float sr)
{
    return sr < 8000.0f ? 44100.0f : (sr > 384000.0f ? 384000.0f : sr);
}

int delay_buffer_frames(float sample_rate)
{
    return (int)ceilf(DELAY_MAX_SECONDS * clamp_sr(sample_rate)) + 16;
}

float delay_base_seconds(float time)
{
    /* 1/32 s at 0, 1 s at 1: five octaves */
    return BASE_MIN_S * exp2f(5.0f * clampf(time, 0.0f, 1.0f));
}

float delay_speed_ratio(float speed, float time)
{
    const float base = delay_base_seconds(time);
    float r = clampf(speed, RATE_MIN, 1.0f);
    const float floor_r = base / DELAY_MAX_SECONDS; /* keeps the delay at or under DELAY_MAX_SECONDS */
    return r < floor_r ? floor_r : r;
}

float delay_time_seconds(float time, float speed)
{
    return delay_base_seconds(time) / delay_speed_ratio(speed, time);
}

float delay_tail_seconds(const delay_params *params)
{
    const float t = delay_time_seconds(params->time, params->speed);
    const float fb = clampf(params->fdb, 0.0f, 1.0f);
    if (params->input <= 0.0f && fb <= 0.0f)
        return t;
    if (fb < 0.02f)
        return t;
    if (fb >= 0.985f)
        return 30.0f;
    /* repeats until the echo is 60 dB down */
    const float repeats = logf(0.001f) / logf(fb);
    const float tail = t * (1.0f + repeats);
    return tail > 30.0f ? 30.0f : tail;
}

static void update_control(delay_t *d)
{
    const float base = delay_base_seconds(d->s_time);
    const float rate = delay_speed_ratio(d->s_speed, d->s_time);
    double dt = (double)base / (double)rate * (double)d->sr;
    dt = dt < (double)DELAY_MIN_SAMPLES ? (double)DELAY_MIN_SAMPLES : (dt > d->d_max ? d->d_max : dt);
    d->d_target = dt;
    float fc = POLE_HZ_AT_FULL_SPEED * rate;
    const float lim = 0.45f * d->sr;
    fc = fc > lim ? lim : fc;
    d->a_lp = 1.0f - expf(-2.0f * PI_F * fc / d->sr);
}

void delay_reset(delay_t *d)
{
    memset(d->buf[0], 0, sizeof(float) * (size_t)d->size);
    memset(d->buf[1], 0, sizeof(float) * (size_t)d->size);
    d->w = 0;
    d->s_time = d->target.time;
    d->s_speed = d->target.speed;
    d->s_fdb = d->target.fdb;
    d->s_input = d->target.input;
    update_control(d);
    d->d_cur = d->d_target;
    d->kcount = KR;
    d->lp1[0] = d->lp1[1] = d->lp2[0] = d->lp2[1] = 0.0f;
    d->act_peak = 0.0f;
}

void delay_init(delay_t *d, float sample_rate, float *buf_l, float *buf_r)
{
    memset(d, 0, sizeof *d);
    d->sr = clamp_sr(sample_rate);
    d->buf[0] = buf_l;
    d->buf[1] = buf_r;
    d->size = delay_buffer_frames(d->sr);
    d->d_max = (double)DELAY_MAX_SECONDS * (double)d->sr;
    d->target.time = DELAY_DEFAULT_TIME;
    d->target.speed = DELAY_DEFAULT_SPEED;
    d->target.fdb = DELAY_DEFAULT_FDB;
    d->target.input = DELAY_DEFAULT_INPUT;
    d->smooth_coef = 1.0f - expf(-1.0f / (0.030f * d->sr));
    d->d_coef = 1.0f - expf(-1.0f / (0.010f * d->sr));
    delay_reset(d);
}

void delay_set_params(delay_t *d, const delay_params *params)
{
    d->target.time = clampf(params->time, 0.0f, 1.0f);
    d->target.speed = clampf(params->speed, 0.0f, 1.0f);
    d->target.fdb = clampf(params->fdb, 0.0f, 1.0f);
    d->target.input = clampf(params->input, 0.0f, 1.0f);
}

float delay_current_seconds(const delay_t *d)
{
    return (float)(d->d_cur / (double)d->sr);
}

float delay_take_activity(delay_t *d)
{
    const float a = d->act_peak;
    d->act_peak = 0.0f;
    return a;
}

/* Cubic Hermite (Catmull-Rom) read between y0 and y1, f in 0..1. */
static inline float hermite(float ym1, float y0, float y1, float y2, float f)
{
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * f + c2) * f + c1) * f + y0;
}

static inline int wrap(int i, int n)
{
    return i < 0 ? i + n : (i >= n ? i - n : i);
}

void delay_process(delay_t *d, float *left, float *right, int frames)
{
    float *io[2] = {left, right};
    const int n = d->size;
    const float sm = d->smooth_coef;

    for (int i = 0; i < frames; i++) {
        d->s_time += sm * (d->target.time - d->s_time);
        d->s_speed += sm * (d->target.speed - d->s_speed);
        d->s_fdb += sm * (d->target.fdb - d->s_fdb);
        d->s_input += sm * (d->target.input - d->s_input);

        if (--d->kcount <= 0) {
            update_control(d);
            d->kcount = KR;
        }

        /* The delay glides toward its target, so a TIME or SPEED change sweeps the pitch of the echoes
         * (a tape speed change) instead of jumping and clicking. */
        double step = (double)d->d_coef * (d->d_target - d->d_cur);
        step = step < -(double)D_SHRINK_MAX ? -(double)D_SHRINK_MAX : (step > (double)D_GROW_MAX ? (double)D_GROW_MAX : step);
        d->d_cur += step;

        const int di = (int)d->d_cur;
        const float frac = 1.0f - (float)(d->d_cur - (double)di);
        const int i0 = d->w - di - 1;
        const int a = wrap(i0 - 1, n), b = wrap(i0, n), c = wrap(i0 + 1, n), e = wrap(i0 + 2, n);

        const float g_in = d->s_input, fb = d->s_fdb, lp = d->a_lp;

        for (int ch = 0; ch < 2; ch++) {
            float *buf = d->buf[ch];
            const float wet = hermite(buf[a], buf[b], buf[c], buf[e], frac);
            float x = io[ch][i];
            /* A NaN or runaway input must not reach the feedback loop, where it would never leave. */
            const float feed = fabsf(x) <= 16.0f ? x : 0.0f;

            float v = feed * g_in + fb * wet;
            d->lp1[ch] += lp * (v - d->lp1[ch]);
            d->lp2[ch] += lp * (d->lp1[ch] - d->lp2[ch]);
            d->lp1[ch] = flush(d->lp1[ch]);
            d->lp2[ch] = flush(d->lp2[ch]);
            buf[d->w] = flush(soft_ceiling(d->lp2[ch]));

            const float aw = fabsf(wet);
            if (aw > d->act_peak)
                d->act_peak = aw;
            if (wet != 0.0f)
                io[ch][i] = x + wet;
        }
        d->w = wrap(d->w + 1, n);
    }
}
