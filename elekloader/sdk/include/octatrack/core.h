/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * core-ot 0.5 for the Octatrack (OS 1.40C): what the core knows and exports, for C mods.
 * The SDK puts this folder on the include path:
 *
 *     #include "octatrack/core.h"
 *
 * - the firmware's current context (which track, Part and pattern the panel is on, which Part each
 *   track plays, whether the sequencer runs): fixed locations, read through the macros below;
 * - the transport events' arguments (docs/ADAPTING.md, the hook bus on the Octatrack);
 * - the frame budget meter, which the core keeps in every build;
 * - ot_page2_write: a page-2 knob's value into the Part, its copy, the engine and the dirty flags;
 * - ot_midi_send_paced: MIDI out that cannot hang the unit or flood a controller.
 *
 * Locations are for OS 1.40C; a mod that uses only these names rebuilds for another OS by the core's
 * port, not by a change here. Where octabam's docs/firmware says how a location was found, the
 * name is in the comment.
 */
#ifndef ELEKLOADER_OCTATRACK_CORE_H
#define ELEKLOADER_OCTATRACK_CORE_H

#include <stdint.h>

/* ---- the context --------------------------------------------------------------------------------
 * Two copies of the "where is the panel" state exist and the firmware keeps them equal: the UI's
 * (battery-backed SRAM at 0x100b14cc..) and the engine's (0x80000000..). The editors, the MIDI
 * thread and the sequencer read the engine's; the page resolver reads the UI's Part. */
#define OT_B(a)             (*(volatile uint8_t *)(uintptr_t)(a))
#define OT_L(a)             (*(volatile uint32_t *)(uintptr_t)(a))

#define OT_UI_TRACK         OT_B(0x100b14cc)    /* 0-7: the audio track the panel is on */
#define OT_UI_BANK          OT_B(0x100b14ce)    /* 0-15 */
#define OT_UI_PART          OT_B(0x100b14cf)    /* 0-3 */
#define OT_TRACK            OT_B(0x80000000)    /* the engine's copy of OT_UI_TRACK */
#define OT_BANK             OT_B(0x80000002)    /* the current bank, 0-15 */
#define OT_PART             OT_B(0x80000003)    /* the current Part, 0-3: the one page edits write to */
#define OT_PATTERN          OT_B(0x80000004)    /* the current pattern in the bank, 0-15 */

/* The Part and bank each track is playing (a pattern change queued for later has not reached
 * them; a Part edit goes to OT_PART, which may not be the one a track plays). 0xff for a track
 * the frame interrupt has not yet rewritten since the sequencer last switched patterns. */
#define OT_PLAYING_BANK(t)  OT_B(0x8000182a + (t))
#define OT_PLAYING_PART(t)  OT_B(0x80001832 + (t))

/* The sequencer's word. */
#define OT_PLAYING          OT_L(0x800065b8)
enum {
    OT_PLAYING_STOPPED = 0,
    OT_PLAYING_RUNNING = 1,
    OT_PLAYING_STOP    = 2,     /* what the STOP routine (the STOP key, MIDI stop) leaves it at */
};

/* ev_transport(what, playing): what the sequencer just did, and the word as stored. */
enum {
    OT_TRANSPORT_START  = 0,    /* the cold start (PLAY from stopped, MIDI start) */
    OT_TRANSPORT_RESUME = 1,    /* the start from where it stopped (PLAY after STOP, MIDI continue) */
    OT_TRANSPORT_STOP   = 2,    /* the STOP routine, at its head */
};

/* ---- the frame budget meter ---------------------------------------------------------------------
 * The core times every frame interrupt (2,756 a second, 16 samples) on DMA timer 3, a free-running
 * counter of the 132 MHz bus clock (the OS only reads it; it wraps in 32.5 s, so take differences).
 * A frame is 47,891 counts (362.8 us) at 44.1 kHz. A frame's interrupt takes from its first
 * instruction to its last, so it includes every mod's handlers and the DMA interrupt nested in it.
 * Read the fields in a task; ot_meter_reset() starts the maxima and counts again. */
