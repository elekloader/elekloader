/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2: core-ar2 0.11's moments on the Analog Four MKII (README,
 * "Moments"). Events are subscribed in mod.json; calls are core's
 * exports.
 */
#ifndef ELEKLOADER_A4MK2_MOMENTS_H
#define ELEKLOADER_A4MK2_MOMENTS_H

#include <stdint.h>

/* a whole MIDI message (a status byte first; a SysEx F0 .. F7 in one call)
 * out of USB, or nothing: 1 sent (or queued, from an interrupt), 0 not now
 * (USB not configured, a stock SysEx under way, stock's sender busy or its
 * buffer too full). It never waits, and messages leave in the order sent.
 * From a task it goes through stock's sender under its mutex. From an
 * interrupt it goes out at once (every interrupt masked for the send) when
 * no task is in the sender and nothing waits before it; otherwise into
 * core's ring of 1024 bytes, at most 128 a message, which the next
 * sequencer tick or ev_tick sends. */
int core_midi_try_send(int len, const unsigned char *msg);
/* 1 while USB is on and the host has configured the MIDI port, else 0 (the
 * first thing core_midi_try_send checks). Any task or interrupt. */
int core_usb_midi_up(void);

/* the live project's name: 16 bytes, not always zero-terminated. Any task. */
const char *core_project_name(void);

/* every handle field's value in the live project ("handle": true in a
 * storage field's declaration; 0 is none): fn(field, item, index, value)
 * for each item whose handle is not 0, with core_store_get's item and index:
 * the 128 kits as edited, then as last saved (each with its 4 sounds), the
 * 4 global slots, the 128 patterns and their 6 tracks, the project. UI
 * task. */
void core_handle_fields(void (*fn)(unsigned field, void *item, int index, unsigned value));

/* the UI's current kit (0..127), track (0..5: 0..3 the synth tracks, 4 the
 * FX track, 5 the CV track) and pattern (0..127), -1 for none; and their
 * items for core_store_get/set: the live kit, the current track's sound in
 * it (0 on the FX and CV tracks), the live pattern (a track field's index
 * is the track). UI task. */
int core_current_kit(void);
void *core_current_kit_item(void);
int core_current_track(void);
void *core_current_sound_item(void);
int core_current_pattern(void);
void *core_current_pattern_item(void);
/* live kit k (as edited) and live pattern p, 0..127 (0 out of range): the
 * items core_handle_fields passes for them. Any task. */
void *core_kit_item(int k);
void *core_pattern_item(int p);

/* ev_kit: void f(int32_t kit, int32_t how), at the next ev_tick (UI task)
 *   CORE_KIT_SELECTED  the UI's current kit is now kit (a pattern of
 *                      another kit, KIT menu LOAD, project load);
 *   CORE_KIT_PUSHED    kit's whole data went to the engine (RELOAD KIT,
 *                      project load, CREATE NEW, a SysEx kit in, a kit
 *                      change); kit -1 for data in no kit slot.
 * Several pushes of one kit since the last tick are one event; a select
 * comes before the pushes of the same tick. A select keeps the playing
 * trigs' mod locks (core-ar2's stopped kit change drops them); a kit load
 * resets them. */
#define CORE_KIT_SELECTED 1
#define CORE_KIT_PUSHED 2

#endif
