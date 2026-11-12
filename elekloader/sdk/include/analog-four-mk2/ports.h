/* SPDX-License-Identifier: GPL-2.0-or-later */
/* std1 on the Analog Four MKII (core-a4mk2 0.7, OS 1.56): MIDI ports. The
 * PORT_* values are std1.h's; this names which of them the A4 reports. Its
 * MIDI events are not mapped yet; they will report:
 *   PORT_DIN    MIDI IN / OUT
 *   PORT_USB    USB MIDI, cable 0 (the only one stock sends on or dispatches)
 *   PORT_PANEL  the A4's own keyboard and panel notes (ev_live_note) */
#ifndef ELEKLOADER_A4_PORTS_H
#define ELEKLOADER_A4_PORTS_H

#include <std1.h>

#define CORE_PORTS (PORT_DIN | PORT_USB | PORT_PANEL)

#endif
