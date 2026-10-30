#include "dbox_core.h"

#include <math.h>
#include <string.h>

#define TICK 8
#define TWO_PI 6.28318530718f

typedef struct {
    float freq, sweep, sweep_s, osc_gain, noise_gain, noise_hp, dscale, nscale, pan;
    int group;
} key_def;

/* My own kit layout: four kicks, two snares, rim, clap, four toms, four hats, two cymbals, four percussion, a shaker
 * and a zap. sweep is a pitch drop in semitones at the start of the hit (negative rises). */
static const key_def KEYS[DBOX_KEYS] = {
    {48, 30, 0.035f, 1.0f, 0.02f, 800, 3.0f, 0.1f, 0.0f, 0},     /* 0 kick deep */
    {62, 24, 0.025f, 1.0f, 0.03f, 800, 2.2f, 0.1f, 0.0f, 0},     /* 1 kick punch */
    {55, 14, 0.050f, 1.0f, 0.00f, 800, 2.8f, 0.1f, 0.0f, 0},     /* 2 kick soft */
    {70, 36, 0.015f, 1.0f, 0.10f, 3000, 1.4f, 0.2f, 0.0f, 0},    /* 3 kick click */
    {190, 8, 0.015f, 0.6f, 0.60f, 1500, 1.5f, 1.0f, -0.1f, 0},   /* 4 snare 1 */
    {220, 8, 0.015f, 0.5f, 0.75f, 1800, 1.8f, 1.0f, 0.1f, 0},    /* 5 snare 2 */
    {420, 5, 0.010f, 0.7f, 0.30f, 2500, 0.5f, 0.8f, 0.2f, 0},    /* 6 rim */
    {300, 0, 0.010f, 0.0f, 1.00f, 1000, 1.4f, 1.0f, 0.0f, 0},    /* 7 clap */
    {90, 12, 0.060f, 1.0f, 0.00f, 800, 2.5f, 0.1f, -0.4f, 0},    /* 8 tom 1 */
    {120, 12, 0.055f, 1.0f, 0.00f, 800, 2.4f, 0.1f, -0.2f, 0},   /* 9 tom 2 */
    {160, 10, 0.050f, 1.0f, 0.00f, 800, 2.2f, 0.1f, 0.1f, 0},    /* 10 tom 3 */
    {210, 10, 0.045f, 1.0f, 0.00f, 800, 2.0f, 0.1f, 0.3f, 0},    /* 11 tom 4 */
    {330, 0, 0.010f, 0.15f, 0.85f, 6000, 0.5f, 1.0f, 0.3f, 1},   /* 12 hat closed 1 */
    {360, 0, 0.010f, 0.15f, 0.85f, 7500, 0.4f, 1.0f, 0.3f, 1},   /* 13 hat closed 2 */
    {340, 0, 0.010f, 0.15f, 0.80f, 6500, 0.3f, 1.0f, 0.3f, 1},   /* 14 hat pedal */
    {335, 0, 0.010f, 0.15f, 0.85f, 6000, 4.0f, 1.0f, 0.3f, 1},   /* 15 hat open */
    {400, 0, 0.010f, 0.10f, 0.80f, 5000, 6.0f, 1.0f, -0.3f, 2},  /* 16 crash */
    {440, 0, 0.010f, 0.30f, 0.50f, 7000, 5.0f, 1.0f, 0.4f, 2},   /* 17 ride */
    {540, 0, 0.010f, 0.9f, 0.00f, 800, 1.2f, 0.5f, 0.2f, 0},     /* 18 cowbell */
    {880, 0, 0.010f, 1.0f, 0.00f, 800, 0.35f, 0.5f, -0.2f, 0},   /* 19 clave */
    {200, 5, 0.040f, 1.0f, 0.05f, 800, 1.6f, 0.3f, -0.3f, 0},    /* 20 conga low */
    {310, 5, 0.035f, 1.0f, 0.05f, 800, 1.3f, 0.3f, 0.3f, 0},     /* 21 conga high */
    {500, 0, 0.010f, 0.0f, 0.90f, 8000, 0.5f, 1.0f, 0.4f, 0},    /* 22 shaker */
    {800, -30, 0.120f, 0.9f, 0.05f, 800, 0.8f, 0.3f, 0.0f, 0},   /* 23 zap */
};

