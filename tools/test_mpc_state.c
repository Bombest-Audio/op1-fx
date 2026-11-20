/* Round trip of the preset state op1fx_act.h adds (issue #3) through one port's engine table, without the wrapper:
 * every saved parameter survives save and load into an instance set elsewhere, a key-less chunk gives the defaults,
 * and junk can't crash it. Built and run per plugin by tools/test_mpc_state.sh against the port's params.h. */
#include "engine.h"
#include "params.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int saved(int i)
{
    const param_t *p = &PARAMS[i];
    return strcmp(p->key, "act") && !p->momentary && p->step_target < 0 && p->popup_of < 0 && !p->string_display;
}

static void norm_str(int i, float n, char *buf, int len)
{
    const param_t *p = &PARAMS[i];
    if (p->nopts) snprintf(buf, len, "%d", (int)lroundf(n * (p->nopts - 1)));
    else if (p->int_display) snprintf(buf, len, "%ld", lroundf(p->min + (p->max - p->min) * n));
    else snprintf(buf, len, "%g", p->min + (p->max - p->min) * n);
}

static void randomize(const mpc_engine_t *e, void *x)
{
    char v[64];
    for (int i = 0; i < NPARAMS; i++)
        if (saved(i)) {
            norm_str(i, (float)rand() / (float)RAND_MAX, v, sizeof v);
            e->set_param(x, PARAMS[i].key, v);
        }
}

static int same(const mpc_engine_t *e, void *a, void *b, const char *what)
{
    int ok = 1;
    char va[64], vb[64];
    for (int i = 0; i < NPARAMS; i++) {
        if (!saved(i)) continue;
        int na = e->get_param(a, PARAMS[i].key, va, sizeof va), nb = e->get_param(b, PARAMS[i].key, vb, sizeof vb);
        if (na <= 0 && nb <= 0) continue;
        double da = atof(va), db = atof(vb);
        if (na <= 0 || nb <= 0 || fabs(da - db) > 1e-4 * (1 + fabs(da))) {
            ok = 0;
            printf("FAIL %s: %s a=%s b=%s\n", what, PARAMS[i].key, na > 0 ? va : "-", nb > 0 ? vb : "-");
        }
    }
    if (!ok) fails++;
    return ok;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : NULL;
    const mpc_engine_t *e = mpc_engine();
    srand(1234);
    static char st[8192], st2[8192];
    for (int round = 0; round < 20; round++) {
        void *a = e->create(dir), *b = e->create(dir);
        randomize(e, a);
        randomize(e, b);   /* b starts somewhere else: loading must move it, the reported bug */
        int n = e->get_param(a, "state", st, sizeof st);
        CHECK(n > 0 && !strncmp(st, "op1\n", 4), "state saved (%d bytes)", n);
        CHECK(n < 0 || (int)strlen(st) == n, "length matches the text");
        e->set_param(b, "state", st);
        same(e, a, b, "round trip");
        int n2 = e->get_param(b, "state", st2, sizeof st2);
        /* exact only without an engine state: drumsampler's own text rounds slot values to 4 places and goes last */
        if (!strstr(st, "\nstate="))
            CHECK(n2 == n && !strcmp(st, st2), "re-saved state is identical");
        else
            CHECK(n2 > 0 && strstr(st2, "\nstate=v1;"), "engine state saved after the reload");
        e->destroy(a);
        e->destroy(b);
    }
    {   /* a chunk with no keys puts every saved param back to its default */
        void *a = e->create(dir), *d = e->create(dir);
        char v[64];
        for (int i = 0; i < NPARAMS; i++)   /* d: params.json defaults, as the wrapper's norm_to_str() sends them */
            if (saved(i)) { norm_str(i, PARAMS[i].def, v, sizeof v); e->set_param(d, PARAMS[i].key, v); }
        randomize(e, a);
        e->set_param(a, "state", "op1\n");
        same(e, a, d, "empty op1 chunk = defaults");
        e->destroy(a);
        e->destroy(d);
    }
    {   /* junk must not crash, and unknown keys are ignored */
        void *a = e->create(dir);
        const char *junk[] = {"op1\n=\n\n==\nfoo=bar\nact=1\n", "op1\nstate=", "op1\nstate=garbage;|;", "op1", "", "v1;x", "op1\n\n\n"};
        for (unsigned j = 0; j < sizeof junk / sizeof *junk; j++) e->set_param(a, "state", junk[j]);
        char tiny[8];
        CHECK(e->get_param(a, "state", tiny, sizeof tiny) == 0, "a state too big for the buffer saves nothing");
        e->destroy(a);
    }
    printf("%s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
