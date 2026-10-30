#include "drumsampler_kit.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define TWO_PI 6.283185307179586

/* One recipe per sound. Every sound is the sum of up to four layers:
 *   body   a sine that sweeps from f0 down to f1 (time constant pd) under an exponential decay (bd)
 *   noise  white noise limited to the band between hp and lp (the difference of two low-passes with those cutoffs),
 *          exponential decay (nd), optional swell (nswell seconds up) or a clap pattern of `bursts` short bursts
 *   metal  mcount square oscillators at fixed inharmonic ratios of mbase, high-passed near mhp, decay md
 *   click  a one millisecond noise burst
 * and then a tanh drive. */
typedef struct {
    const char *name;
    double len;
    double f0, f1, pd, bd, bamp;
    double namp, nd, nswell, hp, lp;
    int bursts;
    double mamp, mbase, md, mhp;
    int mcount;
    double click, drive;
} recipe_t;

static const recipe_t R[DS_KIT_SOUNDS] = {
    /* name        len   f0     f1    pd     bd     bamp  namp  nd     swell hp    lp     bursts mamp mbase md    mhp   mc click drive */
    {"Kick",       0.45, 140.0, 48.0, 0.035, 0.16,  1.0,  0.0,  0.0,   0.0, 0.0,  0.0,   0,  0.0, 0.0,  0.0,  0.0,   0, 0.30, 1.5},
    {"Kick Long",  0.90, 90.0,  42.0, 0.050, 0.45,  1.0,  0.0,  0.0,   0.0, 0.0,  0.0,   0,  0.0, 0.0,  0.0,  0.0,   0, 0.10, 1.2},
    {"Snare",      0.30, 220.0, 170.0, 0.020, 0.08, 0.6,  0.9,  0.11,  0.0, 1500.0, 9000.0, 0, 0.0, 0.0, 0.0, 0.0,   0, 0.10, 1.5},
    {"Snare Tight", 0.20, 260.0, 200.0, 0.015, 0.05, 0.5, 0.8,  0.06,  0.0, 2000.0, 10000.0, 0, 0.0, 0.0, 0.0, 0.0,  0, 0.15, 1.5},
    {"Clap",       0.35, 0.0,   0.0,  0.0,   0.0,   0.0,  1.0,  0.12,  0.0, 900.0, 6000.0, 4,  0.0, 0.0,  0.0,  0.0,   0, 0.0,  1.3},
    {"Rim",        0.12, 1700.0, 1500.0, 0.003, 0.02, 0.7, 0.3, 0.01,  0.0, 2500.0, 8000.0, 0, 0.0, 0.0, 0.0, 0.0,   0, 0.60, 1.4},
    {"Hat Closed", 0.12, 0.0,   0.0,  0.0,   0.0,   0.0,  0.3,  0.025, 0.0, 7000.0, 15000.0, 0, 1.0, 330.0, 0.03, 6000.0, 6, 0.0, 1.0},
    {"Hat Open",   0.60, 0.0,   0.0,  0.0,   0.0,   0.0,  0.25, 0.20,  0.0, 7000.0, 15000.0, 0, 1.0, 330.0, 0.25, 6000.0, 6, 0.0, 1.0},
    {"Hat Pedal",  0.20, 0.0,   0.0,  0.0,   0.0,   0.0,  0.3,  0.05,  0.0, 6500.0, 14000.0, 0, 1.0, 300.0, 0.06, 5500.0, 6, 0.0, 1.0},
    {"Tom Low",    0.50, 120.0, 70.0, 0.060, 0.22,  1.0,  0.1,  0.02,  0.0, 800.0, 5000.0, 0,  0.0, 0.0,  0.0,  0.0,   0, 0.15, 1.2},
    {"Tom Mid",    0.40, 170.0, 105.0, 0.050, 0.18, 1.0,  0.1,  0.02,  0.0, 800.0, 5000.0, 0,  0.0, 0.0,  0.0,  0.0,   0, 0.15, 1.2},
    {"Tom High",   0.35, 240.0, 150.0, 0.040, 0.15, 1.0,  0.1,  0.02,  0.0, 800.0, 5000.0, 0,  0.0, 0.0,  0.0,  0.0,   0, 0.15, 1.2},
    {"Crash",      1.00, 0.0,   0.0,  0.0,   0.0,   0.0,  0.6,  0.50,  0.0, 4000.0, 14000.0, 0, 0.8, 280.0, 0.60, 3000.0, 6, 0.0, 1.0},
    {"Ride",       0.90, 0.0,   0.0,  0.0,   0.0,   0.0,  0.1,  0.15,  0.0, 5000.0, 14000.0, 0, 1.0, 520.0, 0.40, 5000.0, 6, 0.0, 1.0},
    {"Cowbell",    0.35, 0.0,   0.0,  0.0,   0.0,   0.0,  0.0,  0.0,   0.0, 0.0,  0.0,   0,  1.0, 562.0, 0.12, 400.0,  2, 0.05, 1.2},
    {"Clave",      0.12, 2500.0, 2400.0, 0.010, 0.03, 1.0, 0.0, 0.0,   0.0, 0.0,  0.0,   0,  0.0, 0.0,  0.0,  0.0,   0, 0.20, 1.1},
    {"Shaker",     0.25, 0.0,   0.0,  0.0,   0.0,   0.0,  1.0,  0.09,  0.03, 5000.0, 12000.0, 0, 0.0, 0.0, 0.0, 0.0,  0, 0.0,  1.0},
    {"Tambourine", 0.40, 0.0,   0.0,  0.0,   0.0,   0.0,  0.5,  0.10,  0.0, 6000.0, 14000.0, 0, 0.8, 700.0, 0.12, 5000.0, 6, 0.0, 1.0},
    {"Conga",      0.35, 330.0, 250.0, 0.020, 0.15, 0.9,  0.0,  0.0,   0.0, 0.0,  0.0,   0,  0.0, 0.0,  0.0,  0.0,   0, 0.20, 1.2},
    {"Bongo",      0.25, 450.0, 360.0, 0.015, 0.10, 0.9,  0.0,  0.0,   0.0, 0.0,  0.0,   0,  0.0, 0.0,  0.0,  0.0,   0, 0.20, 1.2},
    {"Blip",       0.20, 1800.0, 900.0, 0.050, 0.08, 1.0, 0.0,  0.0,   0.0, 0.0,  0.0,   0,  0.0, 0.0,  0.0,  0.0,   0, 0.0,  1.0},
    {"Zap",        0.40, 3000.0, 80.0, 0.120, 0.25, 1.0,  0.0,  0.0,   0.0, 0.0,  0.0,   0,  0.0, 0.0,  0.0,  0.0,   0, 0.0,  2.0},
    {"Sub Bass",   0.80, 55.0,  55.0, 0.010, 0.50,  1.0,  0.0,  0.0,   0.0, 0.0,  0.0,   0,  0.0, 0.0,  0.0,  0.0,   0, 0.0,  1.8},
    {"Noise Sweep", 0.70, 0.0,  0.0,  0.0,   0.0,   0.0,  1.0,  0.35,  0.25, 800.0, 6000.0, 0, 0.0, 0.0, 0.0, 0.0,   0, 0.0,  1.0},
};

