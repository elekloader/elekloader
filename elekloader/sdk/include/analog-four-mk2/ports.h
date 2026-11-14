/* SPDX-License-Identifier: GPL-2.0-or-later */
/* std1 on the Analog Four MKII (core-a4mk2 0.7, OS 1.56): MIDI ports. The
 * PORT_* values are std1.h's; this names which of them the A4 reports:
 *   PORT_DIN    MIDI IN / OUT
 *   PORT_USB    USB MIDI, cable 0 (the only one stock sends on or dispatches)
 *   PORT_PANEL  the A4's own keyboard and panel notes (ev_live_note, not
 *               mapped yet)
 * ev_midi_out's port is a mask: PORT_DIN | PORT_USB when stock sends to
 * both. ev_seq_midi's buffer is always 0. */
#ifndef ELEKLOADER_A4_PORTS_H
#define ELEKLOADER_A4_PORTS_H

#include <std1.h>

#define CORE_PORTS (PORT_DIN | PORT_USB | PORT_PANEL)

/* ev_midi_cc's and ev_midi_pc's flags (the Digitone mk1's values) */
#define CORE_MIDI_CC_AUTO  0x01u    /* it came on the auto channel */
#define CORE_MIDI_PC_STOCK 0x02u    /* ev_midi_pc: stock would apply it (received, PROG CH IN's channel) */
#define CORE_MIDI_CC_PERF  0x04u    /* ev_midi_cc: the PERF channel's CC, track -1 (the kit's performance set) */

#endif
