/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.11: step tags and ev_steps (docs/ADAPTING.md, "Step tags
 * (core-ar2 0.11, Analog Rytm MKII)").
 *
 * A step tag is a mod's 16-bit handle on one step of one pattern (0..65534):
 * the index of the mod's own record for that step, kept wherever the mod
 * likes (for the Sidecar mods, on the Pi). Core moves it wherever stock moves
 * the step: a trig's copy and paste, a page, track or pattern copy, paste,
 * clear and undo, a trig shift, a pattern switch, RELOAD PATTERN; and it
 * goes where stock clears the step. A mod asks for tags once, through the
 * table core_step_owners:
 *
 *     {"to": "core_step_owners", "order": 50, "data": "00000000",
 *      "relocs": [[0, "abs32", "sym:my_owner", 0]]}
 *
 *     const struct core_step_owner my_owner = {"MINE"};
 *
 * and finds its owner number (0..7, the record's place in the table) with
 * core_step_owner(&my_owner). Each owner's table (214,656 bytes) is made
 * at the first ev_tick after boot (core_step_tables).
 *
 * Tags are RAM only: never in the pattern's lock rows, a saved project or a
 * SysEx dump. A mod keeps its own copy of each pattern's tags, follows
 * ev_steps to keep it current, and puts them back with core_step_tag_set at
 * ev_project (boot, project load, CREATE NEW) and on RELOAD.
 *
 * Patterns are 0..127 (A01..H16), tracks 0..12 (12 the FX track), steps
 * 0..63. Called in the UI task.
 */
#ifndef ELEKLOADER_AR2_STEPS_H
#define ELEKLOADER_AR2_STEPS_H

#include <stdint.h>

struct core_step_owner {
    const char *name;           /* for a readback; core does not show it */
};

#define CORE_STEP_OWNERS 8      /* at most; a ninth owner's number is -1 */

/* the owner's number, or -1 when the record isn't in core_step_owners */
int core_step_owner(const struct core_step_owner *owner);

/* the step's tag (0..65534), or -1 for none (or no table) */
int core_step_tag(int32_t pattern, int32_t track, int32_t step, int32_t owner);

/* the step's tag set to handle (0..65534), or cleared with -1; 1 when set.
 * No event follows: the mod knows what it set. */
int core_step_tag_set(int32_t pattern, int32_t track, int32_t step, int32_t owner, int32_t handle);

/* every tag of the owner: fn(pattern, track, step, handle) for each tagged
 * step of the 128 patterns, then for each copy of one outside them (stock's
 * pattern copy for RELOAD PATTERN, the clipboards, the undo buffers, a trig
 * shift's temporaries) with pattern -1 (and track and step -1 when the copy
 * is no pattern's). A handle a walk finds nowhere is free to reuse. */
void core_step_tags(int32_t owner, void (*fn)(int32_t pattern, int32_t track, int32_t step,
                                              int32_t handle));

/* the pattern the UI edits now (0..127), or -1 */
int core_step_pattern_now(void);

/* 1 once the tables are made, 0 before the first ev_tick (no tags then:
 * core_step_tag_set answers 0) */
int core_step_tables(void);

/* ev_steps(kind, pattern, track, step, n, owner), in the UI task at ev_tick,
 * after stock's change. A mod subscribes with
 * {"event": "ev_steps", "fn": "my_steps", "order": 50}.
 *
 * MOVED and DROPPED name the owner (0..7) and a run of n steps from (track,
 * step), counted across tracks in order (track * 64 + step), so a whole
 * pattern is (pattern, 0, 0, 832); one event can stand for several changes
 * in a row. RELOAD, LENGTH and RESYNC are for every owner (-1). */
#define CORE_STEPS_MOVED 1      /* tags came into these steps (a paste, an undo, a shift,
                                 * RELOAD PATTERN); a handle may now be on two steps: copy the
                                 * record before writing it */
#define CORE_STEPS_DROPPED 2    /* tags left these steps (a clear, an overwrite) */
#define CORE_STEPS_RELOAD 3     /* the patterns pattern .. pattern + n - 1 were loaded from their
                                 * rows (project load, a SysEx pattern in) and have no tags */
#define CORE_STEPS_LENGTH 4     /* a length or scale setting changed: step is the value the
                                 * setter was given, n which setting (below), track -1 for
                                 * the pattern's own */
#define CORE_STEPS_RESYNC 5     /* the queue (64) overflowed: walk the tags again */

#define CORE_STEPS_LEN_TRACK 0          /* a track's length */
#define CORE_STEPS_SPEED_TRACK 1        /* a track's speed */
#define CORE_STEPS_LEN_PATTERN 2        /* the pattern's (master) length */
#define CORE_STEPS_SPEED_PATTERN 3      /* the pattern's speed */
#define CORE_STEPS_SCALE_MODE 4         /* pattern (0) or per track (1) */
#define CORE_STEPS_LEN_CHANGE 5         /* the change length */

typedef void (*ev_steps_t)(int32_t kind, int32_t pattern, int32_t track, int32_t step, int32_t n,
                           int32_t owner);

#endif
