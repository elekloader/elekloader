/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4 0.5: lockable mod parameters and their p-locks (docs/ADAPTING.md,
 * "Lockable parameters (core-a4 0.5, Analog Four MKII)"), core-ar2's design
 * with the A4's track kinds.
 *
 * A storage field declared "lock": true gets a lock code from the build's
 * lockfile. A sound field is a parameter of the synth tracks (1..4), a kit
 * field of the FX track (5), or of the CV track (6) when it also declares
 * "track": "cv". Codes are 0x70..0x7f, numbered per kind, so a build has at
 * most 16 lockable parameters of each kind. A mod registers each one through
 * the table core_lock_params:
 *
 *     "storage": [{"sym": "my_depth", "scope": "sound", "bits": 7, "lock": true}],
 *     "contribute": [
 *       {"to": "core_lock_params", "order": 50, "data": "00000000",
 *        "relocs": [[0, "abs32", "sym:my_depth_param", 0]]}]
 *
 *     STORE_FIELD(my_depth);
 *     const struct core_lock_param my_depth_param =
 *         {STORE(my_depth), 449, 0, 127, "DEP", "My Depth", CORE_LOCK_CATEGORY("MINE")};
 *
 * id is the page knob that shows the parameter (core_params, pages.h): an id
 * stock does not have (above 0x126), unique among the parameters of its
 * kind. With a trig held, that knob sets, clears and shows the trig's p-lock,
 * as a stock knob does; in live recording a turn of it locks the step that
 * plays.
 */
#ifndef ELEKLOADER_A4_LOCK_H
#define ELEKLOADER_A4_LOCK_H

#include <stdint.h>

struct core_lock_param {
    unsigned field;             /* STORE(sym): a lockable sound or kit field */
    int32_t id;                 /* its page knob's id (above 0x126), or 0 */
    int16_t min, max;           /* the values the field takes (locks: to 16383) */
    const char *short_name;     /* up to 4 characters (destinations, from core-a4's part 2) */
    const char *name;           /* up to 32 */
    const char *category;       /* up to 4, or 0 for MOD */
};

#define CORE_LOCK_CATEGORY(s) ((s) + 0 * sizeof(char[sizeof(s) <= 5 ? 1 : -1]))

/* the field's lock code (0x70..0x7f), or 0 when it has none */
int core_lock_code(unsigned field);

/* the field's kind: 0 the synth tracks, 1 FX, 2 CV; -1 when it is not lockable */
int core_lock_kind(unsigned field);

/* the field's p-lock on the trig the track (0..5) last played, in
 * min..max, or -1 when that trig has none (or the field is another kind's).
 * Set at the trig's event accept in the audio interrupt, before it sounds,
 * with stock's own locks; the mod uses it in place of the sound's (the kit's)
 * value for that trig. A p-lock is a lock row in the pattern, coded by the
 * field's lock code, sharing the pattern's 128 rows with stock's (127 used at
 * most, stock's first); a parameter whose max is above 127 takes two rows per
 * track and keeps its whole value. On a poly track the last-played voice is
 * the one the track's last trig went to. */
int core_lock_value(int track, unsigned field);

/* the same for one voice (0..3 the synth voices, 4 the FX track's, 5 the
 * CV track's), by code: the voice's playing trig's lock, or -1 */
int core_lock_value_voice(int voice, int code);

/* Param Slide (a slide trig): the change the voice's lock of code makes from
 * its trig to the next trig or lock trig on the track, in the field's steps,
 * by stock's four rules (to and from the sound's value; none across a lock
 * trig); 0 when it does not slide. A set of the field (its knob, MIDI) ends
 * it, as a stock knob's turn ends a stock parameter's slide. The ramp itself
 * is in core_param_value (below). */
int core_slide_delta_voice(int voice, int code);

/* Destinations (core-a4 0.5, part 2). A lockable field also declared
 * "mod": true is a destination of every modulation source of its track: the
 * five routes, the four Dest knobs, the performance macros and CONTROL IN.
 * Its lists end with the field (its short_name, name and category show
 * there), and the route, Dest or macro keeps the field's lock code. Core
 * notes them when stock pushes a sound, for the value read (part 3). */
