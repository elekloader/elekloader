/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.8: lockable mod parameters (docs/ADAPTING.md, "Lockable
 * parameters (core-ar2 0.8, Analog Rytm MKII)").
 *
 * A sound-scope storage field declared with "lock": true gets a lock code
 * (42..127) from the build's lockfile. A mod registers it once, through the
 * table core_lock_params:
 *
 *     "storage": [{"sym": "my_depth", "scope": "sound", "bits": 7, "lock": true}],
 *     "contribute": [
 *       {"to": "core_lock_params", "order": 50, "data": "00000000",
 *        "relocs": [[0, "abs32", "sym:my_depth_param", 0]]}]
 *
 * and in C:
 *
 *     STORE_FIELD(my_depth);
 *     const struct core_lock_param my_depth_param =
 *         {STORE(my_depth), 449, 0, 127, "DEP", "My Depth", CORE_LOCK_CATEGORY("MINE")};
 *
 * Core then lists the parameter in each sound's LFO destination menu (after
 * stock's, as category:name, "MOD" when the category is 0), names it there
 * and in the DST value, and keeps the LFO's offset
 * for it, which the mod reads with core_lfo_offset and adds to the value it
 * uses. With id, the page knob that shows the parameter (core_params),
 * RANDOMIZE PAGE gives the knob a value in min..max and RELOAD PAGE takes it
 * back from the saved kit, as they do stock knobs.
 *
 * core-ar2 0.9: a kit-scope field can be lockable too. It is the FX track's
 * (track 12) parameter: listed in the FX LFO's destination menu, p-locked on
 * the FX track, locked in scenes and performances there, on MIDI on the FX
 * track's channel, and read with track 12 in the calls below. It cannot be a
 * velocity or aftertouch destination ("mod"): the FX track has no routes.
 */
#ifndef ELEKLOADER_AR2_LOCK_H
#define ELEKLOADER_AR2_LOCK_H

#include <stdint.h>

struct core_lock_param {
    unsigned field;             /* STORE(sym): a lockable sound field (a drum track's) or
                                 * kit field (the FX track's) */
    int32_t id;                 /* its page knob's id, or 0 */
    int16_t min, max;           /* the values the field takes (locks: to 16383) */
    const char *short_name;     /* the DST value and the knob's label: up to 4 characters */
    const char *name;           /* the destination menu and the knob's pop-up: up to 32, in
                                 * proper case as stock's */
    const char *category;       /* before the colon in the menu: up to 4 characters, upper
                                 * case as stock's (SAMP, FILT, AMP), or 0 for MOD; (0.15)
                                 * stock's own puts it after that category's entries */
};

/* core-ar2 0.11: a lockable parameter narrower than the dial, shown on its
 * scale. A mod whose core_params get answers a knob on the dial's 0..127
 * (lfo-rnd's 4-bit fields, say) contributes an entry for the knob's id to
 * core_param_scales:
 *
 *     {"to": "core_param_scales", "order": 50, "data": "00000000",
 *      "relocs": [[0, "abs32", "sym:my_scale", 0]]}
 *
 *     const struct core_param_scale my_scale = {449, 450, my_show, my_unshow, 0};
 *
 * show(id, field value) answers the dial's 8.8 value, unshow(id, dial 8.8)
 * the field's value; with both 0 core uses its linear map,
 * round((v - min) * 127 / (max - min)) and back to the nearest field value
 * (none for a field 127 wide or wider). Core then converts where a value
 * meets the page: a held trig's lock (get, set and turn, a turn still a
 * whole field step at least), a held scene's lock, the NRPN or CC in and
 * PARAM OUTPUT, and core's number text (the field's value); a performance's
 * depth reaches the page in dial steps (show(min + d) - show(min)), and core
 * writes its text as a signed number unless flags has CORE_SCALE_DEPTHS.
 * Stock's copy and paste of locks keep field values and need nothing. A
 * parameter without an entry is unchanged. */
struct core_param_scale {
    int32_t first, last;                /* the knob ids it answers for */
    int32_t (*show)(int32_t id, int32_t value);
    int32_t (*unshow)(int32_t id, int32_t dial);
    uint32_t flags;
};
#define CORE_SCALE_DEPTHS 1             /* the mod writes a performance depth's text */

/* Core hands stock at most 4 characters of a short name or a category and
 * 32 of a name, so no length a mod gives can overrun stock's buffers. A
 * category written with this is refused by the compiler when it is longer
 * than 4 characters. */
#define CORE_LOCK_CATEGORY(s) ((s) + 0 * sizeof(char[sizeof(s) <= 5 ? 1 : -1]))

/* the field's lock code (42..127), or 0 when it has none */
int core_lock_code(unsigned field);

/* the offset track's LFO (0..12; for a kit field the FX LFO, track 12, 0.9)
 * adds to the field this audio block, in 8.8 steps of a stock 0..127 knob
 * (one step is 256), or 0 when the LFO's destination is another parameter. Updated in the audio interrupt, before
 * ev_render_out; the mod scales it to its own range. */
int core_lfo_offset(int track, unsigned field);

/* core-ar2 0.8: the field's p-lock on the trig track (0..12) last played,
 * in min..max, or -1 when that trig has none. Set with the trig's other locks,
 * before it sounds; the mod uses it in place of the sound's value for that
 * trig. A p-lock is set like a stock one: a trig held, the parameter's knob
 * (its id) turned. It is a lock row in the pattern, coded by the field's
 * lock code, so it shares the pattern's 72 rows with stock's (71 used at
 * most, stock's first); a parameter whose max is above 127 takes two rows
 * per track and keeps its whole value. */
int core_lock_value(int track, unsigned field);

/* core-ar2 0.8: the field's lock in the active scene of the kit track
 * (0..11; 12 for a kit field, 0.9) plays, in min..max, or -1 when the scene has none (or no scene is
 * active). A scene lock is set like a stock one: the scene's pad held in
 * scene edit, the parameter's knob turned. It is an entry with the field's
 * lock code in the kit's 48 scene locks, shared with stock's. Above 127 it
 * moves in coarse steps (max shifted right into 0..127), as the kit's file
 * keeps only a lock's whole step. The mod uses it in place of the sound's
 * value while the scene is active. */
int core_scene_value(int track, unsigned field);

/* core-ar2 0.8: the offset the performances give the field on track (0..11;
 * 12 for a kit field, 0.9) now, in 8.8 steps of the field (one step is 256): each performance's depth
 * for it times its pad's pressure, as stock adds a performance's to a stock
 * parameter. The depth is set like a stock one, the performance's pad held
 * in its edit and the knob turned; it is an entry in the kit's 48
 * performance locks, in coarse steps above 127 (as core_scene_value). The
 * mod adds it to the value it uses, as it adds core_lfo_offset. */
int core_perf_offset(int track, unsigned field);

/* core-ar2 0.9: the offset the sound's velocity and aftertouch routes give
 * the field on drum track (0..11) now, in 8.8 steps of a stock 0..127 knob
 * (one step is 256), or 0 when no route is to it: each route's amount times
 * the trig's velocity or the pressure, as stock adds a route's to a stock
 * parameter. A field declared "mod": true (and "lock": true) is listed at
 * the end of each sound's VELOCITY MOD and AFTERTOUCH lists; a route to it
 * keeps the field's lock code in its own byte. Audio side, like
 * core_lfo_offset; the mod scales it to its own range and adds it. */
int core_mod_offset(int track, unsigned field);

/* core-ar2 0.9: the value stock would play for the lockable field on its
 * track (a sound field on a drum track, 0..11; a kit field on the FX track, 12) now, in 8.8 steps of the field (one step is 256), clamped to
 * min..max. Its target is the sound's (the kit's) value in the kit the audio side plays,
 * the trig's p-lock (core_lock_value) in its place, and the active scene's
 * lock (core_scene_value) in place of that; the target is smoothed with
 * stock's own one-pole (90% of a step in 50 ms, 99% in 100 ms), and the
 * performances (core_perf_offset), the velocity and aftertouch routes
 * (core_mod_offset) and the LFO (core_lfo_offset; these two scaled from a
 * 0..127 knob's steps to min..max) are added after it, unsmoothed, as stock
 * adds them to its own parameters. A field declared "smooth": false jumps to
 * its target. Audio side only (ev_render_in, ev_render_out, a mod's engine
 * hook): the call steps the smoother, lazily, to the current block. A field
 * on the other kind of track (a sound field on 12, a kit field on 0..11) and
 * a field with no lock code give CORE_PARAM_NONE; on the FX track the routes
 * add nothing. */
#define CORE_PARAM_NONE ((int)0x80000000)
int core_param_value(int track, unsigned field);

/* core-ar2 0.9: core_param_value with flags. CORE_PARAM_LFO_RAW adds the
 * LFO's offset as core_lfo_offset gives it (8.8 steps of a 0..127 knob), not
 * scaled to min..max, for a mod that wants a stock knob's swing. */
#define CORE_PARAM_LFO_RAW 1
/* and CORE_PARAM_MOD_RAW the velocity and aftertouch routes' offset as
 * core_mod_offset gives it */
#define CORE_PARAM_MOD_RAW 2
int core_param_value_ex(int track, unsigned field, unsigned flags);

/* core-ar2 0.9: core_param_value rounded to whole steps (CORE_PARAM_NONE as
 * it is) */
int core_param_int(int track, unsigned field);

/* core-ar2 0.10: a field's handle, looked up once (at boot, or on a mod's
 * first read; 0 for a field with no lock code), and core_param_value_ex by
 * the handle, which skips the field's lookup: for a mod that reads its
 * parameters every audio block. The same values as by the field. */
int core_param_handle(unsigned field);
int core_param_value_h(int track, int handle, unsigned flags);

/* core-ar2 0.10: n parameters of one track by their handles, into out[0..n-1]
 * (CORE_PARAM_NONE for one not on the track): the same values as n calls of
 * core_param_value_h, with the track's checks, the block's state and the
 * sound or kit found once. For a mod that reads several parameters of a
 * track every block. A handle is a lock code of this build's layout: look it
 * up at boot or on the first read, and never keep one in a project. */
void core_param_values(int track, const int *handles, int n, unsigned flags, int *out);

#endif
