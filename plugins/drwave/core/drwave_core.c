#include "drwave_core.h"

#include <math.h>
#include <string.h>

#define TWO_PI 6.28318530717958647692
#define TWO_PI_F 6.28318530717958647692f
#define HALF_PI_F 1.57079632679489661923f

/* The control rate. Frequencies, wave position and fold are recomputed every CTRL samples and ramped linearly
 * between ticks, so nothing steps. */
#define CTRL 16

/* Voice states. */
enum { V_IDLE = 0, V_ATTACK = 1, V_DECAY = 2, V_RELEASE = 3 };

/* Highest harmonic stored in the full-bandwidth copy of a wave. Mip level L keeps MAXH >> L harmonics, so each
 * level is one octave of pitch narrower than the one before. */
#define MAXH 480
/* A level is used while its top harmonic stays under this share of the sample rate. */
#define BAND_LIMIT 0.4f

/* Share of an octave of pitch over which one band-limited level fades into the next. Level L still has no partial
 * above 0.4 * 2^MIP_FADE = 0.49 of the sample rate this far past its limit. */
#define MIP_FADE 0.3f

/* Every wave is scaled to this RMS, or less if its peak would pass 1.0. */
#define TABLE_RMS 0.30

/* Second oscillator detune, cents. It is what makes BLEND around the middle shimmer instead of just add up. */
#define B_DETUNE_CENTS 6.0f

/* MOTION: how far a note sweeps the wave position at full MOTION, in bank positions, and how long the sweep takes
 * to fall 60 dB. The sweep starts above the WAVE setting and settles onto it. */
#define MOTION_RANGE 4.0f
#define MOTION_SECONDS 1.2f

/* Mod wheel pushes the wave position up by this many bank positions at full travel. */
#define WHEEL_RANGE 1.5f

/* FOLD: the wave is multiplied by 1 + FOLD_DRIVE * fold^2 and run through a sine, so a larger setting wraps the
 * wave over itself more times, then mixed with the dry wave by the fold amount. The mixed result is scaled down by
 * 1 / (1 + FOLD_COMP * mix^2), which keeps the loudness about constant as the knob moves. */
#define FOLD_DRIVE 6.0f
#define FOLD_COMP 1.4f

/* Output stage. The voice sum is scaled by MASTER, then limited by a soft ceiling that is linear up to CEIL_T and
 * reaches CEIL_MAX, so the summed signal cannot pass CEIL_MAX before the level control. */
#define MASTER 0.8f
#define CEIL_T 0.7f
#define CEIL_MAX 0.97f

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float soft_ceiling(float x)
{
    const float a = fabsf(x);
    if (a <= CEIL_T)
        return x;
    const float k = CEIL_MAX - CEIL_T;
    float z = (a - CEIL_T) / k;
    /* Pade tanh, input clamped to the range where it is accurate. It reaches 1.0 at z = 3. */
    z = z > 3.0f ? 3.0f : z;
    const float z2 = z * z;
    const float t = z * (27.0f + z2) / (27.0f + 9.0f * z2);
    const float y = CEIL_T + k * t;
    return x < 0.0f ? -y : y;
}

/* ---- knob mappings ---- */

float dw_wave_position(float norm)
{
    return clampf(norm, 0.0f, 1.0f) * (float)(DW_WAVES - 1);
}

const char *dw_wave_name(int index)
{
    static const char *names[DW_WAVES] = {"sine", "triangle", "organ", "vowel", "saw", "square", "pulse", "digital"};
    return names[index < 0 ? 0 : (index >= DW_WAVES ? DW_WAVES - 1 : index)];
}

float dw_tone_hz(float norm)
{
    return 80.0f * powf(250.0f, clampf(norm, 0.0f, 1.0f));
}

float dw_level_gain(float norm)
{
    const float n = clampf(norm, 0.0f, 1.0f);
    return n * n;
}

