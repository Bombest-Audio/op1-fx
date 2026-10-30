#include "spring_core.h"

#include <math.h>
#include <string.h>

#if SPRING_COUNT != 3
#error spring_disp_step3 and the output mix are written for three springs
#endif

#define PI_F 3.14159265358979f
#define CTL_PERIOD 16       /* samples between coefficient updates */
#define SEND_SCALE 0.90f    /* LEVEL^2 times this is the gain into the tank */
#define COUPLE_SELF 0.84f   /* each spring feeds back mostly itself... */
#define COUPLE_NEXT 0.16f   /* ...and a little of the next one. The sum is 1.0, so the matrix cannot add energy. */
#define NOISE_FLOOR 1e-24f  /* alternating-sign offset that keeps every tank state far above the denormal range */

/* Delay line lengths in samples at 44.1 kHz. Different primes-ish so the three springs do not line up. */
static const float BASE_DELAY[SPRING_COUNT] = {1103.0f, 1459.0f, 1951.0f};

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

float spring_tone_hz(float norm)
{
    return 1200.0f * powf(7.5f, clampf(norm, 0.0f, 1.0f));
}

float spring_rt60(float damp)
{
    const float d = clampf(damp, 0.0f, 1.0f);
    return 0.45f / (d * d * d + 0.1f);
}

void spring_disp_init(spring_disp *d, int m)
{
    memset(d, 0, sizeof *d);
    d->m = m < 1 ? 1 : (m > SPRING_MAX_M ? SPRING_MAX_M : m);
}

float spring_disp_step(spring_disp *d, float x, float a)
{
    const int m = d->m;
    float *p = d->st + d->pos;
    for (int s = 0; s < SPRING_STAGES; s++, p += m) {
        const float wd = *p;      /* w[n-M] */
        const float w = x - a * wd;
        *p = w;
        x = a * w + wd;
    }
    if (++d->pos >= m)
        d->pos = 0;
    return x;
}

void spring_disp_step3(spring_disp *d, const float *x, float *y, float a)
{
    const int m = d[0].m;
    float *p0 = d[0].st + d[0].pos;
    float *p1 = d[1].st + d[1].pos;
    float *p2 = d[2].st + d[2].pos;
    float x0 = x[0], x1 = x[1], x2 = x[2];
    for (int s = 0; s < SPRING_STAGES; s++, p0 += m, p1 += m, p2 += m) {
        const float wd0 = *p0, wd1 = *p1, wd2 = *p2;
        const float w0 = x0 - a * wd0, w1 = x1 - a * wd1, w2 = x2 - a * wd2;
        *p0 = w0;
        *p1 = w1;
        *p2 = w2;
        x0 = a * w0 + wd0;
        x1 = a * w1 + wd1;
        x2 = a * w2 + wd2;
    }
    for (int i = 0; i < 3; i++)
        if (++d[i].pos >= d[i].m)
            d[i].pos = 0;
    y[0] = x0;
    y[1] = x1;
    y[2] = x2;
}

static void update_coefs(spring_t *s)
{
    const float sr = s->sr;
    s->a = -(0.2f + 0.6f * s->s_turns);

    float fc = spring_tone_hz(s->s_tone);
    if (fc > 0.45f * sr)
        fc = 0.45f * sr;
    s->lp_k = 1.0f - expf(-2.0f * PI_F * fc / sr);

    /* Gain per trip around a spring so the bass decays by 60 dB in RT60 seconds. A trip is the delay line
     * plus the average group delay of the chain, which is M samples per section whatever the coefficient. */
    const float rt60 = spring_rt60(s->s_damp);
    for (int i = 0; i < SPRING_COUNT; i++) {
        const float trip = ((float)s->delay[i] + (float)(SPRING_STAGES * s->m)) / sr;
        s->gain[i] = expf(-6.9078f * trip / rt60);
    }

    const float lv = s->s_level;
    s->send_gain = SEND_SCALE * lv * lv;
}

void spring_reset(spring_t *s)
{
    memset(s->line, 0, sizeof s->line);
    for (int i = 0; i < SPRING_COUNT; i++) {
        spring_disp_init(&s->disp[i], s->m);
        s->wpos[i] = 0;
        s->fb[i] = 0.0f;
        s->lp[i] = 0.0f;
        s->dc_x[i] = 0.0f;
        s->dc_y[i] = 0.0f;
    }
    s->in_hp = 0.0f;
    s->flip = NOISE_FLOOR;
    s->tail = 0.0f;
    s->act_peak = 0.0f;
    s->s_tone = s->target.tone;
    s->s_turns = s->target.turns;
    s->s_damp = s->target.damp;
    s->s_level = s->target.level;
    s->ctl_count = CTL_PERIOD;
    update_coefs(s);
}

