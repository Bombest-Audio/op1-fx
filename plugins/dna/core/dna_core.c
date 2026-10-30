#include "dna_core.h"

#include <math.h>
#include <string.h>

#define TWO_PI 6.283185307179586

/* Control rate: pitch, smoothed knobs and the wave are refreshed every CTRL samples. Pitch and level are
 * ramped linearly between ticks, so nothing steps. */
#define CTRL 32

enum { V_IDLE = 0, V_ATTACK = 1, V_DECAY = 2, V_RELEASE = 3 };

/* Output stage: the voice sum is scaled by MASTER, then limited by a soft ceiling that is linear up to
 * CEIL_T and reaches CEIL_MAX, so the output cannot pass CEIL_MAX before the level control (at most 1). */
#define MASTER 0.45f
#define CEIL_T 0.7f
#define CEIL_MAX 0.97f

/* MUTATE: a bit changes over a window of this many knob units around its own threshold. */
#define MUTATE_RAMP 6.0
/* Grit never redraws a voice's bit errors more often than this many samples apart. */
#define GRIT_MIN_GAP 64
#define SQUARE_BIAS 0.15
#define VIBRATO_HZ 5.5f
#define VIBRATO_SEMIS 0.6f

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline unsigned xorshift(unsigned *s)
{
    unsigned x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
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

float dna_bits_value(float norm)
{
    return 2.0f + 126.0f * clampf(norm, 0.0f, 1.0f);
}

float dna_time_seconds(float norm)
{
    return 0.001f * powf(5000.0f, clampf(norm, 0.0f, 1.0f));
}

float dna_time_norm(float seconds)
{
    const float s = clampf(seconds, 0.001f, 5.0f);
    return clampf(logf(s / 0.001f) / logf(5000.0f), 0.0f, 1.0f);
}

void dna_default_params(dna_params *p)
{
    p->mutate = DNA_DEFAULT_MUTATE;
    p->bits = DNA_DEFAULT_BITS;
    p->sub = DNA_DEFAULT_SUB;
    p->grit = DNA_DEFAULT_GRIT;
    p->attack = DNA_DEFAULT_ATTACK;
    p->decay = DNA_DEFAULT_DECAY;
    p->sustain = DNA_DEFAULT_SUSTAIN;
    p->release = DNA_DEFAULT_RELEASE;
    p->level = DNA_DEFAULT_LEVEL;
    p->bend_range = DNA_DEFAULT_BEND_RANGE;
}

/* ---- the wave ----------------------------------------------------------------------------------
 *
 * One cycle is DNA_CYCLE_SLOTS slots, each holding one value. The table is two cycles long (the SUB knob
 * makes the second differ from the first). The waveform is piecewise constant, so a plain table lookup
 * aliases badly. Instead the table keeps its first and second integrals, and a sample is the second
 * difference of the second integral taken one phase step either side of the read position, divided by
 * the step squared. That is the waveform filtered by a triangle two samples wide, evaluated exactly
 * (second-order antiderivative anti-aliasing), so no table interpolation error and no oversampling.
 * The table has zero mean, and the first integral is then forced to zero mean too, so both integrals
 * are periodic and the read position can wrap freely.
 * Values are doubles: at low notes the step squared is around 1e-8 and a float would lose the result. */

static void wave_build(dna_wave *w, const double *gene, double nbits, double sub_w, double norm_in, double *norm_out)
{
    double v[DNA_CYCLE_SLOTS];
    double raw[DNA_WAVE_SLOTS];

    nbits = clampd(nbits, 2.0, (double)DNA_GENES);
    int n0 = (int)nbits;
    double f = nbits - (double)n0;
    int n1 = n0 < DNA_GENES ? n0 + 1 : n0;
    /* Stretch the first nbits genes over the cycle. A fractional bit count blends the two neighbouring
     * integer counts, so turning the knob never jumps. */
    for (int i = 0; i < DNA_CYCLE_SLOTS; i++) {
        const int k0 = (i * n0) >> 7;
        const int k1 = (i * n1) >> 7;
        v[i] = (1.0 - f) * gene[k0] + f * gene[k1];
        /* A faint square under the genome. Without it a genome whose first few bits agree is a flat line and
         * the voice goes silent; with it the wave never falls below a square at 15% before it is normalized. */
        v[i] += SQUARE_BIAS * (i < DNA_CYCLE_SLOTS / 2 ? 1.0 : -1.0);
    }
    double mean = 0.0;
    for (int i = 0; i < DNA_CYCLE_SLOTS; i++) {
        raw[i] = v[i];
        /* second cycle: the first, drifting toward its own inverse read from a different start */
        raw[DNA_CYCLE_SLOTS + i] = (1.0 - sub_w) * v[i] - sub_w * v[(i + 43) & (DNA_CYCLE_SLOTS - 1)];
    }
    for (int i = 0; i < DNA_WAVE_SLOTS; i++)
        mean += raw[i];
    mean /= (double)DNA_WAVE_SLOTS;
    double peak = 0.0;
    for (int i = 0; i < DNA_WAVE_SLOTS; i++) {
        raw[i] -= mean;
        if (fabs(raw[i]) > peak)
            peak = fabs(raw[i]);
    }
    double norm = norm_in;
    if (norm <= 0.0)
        norm = peak > 1e-9 ? 1.0 / peak : 1.0;
    if (norm_out)
        *norm_out = norm;

    const double wd = 1.0 / (double)DNA_WAVE_SLOTS;
    double i1 = 0.0, j2 = 0.0;
    for (int i = 0; i < DNA_WAVE_SLOTS; i++) {
        const double a = raw[i] * norm;
        w->a[i] = a;
        w->i1[i] = i1;
        w->j2[i] = j2;
        j2 += (i1 + 0.5 * a * wd) * wd;
        i1 += a * wd;
    }
    w->j2_total = j2;
}

/* Second integral of the table at x in [0, 1), with its own linear trend removed so it is periodic. */
static inline double wave_j(const dna_wave *w, double x)
{
    const double t = x * (double)DNA_WAVE_SLOTS;
    int i = (int)t;
    if (i > DNA_WAVE_SLOTS - 1)
        i = DNA_WAVE_SLOTS - 1;
    const double u = (t - (double)i) * (1.0 / (double)DNA_WAVE_SLOTS);
    return w->j2[i] + u * (w->i1[i] + 0.5 * w->a[i] * u) - w->j2_total * x;
}

static inline double wrap01(double x)
{
    return x - floor(x);
}

/* The waveform at phase x (0..1 over two cycles), filtered for a phase step of s per sample. */
static inline double wave_read(const dna_wave *w, double x, double s)
{
    const double jm = wave_j(w, wrap01(x - s));
    const double jc = wave_j(w, x);
    const double jp = wave_j(w, wrap01(x + s));
    return (jp - 2.0 * jc + jm) / (s * s);
}

/* ---- genome ---- */

static void genome_make(dna_t *d)
{
    unsigned s = 0x2545F491u;
    for (int g = 0; g < 3; g++)
        for (int k = 0; k < DNA_GENES; k++)
            d->genome[g][k] = (unsigned char)((xorshift(&s) >> 11) & 1u);
    for (int k = 0; k < DNA_GENES; k++)
        d->order[k] = (float)(xorshift(&s) >> 8) * (1.0f / 16777216.0f);
}

/* MUTATE walks every bit from the original toward the mutant on its side. Each bit has its own threshold, so
 * the genome changes a few bits at a time, and each bit crossfades over a short stretch of the knob. */
static void gene_update(dna_t *d)
{
    const double m = d->s_mutate;
    const double mm = fabs(m);
    const int mut = m < 0.0 ? 1 : 2;
    for (int k = 0; k < DNA_GENES; k++) {
        const double t = 0.5 / MUTATE_RAMP + (double)d->order[k] * (1.0 - 1.0 / MUTATE_RAMP);
        double x = clampd((mm - t) * MUTATE_RAMP + 0.5, 0.0, 1.0);
        x = x * x * (3.0 - 2.0 * x);
        const double a = d->genome[0][k] ? 1.0 : -1.0;
        const double b = d->genome[mut][k] ? 1.0 : -1.0;
        d->gene[k] = a + (b - a) * x;
    }
}

static void wave_update(dna_t *d)
{
    gene_update(d);
    d->nbits = (double)dna_bits_value(d->s_bits);
    d->sub_w = (double)d->s_sub;
    wave_build(&d->wave, d->gene, d->nbits, d->sub_w, 0.0, &d->norm);
    d->wave_ver++;
}

static void voice_flips_draw(dna_t *d, dna_voice *v)
{
    const double p = 0.5 * (double)d->s_grit;
    const unsigned thr = (unsigned)(p * 16777216.0);
    for (int k = 0; k < DNA_GENES; k++)
        v->flip[k] = (xorshift(&v->rng) >> 8) < thr;
}

static void voice_build_own(dna_t *d, dna_voice *v)
{
    double g[DNA_GENES];
    for (int k = 0; k < DNA_GENES; k++)
        g[k] = v->flip[k] ? -d->gene[k] : d->gene[k];
    wave_build(&v->wave_own, g, d->nbits, d->sub_w, d->norm, NULL);
    v->wave_ver = d->wave_ver;
    v->since_build = 0;
}

/* ---- setup ---- */

static float one_pole_coef(float tau_seconds, float sr, float samples)
{
    return 1.0f - expf(-samples / (tau_seconds * sr));
}

void dna_set_params(dna_t *d, const dna_params *in)
{
    dna_params *t = &d->target;
    t->mutate = clampf(in->mutate, -1.0f, 1.0f);
    t->bits = clampf(in->bits, 0.0f, 1.0f);
    t->sub = clampf(in->sub, 0.0f, 1.0f);
    t->grit = clampf(in->grit, 0.0f, 1.0f);
    t->attack = clampf(in->attack, 0.0005f, 10.0f);
    t->decay = clampf(in->decay, 0.005f, 30.0f);
    t->sustain = clampf(in->sustain, 0.0f, 1.0f);
    t->release = clampf(in->release, 0.005f, 30.0f);
    t->level = clampf(in->level, 0.0f, 1.0f);
    t->bend_range = clampf(in->bend_range, 0.0f, 24.0f);

    d->att_inc = 1.0f / (t->attack * d->sr);
    /* exp(-6.9) is -60 dB: decay and release are the times to fall 60 dB. */
    d->dec_c = expf(-6.9f / (t->decay * d->sr));
    d->rel_c = expf(-6.9f / (t->release * d->sr));
}

void dna_reset(dna_t *d)
{
    memset(d->voice, 0, sizeof d->voice);
    for (int i = 0; i < DNA_VOICES; i++) {
        /* spread the voices across the stereo field, constant power, +-0.25 */
        const float pan = ((float)((i * 5) % DNA_VOICES) - 3.5f) / 3.5f * 0.25f;
        const float ang = (pan + 1.0f) * 0.25f * (float)TWO_PI * 0.5f;
        d->voice[i].pan_l = cosf(ang);
        d->voice[i].pan_r = sinf(ang);
        d->voice[i].fade = 1.0f;
    }
    const dna_params *t = &d->target;
    d->s_mutate = t->mutate;
    d->s_bits = t->bits;
    d->s_sub = t->sub;
    d->s_grit = t->grit;
    d->s_level = t->level;
    d->bend_norm = 0.0f;
    d->s_bend = 0.0f;
    d->mod_wheel = 0.0f;
    d->s_mod = 0.0f;
    d->semi_off = 0.0;
    d->lfo_ph = 0.0f;
    d->pedal = 0;
    d->level_cur = d->s_level;
    d->level_d = 0.0f;
    d->tick_left = 0;
    d->act_peak = 0.0f;
    d->rng = 0x1234ABCDu;
    wave_update(d);
}

void dna_init(dna_t *d, float sample_rate)
{
    memset(d, 0, sizeof *d);
    d->sr = clampf(sample_rate, 8000.0f, 384000.0f);
    genome_make(d);
    d->c_smooth = one_pole_coef(0.005f, d->sr, (float)CTRL);
    d->fade_inc = 1.0f / (d->sr * 0.001f > 8.0f ? d->sr * 0.001f : 8.0f);
    dna_default_params(&d->target);
    dna_set_params(d, &d->target);
    dna_reset(d);
}

/* ---- notes ---- */

/* Phase step per sample for a note, in turns of the two-cycle table (so half the note frequency over sr). */
static double note_inc(const dna_t *d, int note)
{
    double f = 440.0 * pow(2.0, ((double)(note - 69) + d->semi_off) / 12.0);
    const double top = 0.45 * (double)d->sr;
    if (f > top)
        f = top;
    return f / (2.0 * (double)d->sr);
}

static int voice_key(const dna_voice *v)
{
    return v->fading ? v->pend_note : v->note;
}

static dna_voice *find_voice(dna_t *d, int note)
{
    dna_voice *quiet = NULL, *oldest = NULL;
    for (int i = 0; i < DNA_VOICES; i++) {
        dna_voice *v = &d->voice[i];
        if (v->state != V_IDLE && voice_key(v) == note)
            return v; /* same note again: retrigger the voice it already has */
    }
    for (int i = 0; i < DNA_VOICES; i++)
        if (d->voice[i].state == V_IDLE)
            return &d->voice[i];
    /* Steal. A voice already in release goes first, the quietest of those; otherwise the oldest note. */
    for (int i = 0; i < DNA_VOICES; i++) {
        dna_voice *v = &d->voice[i];
        if (v->state == V_RELEASE && !v->fading && (!quiet || v->env < quiet->env))
            quiet = v;
        if (!oldest || (unsigned)(d->serial - v->age) > (unsigned)(d->serial - oldest->age))
            oldest = v;
    }
    return quiet ? quiet : oldest;
}

/* Begin a note on a voice that is silent or has just finished fading. */
static void voice_start(dna_t *d, dna_voice *v, int note, float vel, int held, int sustained, unsigned age)
{
    const float pl = v->pan_l, pr = v->pan_r;
    memset(v, 0, sizeof *v);
    v->pan_l = pl;
    v->pan_r = pr;
    v->rng = xorshift(&d->rng) | 1u;
    v->note = note;
    v->vel = vel;
    v->held = held;
    v->sustained = sustained;
    v->age = age;
    v->fade = 1.0f;
    v->env = 0.0f;
    v->state = (held || sustained) ? V_ATTACK : V_RELEASE;
    v->inc = note_inc(d, note);
    v->inc_d = 0.0;
    v->fresh = 1;
    v->since_build = 1 << 20;
}

void dna_note_on(dna_t *d, int note, int velocity)
{
    if (velocity <= 0) {
        dna_note_off(d, note);
        return;
    }
    note = note < 0 ? 0 : (note > 127 ? 127 : note);
    velocity = velocity > 127 ? 127 : velocity;
    const float vel = (float)velocity * (1.0f / 127.0f);

    dna_voice *v = find_voice(d, note);
    const unsigned age = ++d->serial;
    if (v->state == V_IDLE) {
        voice_start(d, v, note, vel, 1, 0, age);
    } else {
        /* A sounding voice fades out over about a millisecond and the new note starts from silence, so the
         * takeover never steps the output. */
        v->fading = 1;
        v->pend_note = note;
        v->pend_vel = vel;
        v->held = 1;
        v->sustained = 0;
        v->age = age;
    }
    d->tick_left = 0;
}

void dna_note_off(dna_t *d, int note)
{
    for (int i = 0; i < DNA_VOICES; i++) {
        dna_voice *v = &d->voice[i];
        if (v->state == V_IDLE || voice_key(v) != note || !v->held)
            continue;
        v->held = 0;
        if (d->pedal)
            v->sustained = 1;
        else if (!v->fading)
            v->state = V_RELEASE;
    }
}

void dna_pitch_bend(dna_t *d, float norm)
{
    d->bend_norm = clampf(norm, -1.0f, 1.0f);
}

void dna_mod_wheel(dna_t *d, float norm)
{
    d->mod_wheel = clampf(norm, 0.0f, 1.0f);
}

void dna_sustain_pedal(dna_t *d, int down)
{
    d->pedal = down ? 1 : 0;
    if (d->pedal)
        return;
    for (int i = 0; i < DNA_VOICES; i++) {
        dna_voice *v = &d->voice[i];
        if (v->state != V_IDLE && v->sustained) {
            v->sustained = 0;
            if (!v->fading)
                v->state = V_RELEASE;
        }
    }
}

void dna_all_off(dna_t *d)
{
    d->pedal = 0;
    for (int i = 0; i < DNA_VOICES; i++) {
        dna_voice *v = &d->voice[i];
        if (v->state == V_IDLE)
            continue;
        v->held = 0;
        v->sustained = 0;
        if (!v->fading)
            v->state = V_RELEASE;
    }
}

int dna_active_voices(const dna_t *d)
{
    int n = 0;
    for (int i = 0; i < DNA_VOICES; i++)
        n += d->voice[i].state != V_IDLE;
    return n;
}

void dna_take_activity(dna_t *d, dna_activity *out)
{
    out->peak = d->act_peak;
    out->voices = dna_active_voices(d);
    out->notes = d->serial;
    d->act_peak = 0.0f;
}

/* ---- control tick ---- */

static void tick_begin(dna_t *d)
{
    const dna_params *t = &d->target;
    const float c = d->c_smooth;

    const float om = d->s_mutate, ob = d->s_bits, os = d->s_sub;
#define SMOOTH(s, tgt) \
    do { \
        (s) += c * ((tgt) - (s)); \
        if (fabsf((tgt) - (s)) < 1e-6f) \
            (s) = (tgt); \
    } while (0)
    SMOOTH(d->s_mutate, t->mutate);
    SMOOTH(d->s_bits, t->bits);
    SMOOTH(d->s_sub, t->sub);
    SMOOTH(d->s_grit, t->grit);
    SMOOTH(d->s_level, t->level);
    SMOOTH(d->s_bend, d->bend_norm);
    SMOOTH(d->s_mod, d->mod_wheel);
#undef SMOOTH
    if (d->s_mutate != om || d->s_bits != ob || d->s_sub != os || d->wave_ver == 0)
        wave_update(d);

    const float inv = 1.0f / (float)CTRL;
    d->level_d = (d->s_level - d->level_cur) * inv;

    d->lfo_ph += (float)CTRL * VIBRATO_HZ / d->sr;
    d->lfo_ph -= floorf(d->lfo_ph);
    d->semi_off = (double)(d->s_bend * t->bend_range) +
                  (double)(d->s_mod * VIBRATO_SEMIS) * sin(TWO_PI * (double)d->lfo_ph);

    const int grit_on = d->s_grit > 1e-3f;
    for (int i = 0; i < DNA_VOICES; i++) {
        dna_voice *v = &d->voice[i];
        if (v->state == V_IDLE)
            continue;
        const double target = note_inc(d, v->note);
        if (v->fresh) {
            v->inc = target;
            v->inc_d = 0.0;
            v->fresh = 0;
        } else {
            v->inc_d = (target - v->inc) * (double)inv;
        }
        if (!grit_on) {
            v->own = 0;
        } else if (!v->own) {
            voice_flips_draw(d, v);
            voice_build_own(d, v);
            v->own = 1;
        } else if (v->wave_ver != d->wave_ver) {
            voice_build_own(d, v);
        }
    }
}

static void render_voice(dna_t *d, dna_voice *v, float *l, float *r, int n)
{
    const float sus = d->target.sustain;
    const int grit_on = d->s_grit > 1e-3f;
    float env = v->env;
    int state = v->state;
    double ph = v->ph, inc = v->inc, inc_d = v->inc_d;
    int sb = v->since_build; /* samples since the own wave was built, saturating */
    float fade = v->fade;
    const float vel = v->vel;
    const float pl = v->pan_l, pr = v->pan_r;
    const dna_wave *w = v->own ? &v->wave_own : &d->wave;

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
        if ((state == V_RELEASE || (state == V_DECAY && sus < 1e-4f)) && env < 1e-4f && !v->fading) {
            state = V_IDLE;
            env = 0.0f;
            break;
        }

        if (v->fading) {
            fade -= d->fade_inc;
            if (fade <= 0.0f) {
                /* Faded out: the waiting note takes the voice over, from silence. */
                const int held = v->held, sus_p = v->sustained;
                voice_start(d, v, v->pend_note, v->pend_vel, held, sus_p, v->age);
                v->own = 0;
                w = &d->wave;
                env = 0.0f;
                state = v->state;
                ph = 0.0;
                inc = v->inc;
                inc_d = 0.0;
                sb = v->since_build;
                fade = 1.0f;
                continue;
            }
        }

        const double y = wave_read(w, ph, inc);
        const float o = (float)y * env * vel * fade;
        l[i] += o * pl;
        r[i] += o * pr;

        ph += inc;
        inc += inc_d;
        if (ph >= 1.0) {
            ph -= 1.0;
            if (grit_on && sb >= GRIT_MIN_GAP) {
                voice_flips_draw(d, v);
                voice_build_own(d, v);
                v->own = 1;
                w = &v->wave_own;
                sb = 0;
            }
        }
        if (sb < (1 << 20))
            sb++;
    }

    v->env = env;
    v->state = state;
    v->ph = ph;
    v->inc = inc;
    v->fade = fade;
    v->inc_d = inc_d;
    v->since_build = sb;
}

void dna_render(dna_t *d, float *l, float *r, int frames)
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

        for (int v = 0; v < DNA_VOICES; v++)
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
        }
        d->level_cur = lev;
        d->act_peak = peak;

        d->tick_left -= n;
        done += n;
    }
}
