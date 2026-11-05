/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sidecar-check (Analog Rytm MKII): the core-ar2 0.11 calls the Sidecar
 * mods use, checked in RAM (README). Part 1: locks the mod keeps itself. */
#include <stdint.h>
#include "analog-rytm-mk2/pages.h"

#define FIRST 560
#define IDS 4                               /* 560..563 on knobs A..D */
#define TRACKS 13
#define STEPS 64
#define RING 64

enum { K_GET, K_TURN, K_SET, K_CLEAR, K_TEXT, K_DELTA, KINDS };

/* a call: what it was, and its arguments; step keeps CORE_LOCK_RECORDING */
struct sc_call { uint16_t kind, id; int16_t track, step; int32_t arg, ret; };

/* everything an emulator or a unit's readback reads, after the magic SCK1 */
struct {
    char magic[4];
    uint32_t n;                             /* calls logged (not get) */
    uint32_t count[KINDS];                  /* every call, get too */
    struct sc_call e[RING];                 /* the last RING, at n % RING */
    struct sc_call last_get;                /* the last get */
    struct sc_call last_text;               /* the last text: step and track from core_held_step */
    int32_t base[IDS];                      /* the knobs' values without a trig, 8.8 */
    int16_t lock[IDS][TRACKS][STEPS];       /* 8.8 + 1, 0 none */
    uint32_t ticks;                         /* ev_tick calls */
    uint16_t trigs, lock_trigs;             /* track 1's steps 1..16 in the current pattern */
} sc = {{'S', 'C', 'K', '1'}};

static int ix(int32_t id) { return (unsigned)(id - FIRST) < IDS ? id - FIRST : -1; }
static int ok(int32_t t, int32_t s) { return (unsigned)t < TRACKS && (unsigned)(s & 0xff) < STEPS; }
static int clamp(int v) { return v < 0 ? 0 : v > (127 << 8) ? 127 << 8 : v; }

static void note(int kind, int32_t id, int32_t t, int32_t s, int32_t arg, int32_t ret)
{
    struct sc_call c = {kind, id, t, s, arg, ret};
    sc.count[kind]++;
    if (kind == K_GET) {
        sc.last_get = c;
        return;
    }
    if (kind == K_TEXT) {
        sc.last_text = c;
        return;
    }
    sc.e[sc.n++ % RING] = c;
}

/* ---- core_param_locks: locks kept here, per id, track and step ---- */

static int l_get(int32_t id, int32_t t, int32_t s)
{
    int i = ix(id), r = i >= 0 && ok(t, s) && sc.lock[i][t][s] ? sc.lock[i][t][s] - 1 : -1;
    note(K_GET, id, t, s, 0, r);
    return r;
}

static int l_set(int32_t id, int32_t t, int32_t s, int32_t v)
{
    int i = ix(id), r = i >= 0 && ok(t, s);
    if (r)
        sc.lock[i][t][s & 0xff] = clamp(v) + 1;
    note(K_SET, id, t, s, v, r);
    return r;
}

/* a turn with a trig held moves the lock (from the value without one);
 * live recording's (CORE_LOCK_RECORDING) locks the knob's value now */
static int l_turn(int32_t id, int32_t t, int32_t s, int32_t d)
{
    int i = ix(id), v = -1, st = s & 0xff;
    if (i >= 0 && ok(t, s)) {
        if (s & CORE_LOCK_RECORDING)
            v = sc.base[i];
        else
            v = clamp((sc.lock[i][t][st] ? sc.lock[i][t][st] - 1 : sc.base[i]) + d);
        sc.lock[i][t][st] = v + 1;
    }
    note(K_TURN, id, t, s, d, v);
    return v;
}

static int l_clear(int32_t id, int32_t t, int32_t s)
{
    int i = ix(id), r = i >= 0 && ok(t, s);
    if (r)
        sc.lock[i][t][s] = 0;
    note(K_CLEAR, id, t, s, 0, r);
    return r;
}