void dw_default_params(dw_params *p)
{
    p->wave = DW_DEFAULT_WAVE;
    p->blend = DW_DEFAULT_BLEND;
    p->motion = DW_DEFAULT_MOTION;
    p->fold = DW_DEFAULT_FOLD;
    p->tone = DW_DEFAULT_TONE;
    p->attack = DW_DEFAULT_ATTACK;
    p->decay = DW_DEFAULT_DECAY;
    p->sustain = DW_DEFAULT_SUSTAIN;
    p->release = DW_DEFAULT_RELEASE;
    p->level = DW_DEFAULT_LEVEL;
    p->bend_range = DW_DEFAULT_BEND_RANGE;
}

static float one_pole_coef(float tau_seconds, float sr, float samples)
{
    return 1.0f - expf(-samples / (tau_seconds * sr));
}

void dw_set_params(dw_t *d, const dw_params *in)
{
    dw_params *t = &d->target;
    t->wave = clampf(in->wave, 0.0f, 1.0f);
    t->blend = clampf(in->blend, 0.0f, 1.0f);
    t->motion = clampf(in->motion, 0.0f, 1.0f);
    t->fold = clampf(in->fold, 0.0f, 1.0f);
    t->tone = clampf(in->tone, 0.0f, 1.0f);
    t->attack = clampf(in->attack, 0.0005f, 10.0f);
    t->decay = clampf(in->decay, 0.005f, 30.0f);
    t->sustain = clampf(in->sustain, 0.0f, 1.0f);
    t->release = clampf(in->release, 0.005f, 30.0f);
    t->level = clampf(in->level, 0.0f, 1.0f);
    t->bend_range = clampf(in->bend_range, 0.0f, 24.0f);

    d->att_inc = 1.0f / (t->attack * d->sr);
    /* exp(-6.9) is -60 dB: the time constants are set so that decay and release are times to fall 60 dB. */
    d->dec_c = expf(-6.9f / (t->decay * d->sr));
    d->rel_c = expf(-6.9f / (t->release * d->sr));
}

/* ---- wave bank ---- */

/* In-place complex FFT, forward (e^-i), power-of-two size DW_TABLE_SIZE. wc and ws hold cos and sin of 2*pi*k/N for
 * k < N/2. */
static void fft(double *re, double *im, const double *wc, const double *ws)
{
    const int n = DW_TABLE_SIZE;
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const int step = n / len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < len / 2; k++) {
                const double c = wc[k * step], s = -ws[k * step];
                const int u = i + k, v = i + k + len / 2;
                const double xr = re[v] * c - im[v] * s, xi = re[v] * s + im[v] * c;
                re[v] = re[u] - xr; im[v] = im[u] - xi;
                re[u] += xr; im[u] += xi;
            }
    }
}

/* The harmonic content of each wave: amp[h] * sin(h * theta + phi[h]) for h = 1..MAXH. Waves are ordered so that
 * neighbours sound like a sensible step apart. */
static void wave_spectrum(int w, double *amp, double *phi)
{
    for (int h = 0; h <= MAXH; h++) {
        amp[h] = 0.0;
        phi[h] = 0.0;
    }
    switch (w) {
    case 0: /* sine */
        amp[1] = 1.0;
        break;
    case 1: /* triangle: odd harmonics falling with 1/h^2, alternating sign */
        for (int h = 1; h <= MAXH; h += 2) {
            amp[h] = 1.0 / ((double)h * (double)h);
            phi[h] = (((h - 1) / 2) & 1) ? 3.14159265358979323846 : 0.0;
        }
        break;
    case 2: { /* organ: a few drawbar harmonics */
        static const int hs[6] = {1, 2, 3, 4, 6, 8};
        static const double as[6] = {1.0, 0.8, 0.6, 0.5, 0.35, 0.25};
        for (int i = 0; i < 6; i++)
            amp[hs[i]] = as[i];
        break;
    }
    case 3: /* vowel: harmonic series with three fixed bumps, like a sung vowel */
        for (int h = 1; h <= 60; h++) {
            const double x3 = ((double)h - 3.0) / 1.1, x8 = ((double)h - 8.0) / 1.6, x14 = ((double)h - 14.0) / 2.2;
            amp[h] = pow((double)h, -0.8) * (0.25 + exp(-x3 * x3) + 0.8 * exp(-x8 * x8) + 0.5 * exp(-x14 * x14));
        }
        break;
    case 4: /* saw */
        for (int h = 1; h <= MAXH; h++)
            amp[h] = 1.0 / (double)h;
        break;
    case 5: /* square */
        for (int h = 1; h <= MAXH; h += 2)
            amp[h] = 1.0 / (double)h;
        break;
    case 6: /* 25 percent pulse, cosine phase */
        for (int h = 1; h <= MAXH; h++) {
            const double v = sin(3.14159265358979323846 * 0.25 * (double)h) / (double)h;
            amp[h] = fabs(v);
            phi[h] = 1.57079632679489661923 + (v < 0.0 ? 3.14159265358979323846 : 0.0);
        }
        break;
    default: /* digital: 40 harmonics with a quadratic phase, a flat-topped buzzy wave */
        for (int h = 1; h <= 40; h++) {
            amp[h] = pow((double)h, -0.6);
            phi[h] = 0.37 * (double)h * (double)h;
        }
        break;
    }
}

