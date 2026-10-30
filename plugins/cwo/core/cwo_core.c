#include "cwo_core.h"

#include <math.h>
#include <string.h>

#define TWO_PI_F 6.28318530717959f
#define BUF_MASK (CWO_BUF_SIZE - 1)

/* Hilbert pair of four first-order allpass sections in z^2 per path, after Olli Niemitalo's design. A section is
 * y[n] = k * (x[n] + y[n-2]) - x[n-2] with k the square of the listed coefficient. The two paths keep unit
 * magnitude and stay about 90 degrees apart from roughly 40 Hz up to just under Nyquist (checked in the tests). */
static const float K_I[4] = {0.6923878f * 0.6923878f, 0.9360654322959f * 0.9360654322959f,
                             0.9882295226860f * 0.9882295226860f, 0.9987488452737f * 0.9987488452737f};
static const float K_Q[4] = {0.4021921162426f * 0.4021921162426f, 0.8561710882420f * 0.8561710882420f,
                             0.9722909545651f * 0.9722909545651f, 0.9952884791278f * 0.9952884791278f};

/* The oscillators of the two channels start a quarter turn apart, which spreads the repeats across the stereo field. */
#define RIGHT_PHASE_OFFSET 0.25

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Linear up to t, then a tanh-shaped knee that reaches lim with matching slope. */
static inline float soft_clip(float x, float t, float lim)
{
    float a = fabsf(x);
    if (a <= t)
        return x;
    float k = lim - t;
    float z = (a - t) / k;
    z = z > 3.0f ? 3.0f : z;
    float z2 = z * z;
    float y = t + k * (z * (27.0f + z2) / (27.0f + 9.0f * z2));
    return x < 0.0f ? -y : y;
}

static inline float flush(float v)
{
    return fabsf(v) < 1e-20f ? 0.0f : v;
}

static inline float ap_run(cwo_ap *s, float k, float x)
{
    float y = k * (x + s->y2) - s->x2;
    s->x2 = s->x1;
    s->x1 = x;
    s->y2 = s->y1;
    s->y1 = flush(y);
    return y;
}

float cwo_shift_hz(float norm)
{
    /* 0 Hz at the bottom, 0.5 * 8000^x - 0.5 above it: about 2 Hz at the factory default, 45 Hz at half, 4 kHz at full. */
    return 0.5f * (powf(8000.0f, clampf(norm, 0.0f, 1.0f)) - 1.0f);
}

float cwo_delay_ms(float norm)
{
    float n = clampf(norm, 0.0f, 1.0f);
    return CWO_MIN_DELAY_MS + (CWO_MAX_DELAY_MS - CWO_MIN_DELAY_MS) * n * n;
}

float cwo_feedback_gain(float norm)
{
    return CWO_MAX_FEEDBACK * clampf(norm, 0.0f, 1.0f);
}

static float one_pole_coef(float tau_seconds, float sr)
{
    return 1.0f - expf(-1.0f / (tau_seconds * sr));
}

static float delay_samples_for(const cwo_t *p, float norm)
{
    float d = cwo_delay_ms(norm) * 0.001f * p->sr;
    return clampf(d, (float)p->min_delay_samples, (float)p->max_delay_samples);
}

void cwo_reset(cwo_t *p)
{
    for (int c = 0; c < 2; c++) {
        memset(p->ch[c].buf, 0, sizeof p->ch[c].buf);
        memset(p->ch[c].ap_i, 0, sizeof p->ch[c].ap_i);
        memset(p->ch[c].ap_q, 0, sizeof p->ch[c].ap_q);
        p->ch[c].i_delay = 0.0f;
        p->ch[c].phase = c == 0 ? 0.0 : RIGHT_PHASE_OFFSET;
    }
    p->write_pos = 0;
    p->s_freq = p->target.freq;
    p->s_fdback = p->target.fdback;
    p->s_side = p->target.sideband;
    p->delay_now = delay_samples_for(p, p->target.delay);
    p->act_peak = 0.0f;
}