struct ot_frame_meter {
    uint32_t frames;            /* frame interrupts measured since boot or the reset */
    uint32_t last;              /* the last one's length, in counts */
    uint32_t worst;             /* the longest since the reset */
    uint32_t overruns;          /* those longer than a frame (tpf): the next frame's interrupt ran late */
    uint32_t late;              /* frames that began more than 1.5 frames after the one before */
    uint32_t tpf;               /* counts a frame takes, measured over the last 1,024 frames */
    uint32_t total;             /* the sum of the lengths (wraps): total/frames is the mean */
    uint32_t private_[4];
};
extern volatile struct ot_frame_meter ot_meter;
#define OT_DTCN3            OT_L(0xfc07c00c)    /* the counter itself */
#define OT_FRAME_COUNTS     47891u              /* tpf until the first 1,024 frames are measured */
void ot_meter_reset(void);

/* ---- the Parts ----------------------------------------------------------------------------------
 * The current bank's RAM is OT_BANK_RAM; its four Parts (6,322 bytes each) start at +0x8ed80. In a
 * Part, track t's effect on FX1 is byte t, its effect on FX2 byte 8 + t, its machine (0 STATIC, 1 FLEX,
 * 2 THRU, 3 NEIGHBOR, 4 PICKUP) byte 0x22 + t. Effect 0 is NONE. */
#define OT_BANK_RAM         OT_L(0x46c82456)
#define OT_PART_SIZE        6322u
#define OT_PART_BASE(p)     ((volatile uint8_t *)(uintptr_t)(OT_BANK_RAM + 0x8ed80u + (p) * OT_PART_SIZE))
#define OT_PART_FX1(p, t)   OT_PART_BASE(p)[t]
#define OT_PART_FX2(p, t)   OT_PART_BASE(p)[8 + (t)]
#define OT_PART_MACHINE(p, t) OT_PART_BASE(p)[0x22 + (t)]

/* ---- page 2 -------------------------------------------------------------------------------------
 * Each effect, machine and AMP has a second page of six knobs (slot 0-5) that stock reaches only
 * through the panel. ot_page2_write makes the stores the panel's editor makes: the Part's byte (the
 * current Part, OT_PART), its battery-backed copy, the part-changed bits, the bank's and the
 * project's dirty flags, the engine's live lane (the engine ships it to the DSP every frame), and
 * the slot's redraw marker. Without the dirty flags the Part's byte is lost on a Part change, and
 * without the lane the sound does not change. The editor's other effect, a redraw of the page it is
 * on, is not made: a page that is on screen shows the new value when it next draws.
 *
 * The value is clamped to the knob's range, which the firmware keeps in the descriptor of the
 * track's effect or machine (page OT_P2_PLAYBACK: the machine, and its own six bytes in the Part).
 * An effect with no real page-2 knob has a placeholder descriptor that allows 0-127 in every slot,
 * so look at OT_PART_FX1/FX2 first when it matters (CC MAP skips NONE). Returns the value stored,
 * or OT_P2_REFUSED for a track above 7, a page above 3, a slot above 5, or a knob that has no
 * range. Call it from a task. */
enum {
    OT_P2_PLAYBACK = 0,         /* the SRC page: the track's machine decides what the six are */
    OT_P2_AMP      = 1,
    OT_P2_FX1      = 2,
    OT_P2_FX2      = 3,
};
#define OT_P2_REFUSED       ((int32_t)0x80000000)
int32_t ot_page2_write(unsigned trk, unsigned page, unsigned slot, int32_t val);

/* ---- paced MIDI out -----------------------------------------------------------------------------
 * The firmware's midi_send (0x40010bc8) has no flow control: it waits, with interrupts masked, for
 * room in the 4 KB DIN ring, which only an interrupt empties, so a burst larger than the ring
 * hangs the unit; and a controller (a BCR2000) that gets hundreds of messages in a second locks up.
 * ot_midi_send_paced copies the message (1-255 bytes: a whole channel message or SysEx) into a
 * queue of 512 bytes and returns 1; core sends one message per 120 Hz tick (ev_tick120) while the
 * DIN ring has room and the engine is idle (a send during a project load reorders the tasks).
 * Returns 0, and queues nothing, when the queue has no room or the length is wrong. It goes through
 * midi_send, so ev_midi_out handlers (USB MIDI) see it. Callable from any task or interrupt. */
int ot_midi_send_paced(const uint8_t *msg, unsigned len);
unsigned ot_midi_paced_space(void);     /* bytes the queue can take: a message needs len + 1 */

#endif