enum { K_ADD, K_RING, K_FM, K_NOISE };
static const struct { int kind; float ratio; const char *name; } MODES[DBOX_MODES] = {
    {K_ADD, 1.0f, "LAYER"}, {K_ADD, 2.0f, "OCTAVE"}, {K_ADD, 1.5f, "FIFTH"}, {K_ADD, 0.5f, "SUB"},
    {K_RING, 1.414f, "RING"}, {K_FM, 1.0f, "FM"}, {K_FM, 3.5f, "FM HARD"}, {K_NOISE, 1.0f, "NOISE"},
};

static float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

float dbox_pitch_semitones(float n) { return 12.0f * clampf(n, -1.0f, 1.0f); }

float dbox_decay_seconds(float n)
{
    n = clampf(n, 0.0f, 0.8f);
    return 0.05f * powf(50.0f, n / 0.8f);
}

float dbox_attack_seconds(float n)
{
    if (n <= 0.8f)
        return 0.0f;
    const float t = clampf((n - 0.8f) / 0.2f, 0.0f, 1.0f);
    return 0.08f * t * t;
}

float dbox_filter_hz(float n)
{
    n = clampf(n, 0.0f, 1.0f);
    return 20000.0f * powf(60.0f / 20000.0f, powf(n, 0.8f));
}

const char *dbox_mode_name(int m) { return MODES[m < 0 ? 0 : (m >= DBOX_MODES ? DBOX_MODES - 1 : m)].name; }

int dbox_mode_from_norm(float n)
{
    int m = (int)floorf(clampf(n, 0.0f, 1.0f) * (float)DBOX_MODES);
    return m >= DBOX_MODES ? DBOX_MODES - 1 : m;
}

float dbox_key_freq(int key) { return KEYS[key < 0 ? 0 : (key >= DBOX_KEYS ? DBOX_KEYS - 1 : key)].freq; }

int dbox_key_for_note(const dbox_params *p, int note)
{
    int k = (note - p->root) % DBOX_KEYS;
    return k < 0 ? k + DBOX_KEYS : k;
}

void dbox_default_params(dbox_params *p)
{
    p->pitch = DBOX_DEFAULT_PITCH;
    p->wave = DBOX_DEFAULT_WAVE;
    p->decay = DBOX_DEFAULT_DECAY;
    p->mode = DBOX_DEFAULT_MODE;
    p->mix2 = DBOX_DEFAULT_MIX2;
    p->wave2 = DBOX_DEFAULT_WAVE2;
    p->decay2 = DBOX_DEFAULT_DECAY2;
    p->filter = DBOX_DEFAULT_FILTER;
    p->attack = DBOX_DEFAULT_ATTACK;
    p->env_decay = DBOX_DEFAULT_ENV_DECAY;
    p->sustain = DBOX_DEFAULT_SUSTAIN;
    p->release = DBOX_DEFAULT_RELEASE;
    p->level = DBOX_DEFAULT_LEVEL;
    p->bend_range = DBOX_DEFAULT_BEND_RANGE;
    p->root = DBOX_DEFAULT_ROOT;
}

static float coef60(float seconds, float sr)
{
    return powf(0.001f, 1.0f / (fmaxf(seconds, 0.0005f) * sr));
}

static void snap_smoothed(dbox_t *d)
{
    const dbox_params *p = &d->target;
    d->s_pitch = p->pitch; d->s_wave = p->wave; d->s_wave2 = p->wave2; d->s_mix2 = p->mix2;
    d->s_filter = p->filter; d->s_level = p->level; d->s_bend = d->bend_norm * p->bend_range;
}

void dbox_set_params(dbox_t *d, const dbox_params *p)
{
    d->target = *p;
    d->att_inc_gate = 1.0f / (fmaxf(p->attack, 0.0005f) * d->sr);
    d->dec_c = coef60(p->env_decay, d->sr);
    d->rel_c = coef60(p->release, d->sr);
}

void dbox_init(dbox_t *d, float sr)
{
    memset(d, 0, sizeof *d);
    d->sr = sr;
    d->smooth = 1.0f - expf(-1.0f / (0.005f * sr));
    for (int i = 0; i <= DBOX_SINE_SIZE; i++)
        d->sine[i] = sinf(TWO_PI * (float)i / (float)DBOX_SINE_SIZE);
    dbox_params p;
    dbox_default_params(&p);
    dbox_set_params(d, &p);
    dbox_reset(d);
}

void dbox_reset(dbox_t *d)
{
    memset(d->voice, 0, sizeof d->voice);
    d->bend_norm = 0.0f;
    d->mod_wheel = 0.0f;
    d->pedal = 0;
    d->serial = 0;
    d->rng = 22222u;
    d->notes = 0;
    d->key_mask = 0;
    d->act_peak = 0.0f;
    d->tick_left = 0;
    snap_smoothed(d);
}

