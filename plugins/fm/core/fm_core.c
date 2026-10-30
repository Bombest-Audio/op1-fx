#include "fm_core.h"

#include <math.h>
#include <string.h>

#define TWO_PI_F 6.28318530717958647692f

/* The control rate. Frequencies, envelopes' targets and the modulation index are recomputed every CTRL
 * samples and ramped linearly between ticks, so nothing steps. */
#define CTRL 16

/* Voice states. */
enum { V_IDLE = 0, V_ATTACK = 1, V_DECAY = 2, V_RELEASE = 3 };

/* The modulation index never relaxes all the way to zero: this share of it stays for the whole note. */
#define MOD_FLOOR 0.25f

/* Output stage. The voice sum is scaled by MASTER, then limited by a soft ceiling that is linear up to
 * CEIL_T and reaches CEIL_MAX, so the summed signal cannot pass CEIL_MAX before the level control. */
#define MASTER 0.5f
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

/* Linear-interpolated sine from a table. x is in cycles and may be any sign. */
static inline float sine_lookup(const float *tab, float x)
{
    const float t = x * (float)FM_SINE_SIZE;
    int k = (int)t;
    if (t < (float)k)
        k--;
    const float fr = t - (float)k;
    k &= FM_SINE_SIZE - 1;
    const float a = tab[k];
    return a + fr * (tab[k + 1] - a);
}

float fm_ratio_value(float norm)
{
    const float n = clampf(norm, 0.0f, 1.0f);
    float raw = 0.5f * exp2f(5.0f * n);
    /* Pull toward the nearest whole number, harder as the ratio grows. A small leftover keeps a slow beat. */
    const float snap = clampf((raw - 1.0f) * 0.5f, 0.0f, 1.0f) * 0.75f;
    raw += snap * (floorf(raw + 0.5f) - raw);
    return raw;
}

float fm_index_value(float norm)
{
    return 16.0f * powf(clampf(norm, 0.0f, 1.0f), 1.5f);
}

float fm_feedback_value(float norm)
{
    return 2.0f * clampf(norm, 0.0f, 1.0f);
}

float fm_mod_decay_seconds(float norm)
{
    return 0.04f * powf(100.0f, clampf(norm, 0.0f, 1.0f));
}

float fm_level_gain(float norm)
{
    const float n = clampf(norm, 0.0f, 1.0f);
    return n * n;
}

void fm_default_params(fm_params *p)
{
    p->ratio = FM_DEFAULT_RATIO;
    p->index = FM_DEFAULT_INDEX;
    p->feedback = FM_DEFAULT_FEEDBACK;
    p->layer = FM_DEFAULT_LAYER;
    p->attack = FM_DEFAULT_ATTACK;
    p->decay = FM_DEFAULT_DECAY;
    p->sustain = FM_DEFAULT_SUSTAIN;
    p->release = FM_DEFAULT_RELEASE;
    p->level = FM_DEFAULT_LEVEL;
    p->mod_decay = FM_DEFAULT_MOD_DECAY;
    p->detune = FM_DEFAULT_DETUNE;
    p->vel_index = FM_DEFAULT_VEL_INDEX;
    p->key_track = FM_DEFAULT_KEY_TRACK;
    p->bend_range = FM_DEFAULT_BEND_RANGE;
}

static float one_pole_coef(float tau_seconds, float sr, float samples)
{
    return 1.0f - expf(-samples / (tau_seconds * sr));
}

void fm_set_params(fm_t *f, const fm_params *in)
{
    fm_params *t = &f->target;
    t->ratio = clampf(in->ratio, 0.0f, 1.0f);
    t->index = clampf(in->index, 0.0f, 1.0f);
    t->feedback = clampf(in->feedback, 0.0f, 1.0f);
    t->layer = clampf(in->layer, 0.0f, 1.0f);
    t->attack = clampf(in->attack, 0.0005f, 10.0f);
    t->decay = clampf(in->decay, 0.005f, 30.0f);
    t->sustain = clampf(in->sustain, 0.0f, 1.0f);
    t->release = clampf(in->release, 0.005f, 30.0f);
    t->level = clampf(in->level, 0.0f, 1.0f);
    t->mod_decay = clampf(in->mod_decay, 0.0f, 1.0f);
    t->detune = clampf(in->detune, -1.0f, 1.0f);
    t->vel_index = clampf(in->vel_index, 0.0f, 1.0f);
    t->key_track = clampf(in->key_track, 0.0f, 1.0f);
    t->bend_range = clampf(in->bend_range, 0.0f, 24.0f);

    f->att_inc = 1.0f / (t->attack * f->sr);
    /* exp(-6.9) is -60 dB: the time constants are set so that decay and release are times to fall 60 dB. */
    f->dec_c = expf(-6.9f / (t->decay * f->sr));
    f->rel_c = expf(-6.9f / (t->release * f->sr));
}

