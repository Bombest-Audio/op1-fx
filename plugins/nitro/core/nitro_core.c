#include "nitro_core.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f

#define FREQ_MIN_HZ 14.0f
#define FREQ_MAX_HZ 16000.0f
#define MAX_FB 4.4f /* a little past the 4.0 where a four-pole loop starts to ring on its own */
#define SAT_T 0.75f /* soft ceiling is linear up to here */

/* Mix of [input, stage 1, stage 2, stage 3, stage 4] for each shape. Plain combinations of the stage outputs:
 * the binomial rows give the low-pass and high-pass slopes, 1 -2 2 is a notch, 0 1 -1 is a band-pass. */
static const float SHAPES[NITRO_SHAPE_ROWS][5] = {
    {0, 0, 0, 0, 1},     /* LP 24 */
    {0, 0, 0, 1, 0},     /* LP 18 */
    {0, 0, 1, 0, 0},     /* LP 12 */
    {0, 1, 0, 0, 0},     /* LP 6 */
    {1, 0, 0, 0, 0},     /* THRU */
    {1, -3, 6, -4, 0},   /* HP 6+ */
    {1, -2, 2, 0, 0},    /* NOTCH */
    {0, 1, -1, 0, 0},    /* BAND */
    {1, -1, 0, 0, 0},    /* HP 6 */
    {1, -2, 1, 0, 0},    /* HP 12 */
    {1, -3, 3, -1, 0},   /* HP 18 */
    {1, -4, 6, -4, 1},   /* HP 24 */
};

static const char *const SHAPE_NAMES[NITRO_SHAPE_ROWS] = {"LP 24", "LP 18", "LP 12", "LP 6", "THRU", "HP 6+",
                                                          "NOTCH", "BAND",  "HP 6",  "HP 12", "HP 18", "HP 24"};

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Linear up to SAT_T, then a tanh knee that reaches 1.0 with matching slope. Output never exceeds 1.0. */
static inline float soft_ceiling(float x)
{
    float a = fabsf(x);
    if (a <= SAT_T)
        return x;
    float k = 1.0f - SAT_T;
    float z = (a - SAT_T) / k;
    z = z > 3.0f ? 3.0f : z; /* Pade tanh, input clamped to its accurate range */
    float z2 = z * z;
    float t = z * (27.0f + z2) / (27.0f + 9.0f * z2);
    float y = SAT_T + k * t;
    return x < 0.0f ? -y : y;
}

static inline float flush(float v)
{
    return fabsf(v) < 1e-20f ? 0.0f : v;
}

float nitro_freq_hz(float norm)
{
    return FREQ_MIN_HZ * powf(FREQ_MAX_HZ / FREQ_MIN_HZ, clampf(norm, 0.0f, 1.0f));
}

float nitro_freq_to_norm(float hz)
{
    return clampf(logf(hz / FREQ_MIN_HZ) / logf(FREQ_MAX_HZ / FREQ_MIN_HZ), 0.0f, 1.0f);
}

const char *nitro_shape_name(float norm)
{
    int i = (int)floorf(clampf(norm, 0.0f, 1.0f) * (float)(NITRO_SHAPE_ROWS - 1) + 0.5f);
    return SHAPE_NAMES[i];
}

void nitro_reset(nitro_t *n)
{
    memset(n->ch, 0, sizeof n->ch);
    n->s_freq = n->target.freq;
    n->s_res = n->target.res;
    n->s_shape = n->target.shape;
    n->s_drive = n->target.drive;
    n->s_mix = n->target.mix;
    n->act_peak = 0.0f;
}

void nitro_init(nitro_t *n, float sample_rate)
{
    memset(n, 0, sizeof *n);
    n->sr = sample_rate > 8000.0f ? sample_rate : 44100.0f;
    n->target.freq = NITRO_DEFAULT_FREQ;
    n->target.res = NITRO_DEFAULT_RES;
    n->target.shape = NITRO_DEFAULT_SHAPE;
    n->target.drive = NITRO_DEFAULT_DRIVE;
    n->target.mix = NITRO_DEFAULT_MIX;
    n->smooth_coef = 1.0f - expf(-1.0f / (0.005f * n->sr)); /* 5 ms */
    n->env_decay = expf(-1.0f / (0.186f * n->sr));
    nitro_reset(n);
}

void nitro_set_params(nitro_t *n, const nitro_params *p)
{
    n->target.freq = clampf(p->freq, 0.0f, 1.0f);
    n->target.res = clampf(p->res, 0.0f, 1.0f);
    n->target.shape = clampf(p->shape, 0.0f, 1.0f);
    n->target.drive = clampf(p->drive, 0.0f, 1.0f);
    n->target.mix = clampf(p->mix, 0.0f, 1.0f);
}