static float noise(dbox_t *d)
{
    d->rng = d->rng * 1664525u + 1013904223u;
    return (float)(d->rng >> 8) * (1.0f / 8388608.0f) - 1.0f;
}

static float wave_at(const dbox_t *d, float ph, float inc, float w, int naive)
{
    const float gs = fmaxf(0.0f, 1.0f - 2.0f * w);
    const float gq = 1.0f - fabsf(2.0f * w - 1.0f);
    const float gw = fmaxf(0.0f, 2.0f * w - 1.0f);
    float out = 0.0f;
    if (gs > 0.0f) {
        const float x = ph * (float)DBOX_SINE_SIZE;
        const int i = (int)x;
        const float f = x - (float)i;
        out += gs * (d->sine[i] + (d->sine[i + 1] - d->sine[i]) * f);
    }
    if (gq > 0.0f || gw > 0.0f) {
        float sq = ph < 0.5f ? 1.0f : -1.0f;
        float sw = 2.0f * ph - 1.0f;
        if (!naive) {
            float t2 = ph + 0.5f;
            t2 -= t2 >= 1.0f ? 1.0f : 0.0f;
            #define BLEP(t, r) ((t) < inc ? ((t) / inc) * 2.0f - ((t) / inc) * ((t) / inc) - 1.0f \
                : ((t) > 1.0f - inc ? (((t) - 1.0f) / inc) * (((t) - 1.0f) / inc) + 2.0f * (((t) - 1.0f) / inc) + 1.0f : 0.0f))
            sq += BLEP(ph, 0) - BLEP(t2, 0);
            sw -= BLEP(ph, 0);
            #undef BLEP
        }
        out += 0.6f * (gq * sq + gw * sw);
    }
    return out;
}

static void voice_tick(dbox_t *d, dbox_voice *v)
{
    const key_def *k = &KEYS[v->key];
    v->pe *= v->pe_c;
    float semis = k->sweep * v->pe * (0.5f + 0.5f * v->vel) + dbox_pitch_semitones(d->s_pitch) + d->s_bend;
    semis = clampf(semis, -60.0f, 60.0f);
    const float f1 = k->freq * exp2f(semis / 12.0f);
    v->inc1 = clampf(f1 / d->sr, 0.0f, 0.45f);
    v->inc2 = clampf(f1 * MODES[v->ratio_idx].ratio / d->sr, 0.0f, 0.45f);
    if (fabsf(v->ic1) < 1e-20f) v->ic1 = 0.0f;
    if (fabsf(v->ic2) < 1e-20f) v->ic2 = 0.0f;
}

static void global_tick(dbox_t *d)
{
    const float fn = clampf(d->s_filter + 0.6f * d->mod_wheel, 0.0f, 1.0f);
    const float fc = clampf(dbox_filter_hz(fn), 20.0f, 0.45f * d->sr);
    const float g = tanf(3.14159265f * fc / d->sr);
    const float k = 1.0f / 0.9f;
    d->filt_g = g;
    d->filt_a1 = 1.0f / (1.0f + g * (g + k));
    d->filt_a2 = g * d->filt_a1;
    d->filt_a3 = g * d->filt_a2;
}

static void voice_start(dbox_t *d, dbox_voice *v, int note, int key, int vel)
{
    const dbox_params *p = &d->target;
    const key_def *k = &KEYS[key];
    const int mode = p->mode < 0 ? 0 : (p->mode >= DBOX_MODES ? DBOX_MODES - 1 : p->mode);
    memset(v, 0, sizeof *v);
    v->state = 1;
    v->key = key;
    v->note = note;
    v->held = 1;
    v->age = ++d->serial;
    v->vel = (float)vel / 127.0f;
    v->gate = 0;
    v->g = 0.0f;
    v->e1 = v->e2 = v->en = 1.0f;
    const float t1 = dbox_decay_seconds(p->decay) * k->dscale;
    v->c1 = coef60(t1, d->sr);
    v->c2 = coef60(dbox_decay_seconds(p->decay2) * k->dscale, d->sr);
    v->cn = coef60(t1 * k->nscale, d->sr);
    const float att = dbox_attack_seconds(p->decay);
    v->att = att > 0.0f ? 0.0f : 1.0f;
    v->att_inc = att > 0.0f ? 1.0f / (att * d->sr) : 0.0f;
    v->pe = 1.0f;
    v->pe_c = expf(-(float)TICK / (k->sweep_s * d->sr));
    v->nz_a = 1.0f - expf(-TWO_PI * k->noise_hp / d->sr);
    v->kind = MODES[mode].kind;
    v->ratio_idx = mode;
    v->fade = 1.0f;
    const float ang = (k->pan + 1.0f) * 0.7853982f;
    v->pan_l = cosf(ang);
    v->pan_r = sinf(ang);
    v->pe *= 1.0f;
    d->notes++;
    d->key_mask |= 1u << key;
    voice_tick(d, v);
}

