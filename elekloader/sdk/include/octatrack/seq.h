/* SPDX-License-Identifier: GPL-2.0-or-later */
/* seq-events (mods/seq-events-ot) for the Octatrack: its sizes, then the API every device shares
 * (seq-events.h; docs/ADAPTING.md, "Step-fire events"):
 *
 *     #include "octatrack/seq.h"
 *
 * Tracks 0-7 are the audio tracks T1-T8, 8-15 the MIDI tracks M1-M8. An audio track plays a
 * sample, not a note: ev_gen_notes, ev_fire_notes and ev_fire_vel run on the MIDI tracks only,
 * and an audio track's lock slots are the page-1 knobs (below). The Octatrack keeps no keyboard
 * key: sq_track_info gives SQ_ROOT_NONE and SQ_SCALE_NONE. */
#ifndef ELEKLOADER_OCTATRACK_SEQ_H
#define ELEKLOADER_OCTATRACK_SEQ_H

#define SQ_AUDIO  8                     /* tracks 0-7: T1-T8 */
#define SQ_TRACKS 16                    /* then 8-15: M1-M8 */
#define SQ_STEPS  64
#define SQ_NOTES  4                     /* a MIDI track's notes: NOTE and NOT2-NOT4 as intervals */
#define SQ_SLOTS  30                    /* a step's lock bytes, 0 to SQ_SLOTS - 1: the page-1 knobs
                                         * in order, PLAYBACK 0-5, LFO 6-11, AMP 12-17 (VOL is 15),
                                         * FX1 18-23, FX2 24-29; value >> 8 is the byte, 0-127 (a
                                         * bipolar knob stores its panel value + 64) */

#include "seq-events.h"

#endif
