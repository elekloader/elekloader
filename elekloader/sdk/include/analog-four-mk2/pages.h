/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2 0.2, 0.3: parameter pages and parameter overrides (docs/ADAPTING.md,
 * "Parameter pages (core-a4mk2 0.2, Analog Four MKII)"): core-ar2 0.4's
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
 * From core-a4mk2 0.3 (core-ar2 0.6's) a page whose knobs change with the
 * sound (a knob that means nothing for the selected type left empty)
 * contributes to core_page_fns instead: page_info asks info(page) on each
 * call, before core_pages, and answers the entry it returns; 0 passes.
 *
 *     {"to": "core_page_fns", "order": 50, "data": "00000000",
 *      "relocs": [[0, "abs32", "sym:my_page_fn", 0]]}
 *
 *     static const struct core_page *my_info(int32_t page)
 *     { return selected_type() ? &my_page : &my_page_short; }
 *     const struct core_page_fn my_page_fn = {50, my_info};
 *
 * The knobs' draw, a knob's touch and a turn each look the knob's id up
 * through page_info afresh, so the knobs follow at the next redraw. The
 * page's view reads the name once, when core makes it at boot: keep the
 * same name in every entry info returns.
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

/* core-a4mk2 0.3: the entry page_info answers for page, asked on each call */
struct core_page_fn {
    int32_t page;
    const struct core_page *(*info)(int32_t page);   /* 0: pass */
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

/* core-a4mk2 0.6: knob looks (core-ar2 0.13's). A mod's own id (above 0x126)
 * has no stock records, so stock draws its knob as record 0's dial on
 * record 0's range. An entry in core_knob_looks gives the ids the look of
 * any stock knob, or a UI record of the mod's own:
 *
 *     {"to": "core_knob_looks", "order": 50, "data": "00000000",
 *      "relocs": [[0, "abs32", "sym:my_looks", 0]]}
 *
 *     const struct core_knob_look my_looks = {449, 449, 23, 0, 0, 0};
 *
 * look is a stock id (1..0x126): the knob is drawn as that id's knob, its
 * graphic, the parts its flags draw, its turn's steps, its pop-ups, and its
 * range, which min and max replace when min < max (8.8, as the parameter
 * record's); then the graphic also spans min..max (core moves the value onto
 * look's range for it). ui, when not 0, answers an 84-byte UI record of the
 * mod's own for the id (stock's layout, as the Rytm's: flags +0, steps
 * +4..+0x10, delegates from +0x14), or 0 for look's; a mod builds one by
 * copying a stock record, core_knob_stock_ui(id), and changing fields, once
 * stock has built its records (any draw: not in ev_boot). A pointer it
 * returns is read from %a0 (asm: return it in %a0 and %d0). The label, the
 * name, the value and its text stay the mod's, and nothing else of look's
 * parameter (its sound slot, its lists, MIDI) comes along. An id an entry
 * doesn't cover draws as record 0; an entry's ids at or below 0x126 are
 * stock's own and keep their look. (mods/core-a4mk2/README.md, "Knob looks
 * (0.6)") */
struct core_knob_look {
    int32_t first, last;    /* the ids this entry answers for (above 0x126) */
    int32_t look;           /* the stock id whose knob these look like, 1..0x126 (0: record 0) */
    const void *(*ui)(int32_t id);  /* an 84-byte UI record of the mod's own, or 0 for look's */
    int32_t min, max;       /* min < max: the range (8.8) in place of look's, the graphic's too */
};

/* the stock UI record of id (0..0x126; record 0 for any other id), to copy */
const void *core_knob_stock_ui(int32_t id);

#endif