void cwo_init(cwo_t *p, float sample_rate)
{
    memset(p, 0, sizeof *p);
    p->sr = sample_rate > 8000.0f ? sample_rate : 44100.0f;
    p->target.freq = CWO_DEFAULT_FREQ;
    p->target.delay = CWO_DEFAULT_DELAY;
    p->target.fdback = CWO_DEFAULT_FDBACK;
    p->target.sideband = CWO_DEFAULT_SIDEBAND;
    p->smooth_coef = one_pole_coef(0.02f, p->sr);
    p->delay_coef = one_pole_coef(0.06f, p->sr);

    /* Delay memory follows the sample rate: 600 ms, capped by the fixed buffer. */
    float longest = CWO_MAX_DELAY_MS * 0.001f * p->sr;
    float cap = (float)(CWO_BUF_SIZE - 16);
    p->max_delay_samples = (int)(longest < cap ? longest : cap);
    int shortest = (int)ceilf(CWO_MIN_DELAY_MS * 0.001f * p->sr);
    p->min_delay_samples = shortest < 8 ? 8 : shortest; /* the cubic read needs a few samples of room */
    cwo_reset(p);
}

void cwo_set_params(cwo_t *p, const cwo_params *params)
{
    p->target.freq = clampf(params->freq, 0.0f, 1.0f);
    p->target.delay = clampf(params->delay, 0.0f, 1.0f);
    p->target.fdback = clampf(params->fdback, 0.0f, 1.0f);
    p->target.sideband = clampf(params->sideband, 0.0f, 1.0f);
}

float cwo_take_activity(cwo_t *p)
{
    const float a = p->act_peak;
    p->act_peak = 0.0f;
    return a;
}

/* Four-point Catmull-Rom read, `delay` samples behind the write position. The write position itself is not valid yet. */
static inline float read_delay(const float *buf, int write_pos, float delay)
{
    int di = (int)delay;
    float f = delay - (float)di;
    int base = write_pos - di - 1; /* sample just older than the read point */
    float g = 1.0f - f;            /* fraction from base toward base + 1 */
    float ym1 = buf[(base - 1) & BUF_MASK];
    float y0 = buf[base & BUF_MASK];
    float y1 = buf[(base + 1) & BUF_MASK];
    float y2 = buf[(base + 2) & BUF_MASK];
    float c1 = 0.5f * (y1 - ym1);
    float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * g + c2) * g + c1) * g + y0;
}

void cwo_process(cwo_t *p, float *left, float *right, int frames)
{
    float *io[2] = {left, right};
    const float sm = p->smooth_coef;
    const float inv_sr = 1.0f / p->sr;
    const float max_hz = 0.45f * p->sr;
    float target_delay = delay_samples_for(p, p->target.delay);

    for (int i = 0; i < frames; i++) {
        p->s_freq += sm * (p->target.freq - p->s_freq);
        p->s_fdback += sm * (p->target.fdback - p->s_fdback);
        p->s_side += sm * (p->target.sideband - p->s_side);

        /* Delay time glides toward its target like tape speeding up or slowing down. Reading speed stays between
         * 0.1x and 1.9x, so the read point is continuous and never overtakes the write point. */
        float step = (target_delay - p->delay_now) * p->delay_coef;
        step = clampf(step, -0.9f, 0.9f);
        p->delay_now += step;

        const float hz = fminf(cwo_shift_hz(p->s_freq), max_hz);
        const double inc = (double)(hz * inv_sr);
        const float fb = cwo_feedback_gain(p->s_fdback);
        const float q_gain = 1.0f - 2.0f * p->s_side; /* weight of the sine term: -1 up, 0 ring mod, +1 down */

        for (int c = 0; c < 2; c++) {
            cwo_channel *ch = &p->ch[c];
            const float x = io[c][i];

            float d = read_delay(ch->buf, p->write_pos, p->delay_now);

            /* In-phase path sees the signal one sample late, quadrature path sees it now. */
            float vi = ch->i_delay;
            ch->i_delay = d;
            float vq = d;
            for (int k = 0; k < 4; k++) {
                vi = ap_run(&ch->ap_i[k], K_I[k], vi);
                vq = ap_run(&ch->ap_q[k], K_Q[k], vq);
            }

            /* With a sine in, vi = sin(t) and vq = cos(t) (up to the shared phase lag), so
             * vq*cos(w) - vi*sin(w) is the upper sideband and vq*cos(w) + vi*sin(w) the lower one. */
            const float theta = TWO_PI_F * (float)ch->phase;
            const float shifted = vq * cosf(theta) + q_gain * vi * sinf(theta);

            ch->phase += inc;
            if (ch->phase >= 1.0)
                ch->phase -= 1.0;

            const float a = fabsf(shifted);
            if (a > p->act_peak)
                p->act_peak = a;

            ch->buf[p->write_pos & BUF_MASK] = flush(soft_clip(x + fb * shifted, 1.0f, 2.0f));
            io[c][i] = soft_clip(x + shifted, 0.9f, 1.0f);
        }
        p->write_pos = (p->write_pos + 1) & BUF_MASK;
    }
}