static void build_tables(dw_t *d)
{
    double re[DW_TABLE_SIZE], im[DW_TABLE_SIZE];
    double wc[DW_TABLE_SIZE / 2], ws[DW_TABLE_SIZE / 2];
    double amp[MAXH + 1], phi[MAXH + 1];
    for (int k = 0; k < DW_TABLE_SIZE / 2; k++) {
        wc[k] = cos(TWO_PI * (double)k / (double)DW_TABLE_SIZE);
        ws[k] = sin(TWO_PI * (double)k / (double)DW_TABLE_SIZE);
    }

    for (int w = 0; w < DW_WAVES; w++) {
        wave_spectrum(w, amp, phi);
        double scale = 1.0;
        for (int lvl = 0; lvl < DW_MIPS; lvl++) {
            int top = MAXH >> lvl;
            if (top < 1)
                top = 1;
            memset(re, 0, sizeof re);
            memset(im, 0, sizeof im);
            /* sum of a*sin(h*theta + phi) as the real part of a forward FFT of the conjugated half spectrum */
            for (int h = 1; h <= top; h++) {
                const double a = 0.5 * amp[h];
                re[h] = a * sin(phi[h]);
                im[h] = a * cos(phi[h]);
                re[DW_TABLE_SIZE - h] = re[h];
                im[DW_TABLE_SIZE - h] = -im[h];
            }
            fft(re, im, wc, ws);
            if (lvl == 0) {
                double sum = 0.0, pk = 0.0;
                for (int i = 0; i < DW_TABLE_SIZE; i++) {
                    sum += re[i] * re[i];
                    if (fabs(re[i]) > pk)
                        pk = fabs(re[i]);
                }
                const double rms = sqrt(sum / (double)DW_TABLE_SIZE);
                scale = TABLE_RMS / rms;
                if (pk * scale > 1.0)
                    scale = 1.0 / pk;
            }
            /* One scale per wave, taken from the full-bandwidth copy, so the levels of a wave match in loudness. */
            for (int i = 0; i < DW_TABLE_SIZE; i++)
                d->table[w][lvl][i] = (float)(re[i] * scale);
            d->table[w][lvl][DW_TABLE_SIZE] = d->table[w][lvl][0];
        }
    }
    for (int i = 0; i <= DW_TABLE_SIZE; i++)
        d->sine[i] = (float)sin(TWO_PI * (double)i / (double)DW_TABLE_SIZE);
}

/* ---- init and reset ---- */

void dw_reset(dw_t *d)
{
    memset(d->voice, 0, sizeof d->voice);
    for (int i = 0; i < DW_VOICES; i++) {
        /* spread the voices across the stereo field, constant power, +-0.25 */
        const float pan = ((float)((i * 5) % DW_VOICES) - 3.5f) / 3.5f * 0.25f;
        const float ang = (pan + 1.0f) * 0.25f * TWO_PI_F * 0.5f;
        d->voice[i].pan_l = cosf(ang);
        d->voice[i].pan_r = sinf(ang);
    }
    const dw_params *t = &d->target;
    d->s_wave = t->wave;
    d->s_blend = t->blend;
    d->s_motion = t->motion;
    d->s_fold = t->fold;
    d->s_tone = t->tone;
    d->s_level = t->level;
    d->bend_norm = 0.0f;
    d->s_bend = 0.0f;
    d->mod_wheel = 0.0f;
    d->s_mod = 0.0f;
    d->pedal = 0;
    d->wa_cur = cosf(d->s_blend * HALF_PI_F);
    d->wb_cur = sinf(d->s_blend * HALF_PI_F);
    d->wa_d = 0.0f;
    d->wb_d = 0.0f;
    d->tone_cur = 1.0f;
    d->tone_d = 0.0f;
    d->level_cur = dw_level_gain(d->s_level);
    d->level_d = 0.0f;
    d->sweep_dec = 1.0f;
    d->tick_left = 0;
    d->act_peak = 0.0f;
}

