/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.11: moments and getters (docs/ADAPTING.md, "Moments
 * (core-ar2 0.11, Analog Rytm MKII)"). Events are subscribed in mod.json,
 * {"event": "ev_kit", "fn": "my_kit"}; calls are core's exports.
 */
#ifndef ELEKLOADER_AR2_MOMENTS_H
#define ELEKLOADER_AR2_MOMENTS_H

#include <stdint.h>

/* ev_sysex_in: int32_t f(const unsigned char *data, int32_t len, int32_t iface),
 * MIDI task, for every SysEx message in (from F0, not the universal F0 7E
 * ones) before stock and core's own pattern dump in. Return nonzero to take
 * it: no later handler, nor core, nor stock sees it. Keep it short: it runs
 * in the MIDI task, between messages. */

/* a whole MIDI message (a status byte first; a SysEx F0 .. F7 in one call)
 * out of USB, or nothing: 1 sent (or queued, from an interrupt), 0 not now
 * (USB not configured, a stock SysEx under way, stock's sender busy or its
 * buffer too full). It never waits, and messages leave in the order sent.
 * From a task it goes through stock's sender under its mutex. From an
 * interrupt it goes out at once (at level 5) when no task is in the sender
 * and nothing waits before it; otherwise into core's ring of 1024 bytes, at
 * most 128 a message, which the next sequencer tick or ev_tick sends. */
int core_midi_try_send(int len, const unsigned char *msg);
/* 1 while USB is on and the host has configured the MIDI port, else 0 (the
 * first thing core_midi_try_send checks). Any task or interrupt. */
int core_usb_midi_up(void);

/* the UI's current kit (0..127), track (0..12, 12 the FX track) and pattern
 * (0..127), -1 for none; and their items for core_store_get/set: the live
 * kit, the current track's sound in it (0 on the FX track), the live
 * pattern (a track field's index is the track). UI task. */
int core_current_kit(void);
void *core_current_kit_item(void);
int core_current_track(void);
void *core_current_sound_item(void);
int core_current_pattern(void);
void *core_current_pattern_item(void);
/* live kit k (as edited) and live pattern p, 0..127 (0 out of range): the
 * items core_handle_fields passes for them, so a handle can be matched to a
 * kit or pattern by number. Any task. */
void *core_kit_item(int k);
void *core_pattern_item(int p);

/* the live project's name: 16 bytes, not always zero-terminated. At
 * ev_project_save's pre 1 of a SAVE AS it is already the new name. */
const char *core_project_name(void);

/* ev_kit: void f(int32_t kit, int32_t how), at the next ev_tick (UI task)
 *   CORE_KIT_SELECTED  the UI's current kit is now kit (the pattern's kit,
 *                      KIT menu LOAD, project load, CREATE NEW, boot); the
 *                      engine moves to it track by track, at each track's
 *                      next trig. A change of kit while stopped drops the
 *                      tracks' playing-trig mod locks (core_lock_value -1).
 *   CORE_KIT_PUSHED    kit's whole data went to the engine (RELOAD KIT,
 *                      project load, CREATE NEW, boot, a SysEx kit in);
 *                      kit -1 for data in no kit slot.
 * Several pushes of one kit since the last tick are one event; a select comes
 * before the pushes of the same tick. */
#define CORE_KIT_SELECTED 1
#define CORE_KIT_PUSHED 2

/* ev_project_slot: void f(int32_t slot, uint32_t how), at the next ev_tick,
 * for a +Drive slot written outside a project save (ev_project_save):
 *   CORE_SLOT_CLEARED  slot now holds a new, empty project: the project
 *                      manager's CLEAR or INIT NEW (or Transfer's clear);
 *                      CLEAR of the active slot leaves the live project
 *   CORE_SLOT_INIT     the live project initialized in place through the
 *                      slot clear (Transfer's, slot -1): ev_project 0 came
 *                      first; slot is the slot that was active, 255 for
 *                      none. LOAD PROJECT's CREATE NEW gives no slot event
 *   CORE_SLOT_PASTED   a copied project pasted into slot
 * with CORE_SLOT_FAILED set when stock reported a failure. */
#define CORE_SLOT_CLEARED 1
#define CORE_SLOT_INIT 2
#define CORE_SLOT_PASTED 3
#define CORE_SLOT_FAILED 0x10000
#define CORE_SLOT_KIND(how) ((how) & 255)

/* every handle field's value in the live project ("handle": true in a
 * storage field's declaration; 0 is none): fn(field, item, index, value)
 * for each item whose handle is not 0, with core_store_get's item and index:
 * the 128 kits as edited, then as last saved (with their sounds), the 128
 * pool sounds, the 4 global slots, the 128 patterns and their tracks, the
 * project. UI task. */
void core_handle_fields(void (*fn)(unsigned field, void *item, int index, unsigned value));

#endif
