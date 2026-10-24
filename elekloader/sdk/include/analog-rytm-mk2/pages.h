/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.4: parameter pages and parameter overrides (docs/ADAPTING.md,
 * "Parameter pages (core-ar2 0.4, Analog Rytm MKII)"). Core owns the entries
 * of stock's four page routines (page_info, page_get_value,
 * param_apply_delta, param_value_text) and asks the mods through two tables,
 * contributed in mod.json:
 *
 *     "contribute": [
 *       {"to": "core_pages", "order": 14, "data": "00000000",
 *        "relocs": [[0, "abs32", "sym:my_page", 0]]},
 *       {"to": "core_params", "order": 50, "data": "00000000",
 *        "relocs": [[0, "abs32", "sym:my_params", 0]]}]
 *
 * and in C:
 *
 *     const struct core_page my_page = {14, "MY PAGE", {449, 450, 0, 0, 0, 0, 0, 0}};
 *     const struct core_param my_params = {449, 450, my_get, my_delta, my_text};
 *
 * Entries are asked in table order (by "order"); the first that answers wins.
 * A page id must be one stock does not have (stock's are 0..11) and no other
 * mod uses. A function left 0 passes.
 *
 * From core-ar2 0.6 a page whose knobs change with the sound (a knob that
 * means nothing for the selected type left empty) contributes to
 * core_page_fns instead: page_info asks info(page) on each call, before
 * core_pages, and answers the entry it returns; 0 passes.
 *
 *     {"to": "core_page_fns", "order": 14, "data": "00000000",
 *      "relocs": [[0, "abs32", "sym:my_page_fn", 0]]}
 *
 *     static const struct core_page *my_info(int32_t page)
 *     { return selected_type() ? &my_page : &my_page_short; }
 *     const struct core_page_fn my_page_fn = {14, my_info};
 *
 * Every reader of the page (the knobs' draw, the knob -> id lookup of a
 * turn) calls page_info afresh, so the knobs follow at the next redraw.
 *
 * The page key reaches the page through core_view_pages: its view, and the
 * page. Each view's list is stock's pages, then the mods' entries in order:
 *
 *     {"to": "core_view_pages", "order": 30, "data": "00000000",
 *      "relocs": [[0, "abs32", "sym:my_view_page", 0]]}
 *
 *     const struct core_view_page my_view_page = {CORE_VIEW_COMP, 14, 0};
 *
 * The page key steps over the page while shown, if set, answers 0, and puts
 * each page's title in the header. A view keeps its page when it is shown
 * again (stock's FX track went back to the first page).
 */
#ifndef ELEKLOADER_AR2_PAGES_H
#define ELEKLOADER_AR2_PAGES_H

#include <stdint.h>

/* page_info answers &page->name: the rest is stock's entry layout */
struct core_page {
    int32_t page;
    const char *name;
    int32_t ids[8];         /* knobs A..H, 0 for none */
};

/* core-ar2 0.6: the entry page_info answers for page, asked on each call */
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

/* the page keys' views (a voice track's five, then the FX track's) */
#define CORE_VIEW_SRC 0         /* page 0, the machine's */
#define CORE_VIEW_SAMPLE 1      /* pages 4 and 5 */
#define CORE_VIEW_FILTER 2      /* page 6 */
#define CORE_VIEW_AMP 3         /* page 7 */
#define CORE_VIEW_LFO 4         /* page 3 */
#define CORE_VIEW_DELAY 5       /* page 8 */
#define CORE_VIEW_REVERB 6      /* page 9 */
#define CORE_VIEW_DIST 7        /* page 10 */
#define CORE_VIEW_COMP 8        /* page 11 */
#define CORE_VIEW_FX_LFO 9      /* page 3, on the FX track */

struct core_view_page {
    int32_t view;           /* CORE_VIEW_* */
    int32_t page;           /* a page id: a mod's own (core_pages), or a stock one listed again */
    int32_t (*shown)(void *view);   /* 0: always; else non-0 while the key may step to it */
};

#endif