void dw_init(dw_t *d, float sample_rate)
{
    memset(d, 0, sizeof *d);
    d->sr = clampf(sample_rate, 8000.0f, 384000.0f);
    build_tables(d);
    d->c_smooth = one_pole_coef(0.005f, d->sr, (float)CTRL);
    d->c_slew = one_pole_coef(0.002f, d->sr, (float)CTRL);
    dw_default_params(&d->target);
    dw_set_params(d, &d->target);
    dw_reset(d);
}

/* ---- notes ---- */

static dw_voice *find_voice(dw_t *d, int note)
{
    dw_voice *quiet = NULL, *oldest = NULL;
    for (int i = 0; i < DW_VOICES; i++) {
        dw_voice *v = &d->voice[i];
        if (v->state != V_IDLE && v->note == note)
            return v; /* same note again: retrigger the voice it already has */
    }
    for (int i = 0; i < DW_VOICES; i++)
        if (d->voice[i].state == V_IDLE)
            return &d->voice[i];
    /* Steal. A voice already in release goes first, the quietest of those; otherwise the oldest note. */
    for (int i = 0; i < DW_VOICES; i++) {
        dw_voice *v = &d->voice[i];
        if (v->state == V_RELEASE && (!quiet || v->env < quiet->env))
            quiet = v;
        if (!oldest || (unsigned)(d->serial - v->age) > (unsigned)(d->serial - oldest->age))
            oldest = v;
    }
    return quiet ? quiet : oldest;
}

void dw_note_on(dw_t *d, int note, int velocity)
{
    if (velocity <= 0) {
        dw_note_off(d, note);
        return;
    }
    note = note < 0 ? 0 : (note > 127 ? 127 : note);
    velocity = velocity > 127 ? 127 : velocity;

    dw_voice *v = find_voice(d, note);
    if (v->state == V_IDLE) {
        /* A fresh voice starts from silence with clean phases. A reused one keeps its envelope, phases and
         * wave position, and slides to the new note, so taking over a sounding voice does not step the output. */
        const float pl = v->pan_l, pr = v->pan_r;
        memset(v, 0, sizeof *v);
        v->pan_l = pl;
        v->pan_r = pr;
        v->fresh = 1;
    }
    v->note = note;
    v->vel = (float)velocity * (1.0f / 127.0f);
    v->held = 1;
    v->sustained = 0;
    v->state = V_ATTACK;
    v->sweep = 1.0f;
    v->age = ++d->serial;
    d->tick_left = 0;
}

void dw_note_off(dw_t *d, int note)
{
    for (int i = 0; i < DW_VOICES; i++) {
        dw_voice *v = &d->voice[i];
        if (v->state == V_IDLE || v->note != note || !v->held)
            continue;
        v->held = 0;
        if (d->pedal)
            v->sustained = 1;
        else
            v->state = V_RELEASE;
    }
}

void dw_pitch_bend(dw_t *d, float norm)
{
    d->bend_norm = clampf(norm, -1.0f, 1.0f);
}

void dw_mod_wheel(dw_t *d, float norm)
{
    d->mod_wheel = clampf(norm, 0.0f, 1.0f);
}

void dw_sustain_pedal(dw_t *d, int down)
{
    d->pedal = down ? 1 : 0;
    if (d->pedal)
        return;
    for (int i = 0; i < DW_VOICES; i++) {
        dw_voice *v = &d->voice[i];
        if (v->state != V_IDLE && v->sustained) {
            v->sustained = 0;
            v->state = V_RELEASE;
        }
    }
}

