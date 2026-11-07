/* SPDX-License-Identifier: GPL-2.0-or-later */
/* look-check (Analog Rytm MKII): core-ar2 0.12's knob looks (README).
 *
 * LOOK CHECK pages behind the LFO key, four pairs of knobs a page: on A, C,
 * E and G a stock id L, on B, D, F and H a knob id of the mod's own whose
 * core_knob_looks entry names L. Both knobs of a pair show the same value
 * (this mod's core_params answers both), the same text (stock's for L), the
 * same label and name (L's), so a pair whose two cells are not pixel for
 * pixel the same is a look core did not give. The pairs cover every
 * different knob display stock builds (looks.h, from the UI records of a
 * stock 1.74 boot). The last page's own knobs take their record from a ui
 * callback (a copy of L's record) and one a range in place of L's.
 *
 * In RAM after the magic LKC1: what core's two lookups answer for each own
 * knob, read at the first tick (uirec(id) against L's record, range(id)
 * against L's or the entry's), for a check by memory. */
#include <stdint.h>
#include "analog-rytm-mk2/pages.h"
#include "looks.h"                          /* LOOK_IDS, NLOOK */

CORE_KNOBS;

#define PAIRS_A_PAGE 4
#define PAGES ((NLOOK + 2 + PAIRS_A_PAGE - 1) / PAIRS_A_PAGE)
#define NOWN (NLOOK + 2)                    /* + the ui pair and the range pair */
#define FIRST_PAGE 60
#define UI_PAIR NLOOK                       /* own knob NLOOK: ui callback */
#define RANGE_PAIR (NLOOK + 1)              /* own knob NLOOK + 1: min, max */
#define RANGE_LOOK 94                       /* COMP MIX, 0..127 */
#define RANGE_MIN (0 << 8)
#define RANGE_MAX (12 << 8)

#define PARAMS ((const uint8_t *)0x401bfbc8)     /* 52 bytes an id */
#define P_MIN(id) (*(const int32_t *)(PARAMS + 52 * (id) + 0x08))
#define P_MAX(id) (*(const int32_t *)(PARAMS + 52 * (id) + 0x0c))
#define P_NAME(id) (*(const char *const *)(PARAMS + 52 * (id) + 0x28))
#define P_LABEL(id) (*(const char *const *)(PARAMS + 52 * (id) + 0x30))
#define UIREC ((uintptr_t (*)(int32_t))0x400ff5c2)  /* answers in %d0 */

static const int16_t looks[NOWN] = {LOOK_IDS, UI_LOOK, RANGE_LOOK};
static int32_t value[NOWN];             /* each pair's value, 8.8 */
static uint8_t own_ui[84];

struct {
    char magic[4];
    uint32_t nown, first;               /* own knobs and the first own id */
    uint32_t ticks;
    struct { int16_t look, ok; uint32_t uirec, want; int32_t range[3], want_range[2]; } k[NOWN];
} lk = {{'L', 'K', 'C', '1'}};

static int own_ix(int32_t id) { return (uint32_t)(id - CORE_KNOB(0)) < NOWN ? id - CORE_KNOB(0) : -1; }

/* the pair a knob's id belongs to, for a stock id the page's own pair */
static int pair_of(int32_t id, const void *view_unused)
{
    int i = own_ix(id), k;
    (void)view_unused;
    if (i >= 0)
        return i;
    for (k = 0; k < NOWN; k++)
        if (looks[k] == id)
            return k;
    return -1;
}

static int32_t lo(int k) { return k == RANGE_PAIR ? RANGE_MIN : P_MIN(looks[k]); }
static int32_t hi(int k) { return k == RANGE_PAIR ? RANGE_MAX : P_MAX(looks[k]); }

static int p_get(int32_t id, int32_t *v)
{
    int k = pair_of(id, 0);
    if (k < 0)
        return 0;
    if (!value[k])
        value[k] = lo(k) + ((hi(k) - lo(k)) * 3 / 8 & ~0xff);
    *v = value[k];
    return 1;
}

static int p_delta(void *view, int32_t id, int32_t d, int32_t func)
{
    int k = pair_of(id, view);
    int32_t v;
    (void)func;
    if (k < 0)
        return 0;
    p_get(id, &v);
    v += d;
    value[k] = v < lo(k) ? lo(k) : v > hi(k) ? hi(k) : v;
    return 1;
}

/* both knobs of a pair: stock's text for L */
static int p_text(struct core_text *t)
{
    int k = pair_of(t->id, 0);
    if (k < 0)
        return CORE_TEXT_PASS;
    t->id = looks[k];
    return CORE_TEXT_STOCK;
}

static const char *l_label(int32_t id) { int i = own_ix(id); return i < 0 ? 0 : P_LABEL(looks[i]); }
static const char *l_name(int32_t id) { int i = own_ix(id); return i < 0 ? 0 : P_NAME(looks[i]); }

static const void *l_ui(int32_t id)
{
    if (!own_ui[0] && !own_ui[1] && !own_ui[2] && !own_ui[3]) {
        const uint8_t *s = core_knob_stock_ui(looks[UI_PAIR]);
        int n;
        for (n = 0; n < 84; n++)
            own_ui[n] = s[n];
    }
    (void)id;
    return own_ui;
}

/* range(id) into r, as stock calls it: the 12 bytes' place in %a0 */
static void range_of(int32_t id, int32_t *r)
{
    register int32_t *a0 __asm__("a0") = r;
    __asm__ volatile("move.l %1,-(%%sp)\n\tjsr 0x40006ff0\n\taddq.l #4,%%sp"
                     : "+a"(a0) : "g"(id) : "d0", "d1", "a1", "memory", "cc");
}

void lk_tick(void *ctrl)
{
    int k;
    (void)ctrl;
    if (lk.ticks++)
        return;
    lk.nown = NOWN;
    lk.first = CORE_KNOB(0);
    for (k = 0; k < NOWN; k++) {
        int32_t id = CORE_KNOB(k);
        lk.k[k].look = looks[k];
        lk.k[k].uirec = UIREC(id);
        lk.k[k].want = k == UI_PAIR ? (uint32_t)own_ui : UIREC(looks[k]);
        range_of(id, lk.k[k].range);
        lk.k[k].want_range[0] = lo(k);
        lk.k[k].want_range[1] = hi(k);
        lk.k[k].ok = (k == UI_PAIR ? lk.k[k].uirec == (uint32_t)l_ui(id) : lk.k[k].uirec == lk.k[k].want) &&
                     lk.k[k].range[0] == lo(k) && lk.k[k].range[1] == hi(k);
    }
}

/* the tables */
#define OWN(k) CORE_KNOB(k)
const struct core_param lk_params_own = {CORE_KNOB(0), CORE_KNOB(NOWN - 1), p_get, p_delta, p_text};
const struct core_param_lock lk_locks = {CORE_KNOB(0), CORE_KNOB(NOWN - 1), 0, 0, 0, 0, l_label, l_name};
const struct core_knob_look lk_looks_each[NLOOK] = {LOOK_ENTRIES};
const struct core_knob_look lk_look_ui = {CORE_KNOB(UI_PAIR), CORE_KNOB(UI_PAIR), UI_LOOK, l_ui, 0, 0};
const struct core_knob_look lk_look_range = {CORE_KNOB(RANGE_PAIR), CORE_KNOB(RANGE_PAIR), RANGE_LOOK, 0,
                                             RANGE_MIN, RANGE_MAX};
const struct core_param lk_params_stock = {1, 468, p_get, p_delta, p_text};