void fm_reset(fm_t *f)
{
    memset(f->voice, 0, sizeof f->voice);
    for (int i = 0; i < FM_VOICES; i++) {
        /* spread the voices across the stereo field, constant power, +-0.25 */
        const float pan = ((float)((i * 5) % FM_VOICES) - 3.5f) / 3.5f * 0.25f;
        const float ang = (pan + 1.0f) * 0.25f * TWO_PI_F * 0.5f;
        f->voice[i].pan_l = cosf(ang);
        f->voice[i].pan_r = sinf(ang);
    }
    const fm_params *t = &f->target;
    f->s_ratio = t->ratio;
    f->s_index = t->index;
    f->s_fb = t->feedback;
    f->s_layer = t->layer;
    f->s_level = t->level;
    f->bend_norm = 0.0f;
    f->s_bend = 0.0f;
    f->mod_wheel = 0.0f;
    f->s_mod = 0.0f;
    f->pedal = 0;
    f->fb_cur = fm_feedback_value(f->s_fb);
    f->fb_d = 0.0f;
    f->layer_cur = f->s_layer;
    f->layer_d = 0.0f;
    f->level_cur = fm_level_gain(f->s_level);
    f->level_d = 0.0f;
    f->mdec = 1.0f;
    f->tick_left = 0;
    f->act_peak = 0.0f;
}

void fm_init(fm_t *f, float sample_rate)
{
    memset(f, 0, sizeof *f);
    f->sr = clampf(sample_rate, 8000.0f, 384000.0f);
    for (int i = 0; i <= FM_SINE_SIZE; i++)
        f->sine[i] = (float)sin((double)i * (6.283185307179586 / (double)FM_SINE_SIZE));
    f->c_smooth = one_pole_coef(0.005f, f->sr, (float)CTRL);
    f->c_slew = one_pole_coef(0.002f, f->sr, (float)CTRL);
    fm_default_params(&f->target);
    fm_set_params(f, &f->target);
    fm_reset(f);
}

/* ---- notes ---- */

static fm_voice *find_voice(fm_t *f, int note)
{
    fm_voice *quiet = NULL, *oldest = NULL;
    for (int i = 0; i < FM_VOICES; i++) {
        fm_voice *v = &f->voice[i];
        if (v->state != V_IDLE && v->note == note)
            return v; /* same note again: retrigger the voice it already has */
    }
    for (int i = 0; i < FM_VOICES; i++)
        if (f->voice[i].state == V_IDLE)
            return &f->voice[i];
    /* Steal. A voice already in release goes first, the quietest of those; otherwise the oldest note. */
    for (int i = 0; i < FM_VOICES; i++) {
        fm_voice *v = &f->voice[i];
        if (v->state == V_RELEASE && (!quiet || v->env < quiet->env))
            quiet = v;
        if (!oldest || (unsigned)(f->serial - v->age) > (unsigned)(f->serial - oldest->age))
            oldest = v;
    }
    return quiet ? quiet : oldest;
}