void dw_all_off(dw_t *d)
{
    d->pedal = 0;
    for (int i = 0; i < DW_VOICES; i++) {
        dw_voice *v = &d->voice[i];
        if (v->state == V_IDLE)
            continue;
        v->held = 0;
        v->sustained = 0;
        v->state = V_RELEASE;
    }
}

int dw_active_voices(const dw_t *d)
{
    int n = 0;
    for (int i = 0; i < DW_VOICES; i++)
        n += d->voice[i].state != V_IDLE;
    return n;
}

void dw_take_activity(dw_t *d, dw_activity *out)
{
    out->peak = d->act_peak;
    out->voices = dw_active_voices(d);
    out->notes = d->serial;
    d->act_peak = 0.0f;
}

/* ---- control tick ---- */

static void tick_begin(dw_t *d)
{
    const dw_params *t = &d->target;
    const float c = d->c_smooth;

    d->s_wave += c * (t->wave - d->s_wave);
    d->s_blend += c * (t->blend - d->s_blend);
    d->s_motion += c * (t->motion - d->s_motion);
    d->s_fold += c * (t->fold - d->s_fold);
    d->s_tone += c * (t->tone - d->s_tone);
    d->s_level += c * (t->level - d->s_level);
    d->s_bend += c * (d->bend_norm * t->bend_range - d->s_bend);
    d->s_mod += c * (d->mod_wheel - d->s_mod);

    const float inv = 1.0f / (float)CTRL;

    /* ramps for the controls every voice reads each sample */
    const float wa = cosf(d->s_blend * HALF_PI_F);
    const float wb = sinf(d->s_blend * HALF_PI_F);
    d->wa_d = (wa - d->wa_cur) * inv;
    d->wb_d = (wb - d->wb_cur) * inv;

    float tone_c;
    {
        const float fc = dw_tone_hz(d->s_tone);
        const float top = 0.45f * d->sr;
        /* At the top of the knob, or when the corner would pass 0.45 of the sample rate, the filter is open. */
        tone_c = (d->s_tone >= 0.999f || fc >= top) ? 1.0f : 1.0f - expf(-TWO_PI_F * fc / d->sr);
    }
    d->tone_d = (tone_c - d->tone_cur) * inv;
    d->level_d = (dw_level_gain(d->s_level) - d->level_cur) * inv;

    d->sweep_dec = expf(-6.9f * (float)CTRL / (MOTION_SECONDS * d->sr));

    const float wheel_pos = d->s_mod * WHEEL_RANGE;
    const float top_pos = (float)(DW_WAVES - 1);
    const float fold_k = d->s_fold;

    for (int i = 0; i < DW_VOICES; i++) {
        dw_voice *v = &d->voice[i];
        if (v->state == V_IDLE)
            continue;

        const float f0 = 440.0f * exp2f(((float)(v->note - 69) + d->s_bend) * (1.0f / 12.0f)) / d->sr;
        float incA = f0, incB = f0 * exp2f(B_DETUNE_CENTS / 1200.0f);
        /* A note too high to play stays under 0.45 of the sample rate rather than folding back. */
        incA = incA > 0.45f ? 0.45f : incA;
        incB = incB > 0.45f ? 0.45f : incB;
        v->inc[0] = incA;
        v->inc[1] = incB;

        /* Band limit: the mip level whose top harmonic stays under BAND_LIMIT of the sample rate for the higher of
         * the two oscillators. Fractional, the two neighbouring levels are crossfaded. */
        float mip = log2f(incB * ((float)MAXH / BAND_LIMIT));
        mip = clampf(mip, 0.0f, (float)(DW_MIPS - 1));
        {
            /* The level just below the safe one stays under Nyquist only for the first MIP_FADE of an octave past
             * its limit, so the crossfade between the two is squeezed into that stretch. The result is continuous
             * in pitch and never lets the wider level in above Nyquist. */
            const float base = floorf(mip);
            mip = base + fminf(1.0f, (mip - base) * (1.0f / MIP_FADE));
            mip = fminf(mip, (float)(DW_MIPS - 1));
        }

        /* Wave position: the WAVE knob, the sweep from MOTION, the mod wheel. Past the top end it reflects. */
        v->sweep *= d->sweep_dec;
        if (v->sweep < 1e-4f)
            v->sweep = 0.0f;
        float pos = d->s_wave * top_pos + wheel_pos + d->s_motion * MOTION_RANGE * v->sweep;
        if (pos > top_pos)
            pos = 2.0f * top_pos - pos;
        pos = clampf(pos, 0.0f, top_pos);

        /* The wavefolder is gentler on high notes, where it would otherwise alias: the amount falls to a half at
         * 2 kHz and a tenth at 18 kHz. */
        const float hz = f0 * d->sr;
        const float att = 1.0f / (1.0f + hz * (1.0f / 2000.0f));
        const float fgain = 1.0f + FOLD_DRIVE * fold_k * fold_k * att;
        const float fmix = fold_k * att;

        if (v->fresh) {
            v->pos_s = pos;
            v->pos_cur = pos;
            v->mip_cur = mip;
            v->fold_cur = fgain;
            v->mix_cur = fmix;
            v->gain_s = v->vel;
            v->gain_cur = v->vel;
            v->fresh = 0;
        } else {
            v->pos_s += d->c_slew * (pos - v->pos_s);
            v->gain_s += d->c_slew * (v->vel - v->gain_s);
        }
        v->pos_d = (v->pos_s - v->pos_cur) * inv;
        v->mip_d = (mip - v->mip_cur) * inv;
        v->fold_d = (fgain - v->fold_cur) * inv;
        v->mix_d = (fmix - v->mix_cur) * inv;
        v->gain_d = (v->gain_s - v->gain_cur) * inv;
    }
}