void spring_init(spring_t *s, float sample_rate)
{
    memset(s, 0, sizeof *s);
    s->sr = clampf(sample_rate, SPRING_MIN_SR, SPRING_MAX_SR);
    s->target.tone = SPRING_DEFAULT_TONE;
    s->target.turns = SPRING_DEFAULT_TURNS;
    s->target.damp = SPRING_DEFAULT_DAMP;
    s->target.level = SPRING_DEFAULT_LEVEL;
    s->smooth_coef = 1.0f - expf(-1.0f / (0.01f * s->sr));
    s->m = (int)floorf(4.0f * s->sr / 44100.0f + 0.5f);
    if (s->m < 1)
        s->m = 1;
    if (s->m > SPRING_MAX_M)
        s->m = SPRING_MAX_M;
    for (int i = 0; i < SPRING_COUNT; i++) {
        int d = (int)floorf(BASE_DELAY[i] * s->sr / 44100.0f + 0.5f);
        s->delay[i] = d < 8 ? 8 : (d > SPRING_MAX_DELAY - 1 ? SPRING_MAX_DELAY - 1 : d);
    }
    s->dc_r = expf(-2.0f * PI_F * 40.0f / s->sr);
    s->in_hp_k = 1.0f - expf(-2.0f * PI_F * 150.0f / s->sr);
    spring_reset(s);
}

void spring_set_params(spring_t *s, const spring_params *p)
{
    s->target.tone = clampf(p->tone, 0.0f, 1.0f);
    s->target.turns = clampf(p->turns, 0.0f, 1.0f);
    s->target.damp = clampf(p->damp, 0.0f, 1.0f);
    s->target.level = clampf(p->level, 0.0f, 1.0f);
}

float spring_take_activity(spring_t *s)
{
    const float a = s->act_peak;
    s->act_peak = 0.0f;
    return a;
}

void spring_process(spring_t *s, float *left, float *right, int frames)
{
    const float sm = s->smooth_coef;

    for (int n = 0; n < frames; n++) {
        s->s_tone += sm * (s->target.tone - s->s_tone);
        s->s_turns += sm * (s->target.turns - s->s_turns);
        s->s_damp += sm * (s->target.damp - s->s_damp);
        s->s_level += sm * (s->target.level - s->s_level);
        if (s->target.level == 0.0f && s->s_level < 1e-4f)
            s->s_level = 0.0f;

        if (--s->ctl_count <= 0) {
            update_coefs(s);
            s->ctl_count = CTL_PERIOD;
        }

        /* Nothing driving the tank and nothing left ringing: pass the audio through untouched. */
        if (s->s_level == 0.0f && s->target.level == 0.0f && s->tail < 1e-7f)
            continue;

        const float dry_l = left[n];
        const float dry_r = right[n];

        /* The pickup does not reproduce deep bass: high-pass the send at 150 Hz. */
        const float mono = 0.5f * (dry_l + dry_r);
        s->in_hp += s->in_hp_k * (mono - s->in_hp);
        const float send = (mono - s->in_hp) * s->send_gain;

        s->flip = -s->flip;
        const float u_noise = s->flip;

        float u[SPRING_COUNT], dsp[SPRING_COUNT], o[SPRING_COUNT];
        for (int i = 0; i < SPRING_COUNT; i++)
            u[i] = send + s->fb[i] + u_noise;
        spring_disp_step3(s->disp, u, dsp, s->a);
        for (int i = 0; i < SPRING_COUNT; i++) {
            const float d = dsp[i];
            int rp = s->wpos[i] - s->delay[i];
            if (rp < 0)
                rp += SPRING_MAX_DELAY;
            o[i] = s->line[i][rp];
            s->line[i][s->wpos[i]] = d;
            if (++s->wpos[i] >= SPRING_MAX_DELAY)
                s->wpos[i] = 0;
        }

        float pk = 0.0f;
        for (int i = 0; i < SPRING_COUNT; i++) {
            const float v = COUPLE_SELF * o[i] + COUPLE_NEXT * o[(i + 1) % SPRING_COUNT];
            s->lp[i] += s->lp_k * (v - s->lp[i]);
            const float x = s->lp[i];
            const float y = x - s->dc_x[i] + s->dc_r * s->dc_y[i];
            s->dc_x[i] = x;
            s->dc_y[i] = y;
            s->fb[i] = s->gain[i] * y;
            const float a = fabsf(o[i]);
            pk = a > pk ? a : pk;
        }

        s->tail = pk > s->tail * 0.9995f ? pk : s->tail * 0.9995f;
        if (pk > s->act_peak)
            s->act_peak = pk;

        /* Each spring is picked up at a different point, so the two channels decorrelate. */
        left[n] = dry_l + 0.70f * o[0] + 0.45f * o[1] + 0.15f * o[2];
        right[n] = dry_r + 0.15f * o[0] + 0.45f * o[1] + 0.70f * o[2];
    }
}
