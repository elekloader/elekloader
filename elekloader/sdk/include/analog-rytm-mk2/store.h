/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.2 and 0.3: mod storage (docs/ADAPTING.md, "Mod storage
 * (core-ar2 0.2, Analog Rytm MKII)"). Declare each field in mod.json:
 *
 *     "storage": [{"sym": "my_accent", "scope": "track", "bits": 4}]
 *
 * and in C:
 *
 *     STORE_FIELD(my_accent);
 *     unsigned v = core_store_get(STORE(my_accent), live_pattern, track);
 *     core_store_set(STORE(my_accent), live_pattern, track, v + 1);
 *
 * A field reads its default where its bits are zero: in a new project, in one
 * saved by another layout, and after its scope was reset. The default is 0,
 * or from core-ar2 0.10 the field's "init" in mod.json ("init": 127): core
 * keeps value ^ init, so the value read and written is the field's own. Both
 * calls mask
 * interrupts while they read or write the field: use them from any context,
 * the audio interrupt too.
 *
 * item and index, by scope (room per item: sound 56 bits with core 0.3, 24
 * with 0.2; kit 64; global 40; track and pattern the store's split):
 *   sound    the live sound (a kit's or the pool's)     index unused
 *   kit      the live kit                               index unused
 *   global   unused (0)                                 the slot, 0..3
 *   track    the live pattern                           the track, 0..12
 *   pattern  the live pattern                           index unused
 *   project  unused (0)                                 index unused
 */
#ifndef ELEKLOADER_AR2_STORE_H
#define ELEKLOADER_AR2_STORE_H

/* a field's descriptor: elekloader defines the field's symbol as its value */
#define STORE_FIELD(sym) extern char sym[]
#define STORE(sym) ((unsigned)(sym))

#define STORE_SCOPE(f) ((f) >> 28)            /* 1 sound .. 6 project */
#define STORE_BITS(f) ((f) >> 22 & 63)        /* 1..32 */
#define STORE_INIT_SLOT(f) ((f) >> 16 & 63)   /* core-ar2 0.10: its slot in core_field_inits, 0 for none */
#define STORE_OFFSET(f) ((f) & 0xffff)        /* the first bit, most significant first */

#define STORE_SOUND 1
#define STORE_KIT 2
#define STORE_GLOBAL 3
#define STORE_TRACK 4
#define STORE_PATTERN 5
#define STORE_PROJECT 6

/* ev_project handlers: void f(int how, unsigned dropped), subscribed in
 * mod.json ({"event": "ev_project", "fn": ...}). how is CORE_PROJECT_NEW after
 * CREATE NEW or the project reset, CORE_PROJECT_LOADED after any project load
 * (at boot too); dropped has STORE_DROPPED(scope) set for each of
 * STORE_GLOBAL, STORE_TRACK, STORE_PATTERN and STORE_PROJECT that came in as
 * zeros (saved by another layout, or with no mods). */
#define CORE_PROJECT_NEW 0
#define CORE_PROJECT_LOADED 1
#define STORE_DROPPED(scope) (1u << (scope))

/* core-ar2 0.10: ev_project_save handlers: void f(unsigned pre, unsigned how),
 * subscribed in mod.json ({"event": "ev_project_save", "fn": ...}), called
 * around every whole save of the live project to a +Drive slot: pre 1 before
 * anything is written, pre 0 after. CORE_SAVE_KIND(how) is CORE_SAVE_SAME (a
 * save to the active slot: SAVE PROJECT), CORE_SAVE_OTHER (to another slot:
 * SAVE PROJECT AS, or a new project's first save) or CORE_SAVE_NEW (LOAD of
 * an empty slot, which writes the new project there after ev_project's
 * CORE_PROJECT_NEW); CORE_SAVE_SLOT(how) is the slot, 0..127 (the screen's\n * number less one; 255: none);
 * CORE_SAVE_FAILED is set in the call after when the slot was not written.
 * A field set with core_store_set in the call before is in the saved copy.
 * Both calls come from the job stock starts for the save ("Project Save", or
 * "Project Load" for CORE_SAVE_NEW), not from a UI tick: keep
 * them short, start no load or save, and leave slow work (MIDI, USB) to the
 * next ev_tick. Don't call core_project_edited from them: the save clears
 * the modified mark after the call before. */
#define CORE_SAVE_SAME 1
#define CORE_SAVE_OTHER 2
#define CORE_SAVE_NEW 3
#define CORE_SAVE_KIND(how) ((how) & 255)
#define CORE_SAVE_SLOT(how) ((how) >> 8 & 255)
#define CORE_SAVE_FAILED 0x10000u

unsigned core_store_get(unsigned field, void *item, int index);
void core_store_set(unsigned field, void *item, int index, unsigned value);

/* core-ar2 0.8: after a mod's own keys, encoders or settings change a field
 * of the live project, so that a project load asks first (ANY CHANGES WILL
 * BE LOST), as after a stock edit. Any context; the mark is made on the next
 * UI tick. Core calls it itself for a mod page's knob, a lockable
 * parameter's CC or NRPN, and RANDOMIZE and RELOAD PAGE. */
void core_project_edited(void);

#endif