/* ---- voice ---- */

/* One oscillator: the two neighbouring waves around position pos, each read from the two neighbouring band-limited
 * levels, all four read at the same phase. */
static inline float osc_read(const dw_t *d, float pos, int lvl, float lvf, float ph)
{
    int w = (int)pos;
    if (w > DW_WAVES - 2)
        w = DW_WAVES - 2;
    const float wf = pos - (float)w;

    const float t = ph * (float)DW_TABLE_SIZE;
    int k = (int)t;
    const float fr = t - (float)k;
    k &= DW_TABLE_SIZE - 1;

    const float *a0 = d->table[w][lvl];
    const float *a1 = d->table[w][lvl + 1];
    const float *b0 = d->table[w + 1][lvl];
    const float *b1 = d->table[w + 1][lvl + 1];
    const float s_a0 = a0[k] + fr * (a0[k + 1] - a0[k]);
    const float s_a1 = a1[k] + fr * (a1[k + 1] - a1[k]);
    const float s_b0 = b0[k] + fr * (b0[k + 1] - b0[k]);
    const float s_b1 = b1[k] + fr * (b1[k + 1] - b1[k]);
    const float a = s_a0 + lvf * (s_a1 - s_a0);
    const float b = s_b0 + lvf * (s_b1 - s_b0);
    return a + wf * (b - a);
}

static inline float fold_sine(const float *tab, float x)
{
    /* sin(x) from the table; x in radians, any sign */
    float t = x * (float)(DW_TABLE_SIZE / TWO_PI);
    int k = (int)t;
    if (t < (float)k)
        k--;
    const float fr = t - (float)k;
    k &= DW_TABLE_SIZE - 1;
    return tab[k] + fr * (tab[k + 1] - tab[k]);
}

