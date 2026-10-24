/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4 0.2: parameter pages and parameter overrides (docs/ADAPTING.md,
 * "Parameter pages (core-a4 0.2, Analog Four MKII)"): core-ar2 0.4's
 * (analog-rytm-mk2/pages.h) at the A4's routines. Core owns the entries of
 * stock's four page routines (page_info, page_get_value, param_apply_delta,
 * param_value_text) and asks the mods through two tables, contributed in
 * mod.json:
 *
 *     "contribute": [
 *       {"to": "core_pages", "order": 50, "data": "00000000",
 *        "relocs": [[0, "abs32", "sym:my_page", 0]]},
 *       {"to": "core_params", "order": 50, "data": "00000000",
 *        "relocs": [[0, "abs32", "sym:my_params", 0]]}]
 *
 * and in C:
 *
 *     const struct core_page my_page = {50, "MY PAGE", {449, 450}};
 *     const struct core_param my_params = {449, 450, my_get, my_delta, my_text};
 *
 * Entries are asked in table order (by "order"); the first that answers wins.
 * A page id must be one stock does not have (stock's are 0..47) and no other
 * mod uses. A function left 0 passes.
 *
 * What differs from the Rytm: a page has ten knobs (A..J), not eight, and
 * each A4 page is a view of its own. A page key steps through a list of
 * views for each track type, and core_view_pages adds one: core makes a
 * view for the page and appends it to that key's list, after stock's views:
 *
 *     {"to": "core_view_pages", "order": 30, "data": "00000000",
 *      "relocs": [[0, "abs32", "sym:my_view_page", 0]]}
 *
 *     const struct core_view_page my_view_page =
 *         {CORE_TRACK_SYNTH, CORE_KEY_AMP, 50, 0};
 *
 * The page key steps over the view while shown, if set, answers 0. At most
 * 16 views in a build. A project saved on a view a later build does not have
 * opens on MIXER (stock alone: PERFORMANCE).
 */
#ifndef ELEKLOADER_A4_PAGES_H
#define ELEKLOADER_A4_PAGES_H

#include <stdint.h>

/* page_info answers &page->name: the rest is stock's entry layout (0x2c bytes) */
struct core_page {
    int32_t page;
    const char *name;
    int32_t ids[10];        /* knobs A..J, 0 for none */
};

/* param_value_text's own arguments */
struct core_text {
    void *obj;
    int32_t id;
    int32_t value;          /* 8.8 */
    char *buf;
};

#define CORE_TEXT_PASS 0    /* not this entry's: the next entry, then stock */
#define CORE_TEXT_DONE 1    /* buf is written */
#define CORE_TEXT_STOCK 2   /* stock formats t->id and t->value (changed or not) */

struct core_param {
    int32_t first, last;    /* the ids this entry answers for */
    /* 1 and *value (8.8) when it answers, 0 to pass */
    int (*get)(int32_t id, int32_t *value);
    /* delta in 8.8, func 1 while FUNC is held; 1 when it took the turn (core
     * redraws the view and stock makes no lock), 0 to pass */
    int (*delta)(void *view, int32_t id, int32_t delta, int32_t func);
    int (*text)(struct core_text *t);
};

/* the track types a key's list belongs to */
#define CORE_TRACK_ALL 0        /* the keys every track has: MIX, PERF, ARP, NOTE */
#define CORE_TRACK_SYNTH 1      /* tracks 1-4 */
#define CORE_TRACK_FX 2
#define CORE_TRACK_CV 3

/* the page keys' codes */
#define CORE_KEY_PERF 0x30
#define CORE_KEY_ARP 0x31
#define CORE_KEY_NOTE 0x32
#define CORE_KEY_OSC1 0x33
#define CORE_KEY_OSC2 0x34
#define CORE_KEY_FILTERS 0x35
#define CORE_KEY_AMP 0x36
#define CORE_KEY_ENV 0x37
#define CORE_KEY_LFO 0x38
#define CORE_KEY_MIX 0xb3

struct core_view_page {
    int32_t type;           /* CORE_TRACK_* */
    int32_t key;            /* CORE_KEY_*: one the track type's lists have (the FX track has no OSC2) */
    int32_t page;           /* a page id: a mod's own (core_pages), or a stock one */
    int32_t (*shown)(void *view);   /* 0: always; else non-0 while the key may step to it */
};

#endif