static void voice_release(dbox_voice *v)
{
    v->held = 0;
    v->sustained = 0;
    v->gate = 3;
}

void dbox_note_on(dbox_t *d, int note, int vel)
{
    if (vel <= 0) {
        dbox_note_off(d, note);
        return;
    }
    if (vel > 127) vel = 127;
    const int key = dbox_key_for_note(&d->target, note);
    const int grp = KEYS[key].group;
    if (grp)
        for (int i = 0; i < DBOX_VOICES; i++) {
            dbox_voice *v = &d->voice[i];
            if (v->state && v->key != key && KEYS[v->key].group == grp)
                v->choked = 1;
        }
    int slot = -1;
    for (int i = 0; i < DBOX_VOICES; i++)
        if (d->voice[i].state && d->voice[i].key == key && !d->voice[i].pending) { slot = i; break; }
    if (slot < 0)
        for (int i = 0; i < DBOX_VOICES; i++)
            if (!d->voice[i].state) { slot = i; break; }
    if (slot < 0) {
        float best = 1e30f;
        for (int i = 0; i < DBOX_VOICES; i++) {
            const dbox_voice *v = &d->voice[i];
            float lvl = fmaxf(fmaxf(v->e1, v->e2), v->en) * v->g * v->vel + (v->pending ? 10.0f : 0.0f);
            if (lvl < best) { best = lvl; slot = i; }
        }
    }
    dbox_voice *v = &d->voice[slot];
    if (!v->state) {
        voice_start(d, v, note, key, vel);
    } else {
        v->pending = 1;
        v->p_note = note;
        v->p_key = key;
        v->p_vel = vel;
    }
}

void dbox_note_off(dbox_t *d, int note)
{
    for (int i = 0; i < DBOX_VOICES; i++) {
        dbox_voice *v = &d->voice[i];
        if (v->state && v->held && v->note == note) {
            if (d->pedal) v->sustained = 1;
            else voice_release(v);
            v->held = 0;
        }
    }
}

void dbox_pitch_bend(dbox_t *d, float norm) { d->bend_norm = clampf(norm, -1.0f, 1.0f); }
void dbox_mod_wheel(dbox_t *d, float norm) { d->mod_wheel = clampf(norm, 0.0f, 1.0f); }

void dbox_sustain_pedal(dbox_t *d, int down)
{
    d->pedal = down != 0;
    if (!down)
        for (int i = 0; i < DBOX_VOICES; i++)
            if (d->voice[i].state && d->voice[i].sustained)
                voice_release(&d->voice[i]);
}

void dbox_all_off(dbox_t *d)
{
    d->pedal = 0;
    for (int i = 0; i < DBOX_VOICES; i++)
        if (d->voice[i].state)
            voice_release(&d->voice[i]);
}

int dbox_active_voices(const dbox_t *d)
{
    int n = 0;
    for (int i = 0; i < DBOX_VOICES; i++)
        n += d->voice[i].state != 0;
    return n;
}

void dbox_take_activity(dbox_t *d, dbox_activity *out)
{
    out->peak = d->act_peak;
    out->voices = dbox_active_voices(d);
    out->notes = d->notes;
    out->keys = d->key_mask;
    d->act_peak = 0.0f;
    d->key_mask = 0;
}

static float soft_clip(float x)
{
    const float a = fabsf(x);
    if (a <= 0.75f)
        return x;
    const float y = 0.75f + 0.24f * tanhf((a - 0.75f) / 0.24f);
    return x < 0.0f ? -y : y;
}

