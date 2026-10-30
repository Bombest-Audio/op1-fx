#include "punch_core.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f

/* Soft ceiling. Linear up to SAT_T, then a tanh knee that reaches 1.0 with matching slope. */
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

int punch_rounds_from_norm(float v)
{
    int r = 1 + (int)floorf(clampf(v, 0.0f, 1.0f) * (float)(PUNCH_MAX_ROUNDS - 1) + 0.5f);
    return r < 1 ? 1 : (r > PUNCH_MAX_ROUNDS ? PUNCH_MAX_ROUNDS : r);
}

float punch_rounds_to_norm(int rounds)
{
    int r = rounds < 1 ? 1 : (rounds > PUNCH_MAX_ROUNDS ? PUNCH_MAX_ROUNDS : rounds);
    return (float)(r - 1) / (float)(PUNCH_MAX_ROUNDS - 1);
}

float punch_freq_hz(float norm)
{
    return 30.0f * exp2f(clampf(norm, 0.0f, 1.0f) * 8.0f);
}

static float one_pole_coef(float tau_seconds, float sr)
{
    return 1.0f - expf(-1.0f / (tau_seconds * sr));
}

void punch_reset(punch_t *p)
{
    memset(p->round, 0, sizeof p->round);
    p->s_freq = p->target.freq;
    p->s_punch = p->target.punch;
    p->s_power = p->target.power;
    p->w_sum = 0.0f;
    p->act_peak = 0.0f;
    for (int r = 0; r < PUNCH_MAX_ROUNDS; r++) {
        p->round[r].w = r < p->target.rounds ? 1.0f : 0.0f;
        p->w_sum += p->round[r].w;
    }
}

void punch_init(punch_t *p, float sample_rate)
{
    memset(p, 0, sizeof *p);
    p->sr = sample_rate > 8000.0f ? sample_rate : 44100.0f;
    p->target.freq = PUNCH_DEFAULT_FREQ;
    p->target.punch = PUNCH_DEFAULT_PUNCH;
    p->target.rounds = PUNCH_DEFAULT_ROUNDS;
    p->target.power = PUNCH_DEFAULT_POWER;
    p->smooth_coef = one_pole_coef(0.005f, p->sr);

    /* Each round looks at a longer time scale than the one before it. Round 0 catches
     * sharp clicks (0.3 ms attack window), round 23 catches slow swells (about 16 ms). */
    for (int r = 0; r < PUNCH_MAX_ROUNDS; r++) {
        float scale = powf(1.18f, (float)r);
        p->af[r] = one_pole_coef(0.0003f * scale, p->sr);
        p->rf[r] = one_pole_coef(0.0030f * scale, p->sr);
        p->as[r] = one_pole_coef(0.0060f * scale, p->sr);
        p->rs[r] = one_pole_coef(0.0600f * scale, p->sr);
    }
    punch_reset(p);
}

void punch_set_params(punch_t *p, const punch_params *params)
{
    p->target.freq = clampf(params->freq, 0.0f, 1.0f);
    p->target.punch = clampf(params->punch, 0.0f, 1.0f);
    p->target.power = clampf(params->power, 0.0f, 1.0f);
    int r = params->rounds;
    p->target.rounds = r < 1 ? 1 : (r > PUNCH_MAX_ROUNDS ? PUNCH_MAX_ROUNDS : r);
}

float punch_take_activity(punch_t *p)
{
    const float a = p->act_peak;
    p->act_peak = 0.0f;
    return a;
}

void punch_process(punch_t *p, float *left, float *right, int frames)
{
    const float sm = p->smooth_coef;
    const float w_coef = sm;
    const int rounds = p->target.rounds;

    for (int i = 0; i < frames; i++) {
        p->s_freq += sm * (p->target.freq - p->s_freq);
        p->s_punch += sm * (p->target.punch - p->s_punch);
        p->s_power += sm * (p->target.power - p->s_power);

        const float dry_l = left[i];
        const float dry_r = right[i];
        const float power = p->s_power;

        if (power <= 0.0f && p->target.power <= 0.0f)
            continue; /* fully dry, leave the buffer untouched */

        const float a_lp = 1.0f - expf(-2.0f * PI_F * punch_freq_hz(p->s_freq) / p->sr);
        /* PUNCH sets how hard a detected transient is pushed. The push is split evenly
         * across the active rounds, so the cascade multiplies to roughly the same total
         * boost (up to about +13 dB at full PUNCH) whatever ROUNDS is. */
        const float per_round = p->s_punch * 0.5f / (p->w_sum < 1.0f ? 1.0f : p->w_sum);
        float w_sum = 0.0f;

        const float drive = 1.0f + 0.7f * power;
        float l = dry_l * drive;
        float r = dry_r * drive;

        for (int k = 0; k < PUNCH_MAX_ROUNDS; k++) {
            punch_round *rd = &p->round[k];
            const float w_target = k < rounds ? 1.0f : 0.0f;
            rd->w += w_coef * (w_target - rd->w);
            if (rd->w < 1e-4f && w_target == 0.0f) {
                rd->w = 0.0f;
                continue;
            }

            const float m = 0.5f * (l + r);
            rd->lp += a_lp * (m - rd->lp);
            const float x = fabsf(m - rd->lp);

            rd->ef += (x > rd->ef ? p->af[k] : p->rf[k]) * (x - rd->ef);
            rd->es += (x > rd->es ? p->as[k] : p->rs[k]) * (x - rd->es);
            rd->lp = flush(rd->lp);
            rd->ef = flush(rd->ef);
            rd->es = flush(rd->es);

            /* relative transient strength, with a floor so the noise floor is not boosted */
            float t = (rd->ef - rd->es) / (rd->es + 0.01f);
            t = clampf(t, 0.0f, 3.0f);

            if (k == 0 && t * rd->w > p->act_peak)
                p->act_peak = t * rd->w;

            w_sum += rd->w;
            const float g = 1.0f + rd->w * per_round * t;
            l = soft_ceiling(l * g);
            r = soft_ceiling(r * g);
        }

        p->w_sum = w_sum;
        left[i] = dry_l * (1.0f - power) + l * power;
        right[i] = dry_r * (1.0f - power) + r * power;
    }
}
