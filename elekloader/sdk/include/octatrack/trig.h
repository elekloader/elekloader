/* SPDX-License-Identifier: GPL-2.0-or-later */
/* trig-events (mods/trig-events-ot) for the Octatrack, OS 1.40C: the machine trig dispatch in the
 * frame interrupt, as the event ev_track_trig (docs/ADAPTING.md, "Trig events"):
 *
 *     #include "octatrack/trig.h"
 *
 * with "requires": ["core", "trig-events"] and "subscribe": [{"event": "ev_track_trig", "fn": "...",
 * "order": 50}]. */
#ifndef ELEKLOADER_OCTATRACK_TRIG_H
#define ELEKLOADER_OCTATRACK_TRIG_H

#include <stdint.h>

#define OT_TRIG_TRACKS 8                /* the audio tracks T1-T8, 0-7: the MIDI tracks never come here */

/* ev_track_trig: the frame interrupt (level 5, every 16 samples) is about to hand track's trig to
 * its machine, which would start the voice. -> nonzero vetoes: the machine is not called and the
 * trig is gone, as a Digitakt's trig mute has it (a voice already sounding rings on under its own
 * envelope); nothing after it in the table sees the trig, and the interrupt goes on as stock.
 * 0 leaves it to the next handler, and at last to the machine.
 *
 * Runs at interrupt level 5, inside the interrupt's loop over the tracks, for every trig that
 * reaches a machine: a trigger trig and an audio track's one-shot trig (a trigless or lock trig
 * sets no trig bit and never gets here). Keep it to a few dozen instructions, call no firmware
 * routine, wait for nothing, and leave the EMAC (MACSR and the accumulators) as you find it: the
 * machines' functions after it count on its mode. Handlers keep d2-d7/a2-a6 as the C convention
 * has it. An observer that must see the trigs another handler vetoes takes a smaller "order" than
 * it. */
typedef int32_t (*ev_track_trig_fn)(int32_t track);

/* What the dispatch reads, for a handler that wants the trig's flags or its machine. Each location
 * is one the stock interrupt reads as an absolute address (tests/test_octatrack.py checks them in
 * the image). */
#define OT_TRIG_FLAGS   ((volatile uint8_t *)0x46104d0c)     /* a byte a track: bit 4, a trig is to be
                                                              * handed to the machine (cleared at the
                                                              * loop's end); bit 5 goes to it as its flag */
#define OT_FRAME_SIDE   (*(volatile uint32_t *)0x800000e0)   /* 0 or 1: which of the two frame
                                                              * records the interrupt is building */
#define OT_FRAME_KIND   ((volatile int8_t *)0x80000eb4)      /* a byte a track and side: the machine
                                                              * the dispatch looks up in its table
                                                              * (0x400d6454), [8 * side + track] */

/* the machine the dispatch is about to call for track (a handler's argument) */
static inline int32_t ot_trig_machine(int32_t track)
{
    return OT_FRAME_KIND[8 * OT_FRAME_SIDE + track];
}

#endif
