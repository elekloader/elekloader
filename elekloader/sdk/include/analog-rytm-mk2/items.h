/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.11: remote fields and ev_items (docs/ADAPTING.md, "Remote
 * fields (core-ar2 0.11, Analog Rytm MKII)").
 *
 * A storage field declared "where": "sidecar" lives in no stock object:
 *
 *     "storage": [{"sym": "my_rec", "scope": "kit", "bits": 16, "where": "sidecar"}]
 *
 * Core keeps a RAM mirror of it for every place that holds a sound, kit or
 * pattern, read and written with core_store_get and core_store_set as any
 * field (item: the record, a live kit, sound or pattern object as for a
 * stock field, or any address core_item_addr gives). The mirror reads 0
 * until set, and after a power-off: the mod keeps the data itself (for the
 * Sidecar mods, on the Pi) and puts it back. Room per item: sound 64 bits,
 * kit 64, pattern 256, shared by the build's mods.
 *
 * Core moves the mirrors wherever stock moves the records (memcpy, memset,
 * the serializers, the inits), and tells the mods each move:
 *
 *     {"event": "ev_items", "fn": "my_items"}
 *     void my_items(int kind, unsigned from, unsigned to, int n);
 *
 * kind CORE_ITEM_SOUND, _KIT or _PATTERN; n records, the next n of from to
 * the next n of to (n 0: every record of the kind, from and to ..._ALL: a
 * whole project load, save or CREATE NEW). A kit's move carries its 12
 * sounds' mirrors, under the one KIT event. kind CORE_ITEM_RESYNC (0): the
 * queue overflowed, read the mirrors again. Called at ev_tick, in the UI
 * task, after the moves happened.
 *
 * from and to are refs: CORE_ITEM_WHERE(ref) and CORE_ITEM_INDEX(ref):
 *   NONE       from: a fresh record (an init, a clear); index 0
 *   LIVE       pattern 0..127 (A01..H16), CORE_ITEM_WORKING: the playing
 *              copy (object 0); kit 0..127 as edited; sound kit * 16 + track
 *   SAVED      kit 0..127 as last saved (what RELOAD reads); sound kit * 16 + track
 *   POOL       sound 0..127
 *   IMAGE      the stored project (what SAVE PROJECT writes to the +Drive):
 *              pattern 0..127 and WORKING, kit 0..127 and WORKING, sound kit * 16 + track
 *   IMAGE_POOL the stored project's pool sound 0..127
 *   OTHER      memory core follows (a clipboard, an undo buffer, a
 *              temporary): a serial; a kit's sound serial * 16 + track
 *   OUT        a record serialized off the project (SysEx out, Transfer,
 *              the +Drive's sound library): a serial; the same record
 *              coming back unchanged names it as from and brings its mirror
 *   EXT        from: a record from outside core knows nothing of (mirror 0)
 */
#ifndef ELEKLOADER_AR2_ITEMS_H
#define ELEKLOADER_AR2_ITEMS_H

#include <stdint.h>

#define CORE_ITEM_RESYNC 0
#define CORE_ITEM_SOUND 1
#define CORE_ITEM_KIT 2
#define CORE_ITEM_PATTERN 5

#define CORE_ITEM_NONE 0
#define CORE_ITEM_LIVE 1
#define CORE_ITEM_SAVED 2
#define CORE_ITEM_POOL 3
#define CORE_ITEM_IMAGE 4
#define CORE_ITEM_IMAGE_POOL 5
#define CORE_ITEM_OTHER 6
#define CORE_ITEM_OUT 7
#define CORE_ITEM_EXT 8

#define CORE_ITEM_REF(where, index) ((uint32_t)(where) << 24 | ((uint32_t)(index) & 0xffffff))
#define CORE_ITEM_WHERE(ref) ((ref) >> 24)
#define CORE_ITEM_INDEX(ref) ((ref) & 0xffffff)
#define CORE_ITEM_ALL 0xffffff
#define CORE_ITEM_WORKING 0xffff

/* the record a ref names (0: none, or no longer in memory) */
void *core_item_addr(int32_t kind, uint32_t ref);
/* the ref of the record at addr (0: no place core follows) */
uint32_t core_item_ref(int32_t kind, void *addr);

#endif
