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

/* core-ar2 0.11: locks a mod keeps itself. A knob whose id is in an entry's
 * range, from CORE_PARAM_LOCK_FIRST up: with a trig held, stock's
 * lock calls for it come here, on the current track (0..11, 12 on the FX
 * track) and the held step, once for each held trig; core keeps nothing.
 * Contributed to core_param_locks:
 *
 *     {"to": "core_param_locks", "order": 50, "data": "00000000",
 *      "relocs": [[0, "abs32", "sym:my_locks", 0]]}
 *
 *     const struct core_param_lock my_locks = {560, 573, my_get, my_turn, my_set, my_clear,
 *                                              my_label, my_name};
 *
 * A function left 0 answers "none" (get, turn: -1; set, clear: 0; label,
 * name: stock's). Called in the UI task. A turn that answers a lock marks the
 * project modified. What stock does, seen in the emulator:
 *   - a turn's first call for a step without a lock is set, with the value
 *     the knob has without one; the turns after it are turn. So a mod that
 *     keeps locks needs set.
 *   - get runs for every knob of the page each time it is drawn with a trig
 *     held (50 to 170 calls a hold): answer from RAM.
 *   - live recording (a knob turned with no trig held while it records,
 *     after a core_params entry took the turn): turn gets the step the
 *     track plays with CORE_LOCK_RECORDING set in step, once for each turn;
 *     core has added a lock trig there as it does for its own locks.
 * The linker refuses an entry here or in core_params whose ids take
 * 469..554, one here below 555, and two mods' entries sharing an id from 555
 * up (the profile's param_ids). */
/* the first id a core_param_locks entry may use: stock's records end at
 * 468 and core's lockable parameters take pseudo ids 469..554 (469 + code
 * - 42, codes 42..127, lock.c) */
#define CORE_PARAM_LOCK_FIRST 555
#define CORE_LOCK_RECORDING 0x100   /* in step: live recording; the step is step & 0xff */

/* core-ar2 0.12: knob ids by count. A mod that declares "knobs": n in its
 * mod.json gets n ids in a row from 555 up, past every other mod's; the
 * linker defines __knobs, the mod's own, as the first. CORE_KNOB(k) is knob
 * k's id, 0..n-1, a constant in a const table (an abs32 relocation):
 *
 *     CORE_KNOBS;
 *     const struct core_page my_page = {14, "MY PAGE", {CORE_KNOB(0), CORE_KNOB(1)}};
 *     const struct core_param_lock my_locks = {CORE_KNOB(0), CORE_KNOB(1), ...};
 *
 * The ids can differ from one build to the next with other mods, so keep a
 * knob's values by id - CORE_KNOB(0), never by the id. */
#define CORE_KNOBS extern char __knobs[]
#define CORE_KNOB(k) ((int32_t)__knobs + (k))

/* core-ar2 0.12: knob looks. A mod's own id (555 up) has no stock records,
 * so stock draws its knob as record 0's dial on record 0's range. An entry
 * in core_knob_looks gives the ids the look of any stock knob, or a UI
 * record of the mod's own:
 *
 *     {"to": "core_knob_looks", "order": 50, "data": "00000000",
 *      "relocs": [[0, "abs32", "sym:my_looks", 0]]}
 *
 *     const struct core_knob_look my_looks = {CORE_KNOB(0), CORE_KNOB(0), 457, 0, 0, 0};
 *
 * look is a stock id (1..468): the knob is drawn as that id's knob, its
 * graphic (dial, switch, select box, bipolar dial, ...), the parts its flags
 * draw, its turn's steps, its pop-ups, and its range, which min and max
 * replace when min < max (8.8, as the parameter record's); then the graphic
 * also spans min..max (core moves the value onto look's range for it). ui, when not 0,
 * answers an 84-byte UI record of the mod's own for the id (stock's layout:
 * flags +0, steps +4..+0x10, delegates +0x14 text, +0x24 graphic, +0x34,
 * +0x44 pop-up), or 0 for look's; a mod builds one by copying a stock record,
 * core_knob_stock_ui(id), and changing fields, once stock has built its
 * records (any draw: not in ev_boot). Like a label, a pointer it returns is
 * read from %a0 (asm: return it in %a0 and %d0). The label and the name
 * stay core_param_locks', the value and its text core_params', and nothing
 * else of look's parameter (its sound slot, its lists, MIDI) comes along.
 * The linker refuses an entry below 555 and two mods' entries sharing an
 * id, as for core_param_locks. */
struct core_knob_look {
    int32_t first, last;    /* the ids this entry answers for */
    int32_t look;           /* the stock id whose knob these look like, 1..468 (0: record 0) */
    const void *(*ui)(int32_t id);  /* an 84-byte UI record of the mod's own, or 0 for look's */
    int32_t min, max;       /* min < max: the range (8.8) in place of look's, the graphic's too */
};

/* the stock UI record of id (0..468; record 0 for any other id), to copy */
const void *core_knob_stock_ui(int32_t id);

struct core_param_lock {
    int32_t first, last;    /* the ids this entry answers for */
    /* step's lock of id on track, 8.8, or -1 for none (what the knob shows) */
    int (*get)(int32_t id, int32_t track, int32_t step);
    /* the knob turned by delta (8.8) with step's trig held: the new lock
     * (8.8), the first turn starting from the value without one; -1 none.
     * With CORE_LOCK_RECORDING in step, live recording (above) */
    int (*turn)(int32_t id, int32_t track, int32_t step, int32_t delta);
    /* step's lock set to value (8.8), as stock's paste and value entry do,
     * and a turn's first step: 1 */
    int (*set)(int32_t id, int32_t track, int32_t step, int32_t value);
    /* step's lock of id cleared: 1 */
    int (*clear)(int32_t id, int32_t track, int32_t step);
    /* the knob's label (up to 4 characters) and the header's name (up to
     * 32; the header shows the first 13), for any page that shows the id,
     * locks or not; 0 for stock's (which reads ERR / ERROR above 468) */
    const char *(*label)(int32_t id);
    const char *(*name)(int32_t id);
};

/* the trig held while a knob of an id in core_param_locks is drawn: its
 * step, and its track in *track (may be 0); -1 when none. For a core_params
 * text entry, which gets the lock's value but not the step: it is the step
 * of the last lock call (a page draws a knob's lock with get just before its
 * text) and lasts 4 ms. With no core_params text entry, an id here shows its
 * value as a number. */
int core_held_step(int32_t *track);

/* core-ar2 0.11: what the view's held pad is, for a mod drawing its own
 * knob: 0 none (or a trig: core_held_step), CORE_HELD_SCENE or
 * CORE_HELD_PERF, when the value the page passes is a performance's depth
 * (in dial steps for a parameter in core_param_scales, lock.h). UI task. */
#define CORE_HELD_SCENE 1
#define CORE_HELD_PERF 2
int core_held_kind(void *view);

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