struct core_dst {
    uint8_t src;                /* 0..4 a route (velocity, bend, wheel, breath, aftertouch);
                                   5..12 a Dest, 5 + its place in the voice's Dest list (synth:
                                   EnvF A, B, Env2 A, B, LFO1 A, B, LFO2 A, B; FX: LFO1 A, B,
                                   LFO2 A, B; CV: ENV1 A, B, ENV2 A, B, LFO1 A, B, LFO2 A, B);
                                   a macro: 0..9, then 10, 11 CONTROL IN 1, 2 */
    uint8_t n;                  /* a route's place (0..4); a Dest's selector (its sound word,
                                   e.g. 0x44 EnvF Dest A); a macro target's track */
    uint8_t code;               /* the field's lock code */
    uint8_t pad;
    int16_t depth;              /* a route's or macro target's depth (signed, 0x4000 = +0.5) */
};

/* the voice's (0..5) modulations: *routes routes, then *dests Dests */
const struct core_dst *core_dst_voice(int voice, int *routes, int *dests);

/* every macro's targets on mod parameters */
const struct core_dst *core_dst_macros(int *n);

/* 1 when anything modulates one of the voice's mod parameters */
int core_dst_any(int voice);

/* The value read (core-a4 0.5, part 3): the value stock would play for a
 * lockable parameter now, in 8.8 steps of the field (one step is 256),
 * clamped to min..max, computed in stock's own stages and order (the audio
 * interrupt's), with a clamp after each, as stock computes a coarse
 * parameter of the same range: the field's value in the sound the voice
 * plays (on FX and CV the kit), the playing trig's p-lock in its place;
 * smoothed with stock's one-pole (90% of a step in 50 ms), jumping into a
 * p-lock at its trig and back out at the next unlocked trig, as stock's
 * parameters do; Param Slide's ramp; then, for a "mod" field, the routes
 * (velocity, bend, wheel, breath, aftertouch), the performance macros and
 * CONTROL IN, and LFO2, Env2, LFO1 and EnvF on their Dest A and B. A field
 * declared "smooth": false jumps to its target. The modulations' inputs are
 * the audio side's of the block, taken by core at ev_render_out before any
 * mod's handler: a read in ev_render_out sees this block's, one in
 * ev_render_in the last block's. Audio side only (ev_render_in,
 * ev_render_out, a mod's engine hook): a read steps the smoother, lazily,
 * to the current block.
 *
 * A track (0..3 synth, 4 FX, 5 CV) reads the voice its last trig went to;
 * on a poly track the last-played voice. core_param_values_voice reads one
 * voice (0..3 the synth voices, 4 the FX track's, 5 the CV track's). A field
 * of another kind than the track's, or with no lock code, gives
 * CORE_PARAM_NONE. */
#define CORE_PARAM_NONE ((int)0x80000000)
int core_param_value(int track, unsigned field);

/* with flags: CORE_PARAM_ENGINE gives the engine's value, 0..0x7fff (the
 * field's min..max as 0..0x7f00), the scale stock's own parameters move in */
#define CORE_PARAM_ENGINE 4
int core_param_value_ex(int track, unsigned field, unsigned flags);

/* core_param_value rounded to whole steps (CORE_PARAM_NONE as it is) */
int core_param_int(int track, unsigned field);

/* a field's handle, looked up once (0 for a field with no lock code): its
 * code and kind. A build's own numbering: look it up at boot or on the
 * first read, and never keep one in a project. */
int core_param_handle(unsigned field);
int core_param_value_h(int track, int handle, unsigned flags);

/* n parameters of one track or voice by their handles, into out[0..n-1]: the
 * same values as n single reads, with the voice, its sound and the block's
 * inputs found once. For a mod that reads several parameters every block. */
void core_param_values(int track, const int *handles, int n, unsigned flags, int *out);
void core_param_values_voice(int voice, const int *handles, int n, unsigned flags, int *out);

/* the voice a track's read uses (0..5), or -1 for no such track */
int core_param_voice(int track);

#endif
