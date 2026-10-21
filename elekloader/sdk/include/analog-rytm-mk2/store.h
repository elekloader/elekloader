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
 * Zero is always the default: a field reads 0 in a new project, in one saved
 * by another layout, and after its scope was reset. Both calls mask
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
#define STORE_OFFSET(f) ((f) & 0x3fffff)      /* the first bit, most significant first */

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

unsigned core_store_get(unsigned field, void *item, int index);
void core_store_set(unsigned field, void *item, int index, unsigned value);

#endif
