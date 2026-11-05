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
 * comes with core_param_value (core-a4's part 3). */
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

#endif