/* Classic 808-style inharmonic ratios for the metallic layer. */
static const double METAL_RATIO[6] = {1.0, 1.4471, 1.6170, 1.9265, 2.5028, 2.6637};

const char *ds_kit_name(int index)
{
    return index >= 0 && index < DS_KIT_SOUNDS ? R[index].name : "";
}

int ds_kit_frames(int index)
{
    return index >= 0 && index < DS_KIT_SOUNDS ? (int)(R[index].len * DS_KIT_RATE) : 0;
}

/* Two cascaded one-pole low-passes. A high-pass is the input minus this. */
typedef struct {
    double a, z1, z2;
} lp2_t;

static void lp2_set(lp2_t *f, double cutoff)
{
    f->a = 1.0 - exp(-TWO_PI * cutoff / (double)DS_KIT_RATE);
    f->z1 = f->z2 = 0.0;
}

static double lp2_run(lp2_t *f, double x)
{
    f->z1 += f->a * (x - f->z1);
    f->z2 += f->a * (f->z1 - f->z2);
    return f->z2;
}

int ds_kit_render(int index, float *out, int cap)
{
    if (index < 0 || index >= DS_KIT_SOUNDS)
        return 0;
    const recipe_t *r = &R[index];
    const int n = ds_kit_frames(index);
    if (cap < n)
        return 0;

    uint32_t rng = 2654435761u * (uint32_t)(index + 1);
    lp2_t hp, lp, mhp;
    lp2_set(&hp, r->hp > 0.0 ? r->hp : 1.0);
    lp2_set(&lp, r->lp > 0.0 ? r->lp : 1.0);
    lp2_set(&mhp, r->mhp > 0.0 ? r->mhp : 1.0);

    double body_phase = 0.0;
    double mphase[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    const double clap_gap = 0.011; /* seconds between clap bursts */

    for (int i = 0; i < n; i++) {
        const double t = (double)i / (double)DS_KIT_RATE;
        double s = 0.0;

        if (r->bamp > 0.0) {
            const double f = r->f1 + (r->f0 - r->f1) * exp(-t / r->pd);
            body_phase += TWO_PI * f / (double)DS_KIT_RATE;
            if (body_phase > TWO_PI)
                body_phase -= TWO_PI;
            s += r->bamp * sin(body_phase) * exp(-t / r->bd);
        }

        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        const double white = (double)(int32_t)rng / 2147483648.0;

        if (r->namp > 0.0 || r->mamp > 0.0) {
            /* Noise runs through the filters even when only the metal layer needs the high-pass state. */
            const double filtered = lp2_run(&lp, white) - lp2_run(&hp, white);
            if (r->namp > 0.0) {
                double env;
                if (r->bursts > 0) {
                    const double last = clap_gap * (double)(r->bursts - 1);
                    env = 0.0;
                    for (int k = 0; k < r->bursts - 1; k++) {
                        const double dt = t - clap_gap * (double)k;
                        if (dt >= 0.0 && exp(-dt / 0.004) > env)
                            env = exp(-dt / 0.004);
                    }
                    if (t >= last) {
                        const double tail = exp(-(t - last) / r->nd);
                        if (tail > env)
                            env = tail;
                    }
                } else {
                    env = exp(-t / r->nd);
                    if (r->nswell > 0.0)
                        env *= t < r->nswell ? t / r->nswell : 1.0;
                }
                s += r->namp * filtered * env;
            }
        }

        if (r->mamp > 0.0) {
            double m = 0.0;
            for (int k = 0; k < r->mcount; k++) {
                mphase[k] += r->mbase * METAL_RATIO[k] / (double)DS_KIT_RATE;
                if (mphase[k] >= 1.0)
                    mphase[k] -= 1.0;
                m += mphase[k] < 0.5 ? 1.0 : -1.0;
            }
            m /= (double)r->mcount;
            m -= lp2_run(&mhp, m);
            s += r->mamp * m * exp(-t / r->md);
        }

        if (r->click > 0.0 && t < 0.001)
            s += r->click * white * (1.0 - t / 0.001);

        if (r->drive > 1.0)
            s = tanh(s * r->drive) / tanh(r->drive);
        out[i] = (float)s;
    }

    /* End at zero: a raised-cosine fade over the last 8 ms. */
    int fade = (int)(0.008 * DS_KIT_RATE);
    if (fade > n)
        fade = n;
    for (int i = 0; i < fade; i++) {
        const double w = 0.5 + 0.5 * cos(3.14159265358979323846 * (double)(i + 1) / (double)fade);
        out[n - fade + i] *= (float)w;
    }
    /* The start is left alone on purpose: a drum hit should keep its transient. */

    float peak = 0.0f;
    for (int i = 0; i < n; i++)
        if (fabsf(out[i]) > peak)
            peak = fabsf(out[i]);
    if (peak > 1e-9f) {
        const float g = 0.9f / peak;
        for (int i = 0; i < n; i++)
            out[i] *= g;
    }
    return n;
}