void dbox_render(dbox_t *d, float *l, float *r, int frames)
{
    const dbox_params *p = &d->target;
    const float fade_dec = 1.0f / (0.001f * d->sr);
    const float choke_c = coef60(0.01f, d->sr);
    for (int n = 0; n < frames; n++) {
        if (d->tick_left <= 0) {
            global_tick(d);
            for (int i = 0; i < DBOX_VOICES; i++)
                if (d->voice[i].state)
                    voice_tick(d, &d->voice[i]);
            d->tick_left = TICK;
        }
        d->tick_left--;
        d->s_pitch += (p->pitch - d->s_pitch) * d->smooth;
        d->s_wave += (p->wave - d->s_wave) * d->smooth;
        d->s_wave2 += (p->wave2 - d->s_wave2) * d->smooth;
        d->s_mix2 += (p->mix2 - d->s_mix2) * d->smooth;
        d->s_filter += (p->filter - d->s_filter) * d->smooth;
        d->s_level += (p->level - d->s_level) * d->smooth;
        d->s_bend += (d->bend_norm * p->bend_range - d->s_bend) * d->smooth;

        float sl = 0.0f, sr_ = 0.0f;
        for (int i = 0; i < DBOX_VOICES; i++) {
            dbox_voice *v = &d->voice[i];
            if (!v->state)
                continue;
            const key_def *k = &KEYS[v->key];
            const int naive = v->kind == K_FM;
            const float m = d->s_mix2;
            float osc;
            const float o2_raw = wave_at(d, v->ph2, v->inc2, d->s_wave2, naive);
            const float e1 = v->e1 * v->att, e2 = v->e2 * v->att;
            if (v->kind == K_FM) {
                float pm = v->ph1 + (6.0f * m / TWO_PI) * o2_raw * e2;
                pm -= floorf(pm);
                osc = e1 * wave_at(d, pm, v->inc1, d->s_wave, 1);
            } else {
                const float o1 = wave_at(d, v->ph1, v->inc1, d->s_wave, 0);
                if (v->kind == K_RING)
                    osc = e1 * o1 * (1.0f - m + 2.0f * m * e2 * o2_raw);
                else if (v->kind == K_NOISE)
                    osc = e1 * o1 + m * e2 * noise(d) * 0.5f;
                else
                    osc = e1 * o1 + m * e2 * o2_raw;
            }
            float x = k->osc_gain * osc;
            if (k->noise_gain > 0.0f) {
                const float nz = noise(d);
                v->nz_lp += v->nz_a * (nz - v->nz_lp);
                x += 0.6f * k->noise_gain * v->en * v->att * (nz - v->nz_lp);
            }
            /* low-pass */
            const float v3 = x - v->ic2;
            const float v1 = d->filt_a1 * v->ic1 + d->filt_a2 * v3;
            const float v2 = v->ic2 + d->filt_a2 * v->ic1 + d->filt_a3 * v3;
            v->ic1 = 2.0f * v1 - v->ic1;
            v->ic2 = 2.0f * v2 - v->ic2;
            x = v2 * v->g * v->vel * 0.8f * v->fade;

            sl += x * v->pan_l;
            sr_ += x * v->pan_r;

            /* advance */
            v->ph1 += v->inc1; v->ph1 -= v->ph1 >= 1.0f ? 1.0f : 0.0f;
            v->ph2 += v->inc2; v->ph2 -= v->ph2 >= 1.0f ? 1.0f : 0.0f;
            v->e1 *= v->c1; v->e2 *= v->c2; v->en *= v->cn;
            if (v->att < 1.0f) { v->att += v->att_inc; if (v->att > 1.0f) v->att = 1.0f; }
            switch (v->choked ? 3 : v->gate) {
            case 0:
                v->g += d->att_inc_gate;
                if (v->g >= 1.0f) { v->g = 1.0f; v->gate = 1; }
                break;
            case 1:
                v->g = p->sustain + (v->g - p->sustain) * d->dec_c;
                if (fabsf(v->g - p->sustain) < 1e-4f) { v->g = p->sustain; v->gate = 2; }
                break;
            case 2:
                v->g = p->sustain;
                break;
            default:
                v->g *= v->choked ? choke_c : d->rel_c;
                break;
            }
            if (v->pending) {
                v->fade -= fade_dec;
                if (v->fade <= 0.0f) {
                    const int pn = v->p_note, pk = v->p_key, pv = v->p_vel;
                    voice_start(d, v, pn, pk, pv);
                    continue;
                }
            }
            if (!v->pending && ((v->e1 < 1e-4f && v->e2 < 1e-4f && v->en < 1e-4f) ||
                                (v->gate == 3 && v->g < 1e-4f) || !(v->g == v->g)))
                memset(v, 0, sizeof *v);
        }
        const float lv = d->s_level * d->s_level;
        const float ol = soft_clip(sl * lv), or_ = soft_clip(sr_ * lv);
        l[n] = ol;
        r[n] = or_;
        d->act_peak = fmaxf(d->act_peak, fmaxf(fabsf(ol), fabsf(or_)));
    }
}
