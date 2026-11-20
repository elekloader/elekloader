/* SPDX-License-Identifier: GPL-2.0-or-later */
/* knob-flags-check (Analog Rytm MKII): core-ar2 0.18's knob flags (README).
 *
 * One page, KNOB FLAGS, behind the LFO key: A is stock's LFO DEST (id 67),
 * the reference; B, C and D are knob ids of the mod's own that all look like
 * LFO DEST (core_knob_looks, look 67):
 *
 *   B  KFL  a core_knob_flags entry with CORE_KNOB_LIST: opens the list
 *   C  KFM  an entry with every other bit (0xfffeffff): core masks them, so
 *           C behaves as D
 *   D  KFN  no entry: no list, the turn reaches this mod's delta
 *
 * The values are this mod's (core_params), shown with stock's LFO DEST text.
 *
 * In RAM after the magic KFC1, from the first tick on: what stock's flags
 * getter (0x40007234, which core takes) answers for 67 and for B, C and D,
 * and per own knob how often core asked get, delta and text, the last delta
 * and text value, and the value; the stock overlay state (screen.h) and how
 * often it changed. */
#include <stdint.h>
#include "analog-rytm-mk2/pages.h"
#include "analog-rytm-mk2/screen.h"

CORE_KNOBS;
CORE_PAGES;

#define NOWN 3
#define LOOK 67                                  /* LFO DEST */
#define PARAMS ((const uint8_t *)0x401bfbc8)     /* 52 bytes an id */
#define P_MIN(id) (*(const int32_t *)(PARAMS + 52 * (id) + 0x08))
#define P_MAX(id) (*(const int32_t *)(PARAMS + 52 * (id) + 0x0c))
#define FLAGS_OF ((uint32_t (*)(int32_t))0x40007234)   /* stock's getter, core's from 0.18 */

static const char *const labels[NOWN] = {"KFL", "KFM", "KFN"};
static const char *const names[NOWN] = {"FLAG LIST", "FLAG MASKED", "FLAG NONE"};

struct {
    char magic[4];
    uint32_t first;                     /* B's id; C and D follow */
    uint32_t ticks;
    uint32_t flags67;                   /* the getter's answer for stock's LFO DEST */
    uint32_t flags[NOWN];               /* ... for B, C, D: B has 0x10000, C and D not */
    struct { uint32_t gets, deltas, texts; int32_t last_delta, last_text, value; } k[NOWN];
    int32_t overlay;                    /* core_stock_overlay() at the last tick */
    uint32_t overlay_changes;
} kf = {{'K', 'F', 'C', '1'}};

static int own_ix(int32_t id) { return (uint32_t)(id - CORE_KNOB(0)) < NOWN ? id - CORE_KNOB(0) : -1; }

static int p_get(int32_t id, int32_t *v)
{
    int i = own_ix(id);
    if (i < 0)
        return 0;
    kf.k[i].gets++;
    *v = kf.k[i].value;
    return 1;
}

static int p_delta(void *view, int32_t id, int32_t d, int32_t func)
{
    int i = own_ix(id);
    int32_t v;
    (void)view;
    (void)func;
    if (i < 0)
        return 0;
    kf.k[i].deltas++;
    kf.k[i].last_delta = d;
    v = kf.k[i].value + d;
    kf.k[i].value = v < P_MIN(LOOK) ? P_MIN(LOOK) : v > P_MAX(LOOK) ? P_MAX(LOOK) : v;
    return 1;
}

static int p_text(struct core_text *t)          /* stock's LFO DEST text for the value */
{
    int i = own_ix(t->id);
    if (i < 0)
        return CORE_TEXT_PASS;
    kf.k[i].texts++;
    kf.k[i].last_text = t->value;
    t->id = LOOK;
    return CORE_TEXT_STOCK;
}

static const char *l_label(int32_t id) { int i = own_ix(id); return i < 0 ? 0 : labels[i]; }
static const char *l_name(int32_t id) { int i = own_ix(id); return i < 0 ? 0 : names[i]; }

void kf_tick(void *ctrl)
{
    int i, o;
    (void)ctrl;
    kf.ticks++;
    kf.first = CORE_KNOB(0);
    kf.flags67 = FLAGS_OF(LOOK);
    for (i = 0; i < NOWN; i++)
        kf.flags[i] = FLAGS_OF(CORE_KNOB(i));
    o = core_stock_overlay();
    if (o != kf.overlay && kf.ticks > 1)
        kf.overlay_changes++;
    kf.overlay = o;
}

/* the tables */
const struct core_param kf_params = {CORE_KNOB(0), CORE_KNOB(NOWN - 1), p_get, p_delta, p_text};
const struct core_param_lock kf_locks = {CORE_KNOB(0), CORE_KNOB(NOWN - 1), 0, 0, 0, 0, l_label, l_name};
const struct core_knob_look kf_looks = {CORE_KNOB(0), CORE_KNOB(NOWN - 1), LOOK, 0, 0, 0};
const struct core_knob_flag kf_flag_list = {CORE_KNOB(0), CORE_KNOB(0), CORE_KNOB_LIST};
const struct core_knob_flag kf_flag_masked = {CORE_KNOB(1), CORE_KNOB(1), 0xfffeffffu};
const struct core_page kf_page = {CORE_PAGE(0), "KNOB FLAGS", {LOOK, CORE_KNOB(0), CORE_KNOB(1), CORE_KNOB(2), 0, 0, 0, 0}};
const struct core_view_page kf_view = {CORE_VIEW_LFO, CORE_PAGE(0), 0};