static const char *const labels[IDS] = {"SCA", "SCB", "SCC", "SCD"};
static const char *const names[IDS] = {"SC KNOB A", "SC KNOB B", "SC KNOB C", "SC KNOB D"};

static const char *l_label(int32_t id) { return ix(id) >= 0 ? labels[ix(id)] : 0; }
static const char *l_name(int32_t id) { return ix(id) >= 0 ? names[ix(id)] : 0; }

const struct core_param_lock sc_locks = {FIRST, FIRST + IDS - 1, l_get, l_turn, l_set, l_clear,
                                         l_label, l_name};

/* ---- core_params: the knobs without a trig, and their text ---- */

static int p_get(int32_t id, int32_t *value)
{
    if (ix(id) < 0)
        return 0;
    *value = sc.base[ix(id)];
    return 1;
}

static int p_delta(void *view, int32_t id, int32_t delta, int32_t func)
{
    (void)view; (void)func;
    if (ix(id) < 0)
        return 0;
    sc.base[ix(id)] = clamp(sc.base[ix(id)] + delta);
    note(K_DELTA, id, -1, -1, delta, sc.base[ix(id)]);
    return 1;
}

/* the value, and with a trig held "L" and its step (1..64): "L5 64" */
static int p_text(struct core_text *t)
{
    int32_t track = -1, step = core_held_step(&track), v;
    char *b = t->buf;
    if (ix(t->id) < 0)
        return CORE_TEXT_PASS;
    note(K_TEXT, t->id, track, step, t->value, 0);
    if (step >= 0) {
        *b++ = 'L';
        if (step + 1 >= 10)
            *b++ = '0' + (step + 1) / 10;
        *b++ = '0' + (step + 1) % 10;
        *b++ = ' ';
    }
    v = (t->value < 0 ? 0 : t->value) >> 8;
    if (v >= 100)
        *b++ = '0' + v / 100;
    if (v >= 10)
        *b++ = '0' + v / 10 % 10;
    *b++ = '0' + v % 10;
    *b = 0;
    return CORE_TEXT_DONE;
}

const struct core_param sc_params = {FIRST, FIRST + IDS - 1, p_get, p_delta, p_text};

/* a page behind the LFO key, and one behind the FX LFO key */
const struct core_page sc_page = {20, "SIDECAR CHECK", {560, 561, 562, 563, 0, 0, 0, 0}};
const struct core_view_page sc_view_page = {CORE_VIEW_LFO, 20, 0};
const struct core_page sc_fx_page = {21, "SIDECAR CHECK FX", {560, 561, 562, 563, 0, 0, 0, 0}};
const struct core_view_page sc_fx_view_page = {CORE_VIEW_FX_LFO, 21, 0};

/* ev_tick: track 1's trigs and lock trigs on steps 1..16 (bit 0 = step 1),
 * so a check can tell whether a held step kept a trig */
#define APP ((unsigned (*)(void))0x4017fa0a)
#define CURRENT_PATTERN ((unsigned (*)(unsigned))0x400af0ac)
#define TRACK_REC ((unsigned (*)(unsigned, int))0x400ab29e)
#define HAS_TRIG ((unsigned char (*)(unsigned, int))0x400bff7e)
#define HAS_LOCK_TRIG ((unsigned char (*)(unsigned, int))0x400c000e)

void sc_tick(void *ctrl)
{
    unsigned rec = TRACK_REC(CURRENT_PATTERN(APP()), 0);
    unsigned t = 0, l = 0;
    int i;
    (void)ctrl;
    for (i = 0; i < 16; i++) {
        if (HAS_TRIG(rec, i))
            t |= 1u << i;
        if (HAS_LOCK_TRIG(rec, i))
            l |= 1u << i;
    }
    sc.trigs = t;
    sc.lock_trigs = l;
    sc.ticks++;
}