float nitro_take_activity(nitro_t *n)
{
    const float a = n->act_peak;
    n->act_peak = 0.0f;
    return a;
}

/* One sample through one channel. G is the trapezoidal one-pole gain g / (1 + g), fb the feedback amount from
 * stage 4 to the input, in_gain the pre-filter gain (drive and resonance make-up), c the five-tap output mix. */
static inline float run_channel(nitro_channel *c, float x, float G, float fb, float in_gain, const float *mix,
                                float env_decay)
{
    const float ax = fabsf(x);
    c->env = ax > c->env ? ax : c->env * env_decay;

    const float a = 1.0f - G;
    const float G2 = G * G;
    const float G3 = G2 * G;
    const float G4 = G2 * G2;

    /* y4 = G^4 * u + rest, where rest comes from the stage states alone. Solving u = xd - fb * y4 for u gives the
     * feedback loop without a one-sample delay, so it stays stable up to the cutoff limit. */
    const float rest = a * (G3 * c->s[0] + G2 * c->s[1] + G * c->s[2] + c->s[3]);
    const float xd = soft_ceiling(x * in_gain);
    const float u = soft_ceiling((xd - fb * rest) / (1.0f + fb * G4));

    float y[4];
    float in = u;
    for (int k = 0; k < 4; k++) {
        const float v = (in - c->s[k]) * G;
        y[k] = v + c->s[k];
        c->s[k] = flush(y[k] + v);
        in = y[k];
    }
    c->env = flush(c->env);

    const float m = mix[0] * u + mix[1] * y[0] + mix[2] * y[1] + mix[3] * y[2] + mix[4] * y[3];
    return soft_ceiling(m);
}

void nitro_process(nitro_t *n, float *left, float *right, int frames)
{
    const float sm = n->smooth_coef;
    const int nch = right ? 2 : 1;
    float *buf[2] = {left, right};

    for (int i = 0; i < frames; i++) {
        n->s_freq += sm * (n->target.freq - n->s_freq);
        n->s_res += sm * (n->target.res - n->s_res);
        n->s_shape += sm * (n->target.shape - n->s_shape);
        n->s_drive += sm * (n->target.drive - n->s_drive);
        n->s_mix += sm * (n->target.mix - n->s_mix);

        float fc = nitro_freq_hz(n->s_freq);
        const float fmax = 0.45f * n->sr;
        fc = fc > fmax ? fmax : fc;
        const float g = tanf(PI_F * fc / n->sr);
        const float G = g / (1.0f + g);

        /* SHAPE walks the table linearly, crossfading the two neighbouring rows. */
        const float pos = n->s_shape * (float)(NITRO_SHAPE_ROWS - 1);
        int r = (int)pos;
        r = r > NITRO_SHAPE_ROWS - 2 ? NITRO_SHAPE_ROWS - 2 : r;
        const float fr = pos - (float)r;
        float mix[5];
        for (int k = 0; k < 5; k++)
            mix[k] = SHAPES[r][k] * (1.0f - fr) + SHAPES[r + 1][k] * fr;

        /* DRIVE: 0 dB to +24 dB of gain into the soft ceiling, quadratic so the low half of the knob is gentle. */
        const float d = n->s_drive;
        const float drive_gain = powf(10.0f, 1.2f * d * d);

        const float res = n->s_res;
        for (int c = 0; c < nch; c++) {
            /* Feedback grows with RES squared and a little with DRIVE, and the louder the input the more it adds.
             * RES at 1 with DRIVE at 1 reaches the point where the loop rings on its own; below that it needs signal. */
            const float fb = MAX_FB * clampf(res * res * (0.8f + 0.2f * d) + 0.2f * res * n->ch[c].env, 0.0f, 1.0f);
            /* Partial make-up for the bass the feedback takes away. */
            const float in_gain = drive_gain * (1.0f + 0.35f * fb);
            /* MIX blends the untouched input with the filtered signal. The filter always runs, so its state stays
             * continuous while MIX moves. At MIX 1 this is exactly the wet signal, at MIX 0 exactly the input. */
            const float dry = buf[c][i];
            const float wet = run_channel(&n->ch[c], dry, G, fb, in_gain, mix, n->env_decay);
            buf[c][i] = dry * (1.0f - n->s_mix) + wet * n->s_mix;
            if (n->ch[c].env > n->act_peak)
                n->act_peak = n->ch[c].env;
        }
    }
}
