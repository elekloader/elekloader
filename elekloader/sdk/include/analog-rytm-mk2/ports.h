/* SPDX-License-Identifier: GPL-2.0-or-later */
/* std1 on the Analog Rytm MKII (core-ar2 0.17, OS 1.74): MIDI ports. The
 * PORT_* values are std1.h's; this names which of them the Rytm reports:
 *   PORT_DIN    MIDI IN / OUT
 *   PORT_USB    USB MIDI, cable 0 (cable 1 never reaches stock's router)
 *   PORT_PANEL  the pads (internal sources; ev_live_note, not mapped yet)
 * The MIDI task's iface bits are 0x08 and 0x10 for DIN, 0x02 and 0x04 for
 * USB; core passes them as PORT_DIN and PORT_USB. ev_midi_out's port is a
 * mask: PORT_DIN | PORT_USB when stock sends to both. ev_seq_midi's buffer
 * is always 0. */
#ifndef ELEKLOADER_AR2_PORTS_H
#define ELEKLOADER_AR2_PORTS_H

#include <std1.h>

#define CORE_PORTS (PORT_DIN | PORT_USB | PORT_PANEL)

/* ev_midi_cc's and ev_midi_pc's flags (the A4's and the Digitone mk1's values) */
#define CORE_MIDI_CC_AUTO  0x01u    /* it came on the auto channel */
#define CORE_MIDI_PC_STOCK 0x02u    /* ev_midi_pc: stock would apply it (received, on the program change channel) */
#define CORE_MIDI_CC_PERF  0x04u    /* ev_midi_cc: the PERF channel's CC, track -1 (the kit's performance set) */

#endif