void fm_note_on(fm_t *f, int note, int velocity)
{
    if (velocity <= 0) {
        fm_note_off(f, note);
        return;
    }
    note = note < 0 ? 0 : (note > 127 ? 127 : note);
    velocity = velocity > 127 ? 127 : velocity;

    fm_voice *v = find_voice(f, note);
    if (v->state == V_IDLE) {
        /* A fresh voice starts from silence with clean phases. A reused one keeps its envelope, phases and
         * index, and slides to the new note, so taking over a sounding voice does not step the output. */
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
    v->mx = 1.0f;
    v->age = ++f->serial;
    f->tick_left = 0;
}

void fm_note_off(fm_t *f, int note)
{
    for (int i = 0; i < FM_VOICES; i++) {
        fm_voice *v = &f->voice[i];
        if (v->state == V_IDLE || v->note != note || !v->held)
            continue;
        v->held = 0;
        if (f->pedal)
            v->sustained = 1;
        else
            v->state = V_RELEASE;
    }
}

void fm_pitch_bend(fm_t *f, float norm)
{
    f->bend_norm = clampf(norm, -1.0f, 1.0f);
}

void fm_mod_wheel(fm_t *f, float norm)
{
    f->mod_wheel = clampf(norm, 0.0f, 1.0f);
}

void fm_sustain_pedal(fm_t *f, int down)
{
    f->pedal = down ? 1 : 0;
    if (f->pedal)
        return;
    for (int i = 0; i < FM_VOICES; i++) {
        fm_voice *v = &f->voice[i];
        if (v->state != V_IDLE && v->sustained) {
            v->sustained = 0;
            v->state = V_RELEASE;
        }
    }
}

void fm_all_off(fm_t *f)
{
    f->pedal = 0;
    for (int i = 0; i < FM_VOICES; i++) {
        fm_voice *v = &f->voice[i];
        if (v->state == V_IDLE)
            continue;
        v->held = 0;
        v->sustained = 0;
        v->state = V_RELEASE;
    }
}

int fm_active_voices(const fm_t *f)
{
    int n = 0;
    for (int i = 0; i < FM_VOICES; i++)
        n += f->voice[i].state != V_IDLE;
    return n;
}

void fm_take_activity(fm_t *f, fm_activity *out)
{
    out->peak = f->act_peak;
    out->voices = fm_active_voices(f);
    out->notes = f->serial;
    f->act_peak = 0.0f;
}

/* ---- control tick ---- */

static void tick_begin(fm_t *f)
{
    const fm_params *t = &f->target;
    const float c = f->c_smooth;

    f->s_ratio += c * (t->ratio - f->s_ratio);
    f->s_index += c * (t->index - f->s_index);
    f->s_fb += c * (t->feedback - f->s_fb);
    f->s_layer += c * (t->layer - f->s_layer);
    f->s_level += c * (t->level - f->s_level);
    f->s_bend += c * (f->bend_norm * t->bend_range - f->s_bend);
    f->s_mod += c * (f->mod_wheel - f->s_mod);

    /* ramps for the controls the voices read every sample */
    const float inv = 1.0f / (float)CTRL;
    f->fb_d = (fm_feedback_value(f->s_fb) - f->fb_cur) * inv;
    f->layer_d = (f->s_layer - f->layer_cur) * inv;
    f->level_d = (fm_level_gain(f->s_level) - f->level_cur) * inv;

    /* The modulation envelope relaxes at the same rate whatever the sample rate. */
    const float md_sec = fm_mod_decay_seconds(t->mod_decay);
    f->mdec = expf(-6.9f * (float)CTRL / (md_sec * f->sr));

    const float ratio = fm_ratio_value(f->s_ratio);
    const float detune = exp2f(t->detune * 30.0f / 1200.0f);
    const float index = fm_index_value(f->s_index) * (1.0f + 2.0f * f->s_mod);

    for (int i = 0; i < FM_VOICES; i++) {
        fm_voice *v = &f->voice[i];
        if (v->state == V_IDLE)
            continue;

        const float f0 = 440.0f * exp2f(((float)(v->note - 69) + f->s_bend) * (1.0f / 12.0f)) / f->sr;
        float inc[4];
        inc[0] = f0;
        inc[1] = f0 * ratio;
        inc[2] = 2.0f * f0 * detune;
        inc[3] = inc[2] * ratio;

        /* Keep the top sideband under Nyquist: carrier + modulator * (index + 1) <= 0.5 sample rate.
         * A note too high for that plays as a plain sine instead of folding back. */
        float allowed[2];
        for (int p = 0; p < 2; p++) {
            const float room = 0.5f - inc[2 * p];
            allowed[p] = inc[2 * p + 1] > 1e-6f ? (room / inc[2 * p + 1]) - 1.0f : 1e6f;
            if (allowed[p] < 0.0f)
                allowed[p] = 0.0f;
        }
        v->b_weight = clampf((0.47f - inc[2]) / 0.04f, 0.0f, 1.0f);
        for (int k = 0; k < 4; k++)
            v->inc[k] = inc[k] > 0.49f ? 0.49f : inc[k];

        v->mx *= f->mdec;
        if (v->mx < 1e-6f)
            v->mx = 0.0f;
        const float menv = MOD_FLOOR + (1.0f - MOD_FLOOR) * v->mx;
        const float kt = clampf(exp2f(-t->key_track * 3.0f * (float)(v->note - 60) * (1.0f / 12.0f)), 0.1f, 2.0f);
        const float vs = 1.0f - t->vel_index + t->vel_index * v->vel;
        const float base = index * menv * kt * vs;

        float tgt[2];
        for (int p = 0; p < 2; p++)
            tgt[p] = base < allowed[p] ? base : allowed[p];

        if (v->fresh) {
            v->idx_s[0] = tgt[0];
            v->idx_s[1] = tgt[1];
            v->idx_cur[0] = tgt[0];
            v->idx_cur[1] = tgt[1];
            v->gain_s = v->vel;
            v->gain_cur = v->vel;
            v->fresh = 0;
        } else {
            v->idx_s[0] += f->c_slew * (tgt[0] - v->idx_s[0]);
            v->idx_s[1] += f->c_slew * (tgt[1] - v->idx_s[1]);
            v->gain_s += f->c_slew * (v->vel - v->gain_s);
        }
        v->idx_d[0] = (v->idx_s[0] - v->idx_cur[0]) * inv;
        v->idx_d[1] = (v->idx_s[1] - v->idx_cur[1]) * inv;
        v->gain_d = (v->gain_s - v->gain_cur) * inv;
    }
}

static void render_voice(fm_t *f, fm_voice *v, float *l, float *r, int n)
{
    const float *tab = f->sine;
    const float inv2pi = 1.0f / TWO_PI_F;
    const float sus = f->target.sustain;
    float env = v->env;
    int state = v->state;
    float idxA = v->idx_cur[0], idxB = v->idx_cur[1], gain = v->gain_cur;
    const float idxAd = v->idx_d[0], idxBd = v->idx_d[1], gd = v->gain_d;
    float fb = f->fb_cur, lay = f->layer_cur;
    const float fbd = f->fb_d, layd = f->layer_d;
    float phCa = v->ph[0], phMa = v->ph[1], phCb = v->ph[2], phMb = v->ph[3];
    const float incCa = v->inc[0], incMa = v->inc[1], incCb = v->inc[2], incMb = v->inc[3];
    float ha1 = v->fb_hist[0][0], ha2 = v->fb_hist[0][1];
    float hb1 = v->fb_hist[1][0], hb2 = v->fb_hist[1][1];
    const float bw = v->b_weight;
    const float pl = v->pan_l, pr = v->pan_r;

    int i = 0;
    for (; i < n; i++) {
        switch (state) {
        case V_ATTACK:
            env += f->att_inc;
            if (env >= 1.0f) {
                env = 1.0f;
                state = V_DECAY;
            }
            break;
        case V_DECAY:
            /* Settles on the sustain level, and follows it if the knob moves. */
            env = sus + (env - sus) * f->dec_c;
            break;
        default: /* V_RELEASE */
            env *= f->rel_c;
            break;
        }
        if ((state == V_RELEASE || (state == V_DECAY && sus < 1e-4f)) && env < 1e-4f) {
            state = V_IDLE;
            env = 0.0f;
            break;
        }

        const float fbc = fb * inv2pi * 0.5f;
        const float ma = sine_lookup(tab, phMa + fbc * (ha1 + ha2));
        ha2 = ha1;
        ha1 = ma;
        const float ca = sine_lookup(tab, phCa + idxA * inv2pi * ma);
        const float mb = sine_lookup(tab, phMb + fbc * (hb1 + hb2));
        hb2 = hb1;
        hb1 = mb;
        const float cb = sine_lookup(tab, phCb + idxB * inv2pi * mb);

        const float lb = lay * bw;
        const float s = (ca + lb * cb) / (1.0f + lb);
        const float o = s * env * gain;
        l[i] += o * pl;
        r[i] += o * pr;

        phCa += incCa;
        phMa += incMa;
        phCb += incCb;
        phMb += incMb;
        if (phCa >= 1.0f) phCa -= 1.0f;
        if (phMa >= 1.0f) phMa -= 1.0f;
        if (phCb >= 1.0f) phCb -= 1.0f;
        if (phMb >= 1.0f) phMb -= 1.0f;
        idxA += idxAd;
        idxB += idxBd;
        gain += gd;
        fb += fbd;
        lay += layd;
    }

    v->env = env;
    v->state = state;
    v->idx_cur[0] = idxA;
    v->idx_cur[1] = idxB;
    v->gain_cur = gain;
    v->ph[0] = phCa;
    v->ph[1] = phMa;
    v->ph[2] = phCb;
    v->ph[3] = phMb;
    v->fb_hist[0][0] = ha1;
    v->fb_hist[0][1] = ha2;
    v->fb_hist[1][0] = hb1;
    v->fb_hist[1][1] = hb2;
}

void fm_render(fm_t *f, float *l, float *r, int frames)
{
    int done = 0;
    while (done < frames) {
        if (f->tick_left <= 0) {
            tick_begin(f);
            f->tick_left = CTRL;
        }
        const int n = frames - done < f->tick_left ? frames - done : f->tick_left;
        float *pl = l + done, *pr = r + done;
        memset(pl, 0, sizeof(float) * (size_t)n);
        memset(pr, 0, sizeof(float) * (size_t)n);

        for (int v = 0; v < FM_VOICES; v++)
            if (f->voice[v].state != V_IDLE)
                render_voice(f, &f->voice[v], pl, pr, n);

        float lev = f->level_cur;
        float peak = f->act_peak;
        for (int i = 0; i < n; i++) {
            const float a = soft_ceiling(pl[i] * MASTER) * lev;
            const float b = soft_ceiling(pr[i] * MASTER) * lev;
            pl[i] = a;
            pr[i] = b;
            const float m = fabsf(a) > fabsf(b) ? fabsf(a) : fabsf(b);
            if (m > peak)
                peak = m;
            lev += f->level_d;
            f->fb_cur += f->fb_d;
            f->layer_cur += f->layer_d;
        }
        f->level_cur = lev;
        f->act_peak = peak;

        f->tick_left -= n;
        done += n;
    }
}