static void render_voice(dw_t *d, dw_voice *v, float *l, float *r, int n)
{
    const float sus = d->target.sustain;
    float env = v->env;
    int state = v->state;
    float gain = v->gain_cur;
    const float gd = v->gain_d;
    float pos = v->pos_cur, mip = v->mip_cur, fg = v->fold_cur, fm = v->mix_cur;
    const float posd = v->pos_d, mipd = v->mip_d, fgd = v->fold_d, fmd = v->mix_d;
    float phA = v->ph[0], phB = v->ph[1];
    const float incA = v->inc[0], incB = v->inc[1];
    float wa = d->wa_cur, wb = d->wb_cur, tc = d->tone_cur;
    const float wad = d->wa_d, wbd = d->wb_d, tcd = d->tone_d;
    float lp = v->lp;
    const float pl = v->pan_l, pr = v->pan_r;
    const float top_pos = (float)(DW_WAVES - 1);
    const int skip_b = (d->wb_cur == 0.0f && d->wb_d == 0.0f);

    for (int i = 0; i < n; i++) {
        switch (state) {
        case V_ATTACK:
            env += d->att_inc;
            if (env >= 1.0f) {
                env = 1.0f;
                state = V_DECAY;
            }
            break;
        case V_DECAY:
            /* Settles on the sustain level, and follows it if the knob moves. */
            env = sus + (env - sus) * d->dec_c;
            break;
        default: /* V_RELEASE */
            env *= d->rel_c;
            break;
        }
        if ((state == V_RELEASE || (state == V_DECAY && sus < 1e-4f)) && env < 1e-4f) {
            state = V_IDLE;
            env = 0.0f;
            break;
        }

        const float posc = pos < 0.0f ? 0.0f : (pos > top_pos ? top_pos : pos);
        int lvl = (int)mip;
        if (lvl > DW_MIPS - 2)
            lvl = DW_MIPS - 2;
        const float lvf = mip - (float)lvl;

        float x = wa * osc_read(d, posc, lvl, lvf, phA);
        if (!skip_b) {
            /* B reads the mirror image of the wave position: 7 - pos */
            x += wb * osc_read(d, top_pos - posc, lvl, lvf, phB);
        }

        if (fm > 1e-6f) {
            const float folded = fold_sine(d->sine, x * fg);
            x += fm * (folded - x);
            x *= 1.0f / (1.0f + FOLD_COMP * fm * fm);
        }

        lp += tc * (x - lp);
        if (fabsf(lp) < 1e-20f)
            lp = 0.0f;

        const float o = lp * env * gain;
        l[i] += o * pl;
        r[i] += o * pr;

        phA += incA;
        phB += incB;
        if (phA >= 1.0f) phA -= 1.0f;
        if (phB >= 1.0f) phB -= 1.0f;
        pos += posd;
        mip += mipd;
        fg += fgd;
        fm += fmd;
        gain += gd;
        wa += wad;
        wb += wbd;
        tc += tcd;
    }

    v->env = env;
    v->state = state;
    v->gain_cur = gain;
    v->pos_cur = pos;
    v->mip_cur = mip;
    v->fold_cur = fg;
    v->mix_cur = fm;
    v->ph[0] = phA;
    v->ph[1] = phB;
    v->lp = lp;
}

void dw_render(dw_t *d, float *l, float *r, int frames)
{
    int done = 0;
    while (done < frames) {
        if (d->tick_left <= 0) {
            tick_begin(d);
            d->tick_left = CTRL;
        }
        const int n = frames - done < d->tick_left ? frames - done : d->tick_left;
        float *pl = l + done, *pr = r + done;
        memset(pl, 0, sizeof(float) * (size_t)n);
        memset(pr, 0, sizeof(float) * (size_t)n);

        for (int v = 0; v < DW_VOICES; v++)
            if (d->voice[v].state != V_IDLE)
                render_voice(d, &d->voice[v], pl, pr, n);

        float lev = d->level_cur;
        float peak = d->act_peak;
        for (int i = 0; i < n; i++) {
            const float a = soft_ceiling(pl[i] * MASTER) * lev;
            const float b = soft_ceiling(pr[i] * MASTER) * lev;
            pl[i] = a;
            pr[i] = b;
            const float m = fabsf(a) > fabsf(b) ? fabsf(a) : fabsf(b);
            if (m > peak)
                peak = m;
            lev += d->level_d;
            /* The shared ramps step one sample at a time, the same additions the voices make on their copies,
             * so the result does not depend on how a block is split. */
            d->wa_cur += d->wa_d;
            d->wb_cur += d->wb_d;
            d->tone_cur += d->tone_d;
        }
        d->level_cur = lev;
        d->act_peak = peak;

        d->tick_left -= n;
        done += n;
    }
}
