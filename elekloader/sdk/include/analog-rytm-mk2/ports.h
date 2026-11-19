/* SPDX-License-Identifier: GPL-2.0-or-later */
/* std1 on the Analog Rytm MKII (core-ar2 0.17, OS 1.74): MIDI ports. The
 * PORT_* values are std1.h's; this names which of them the Rytm reports:
 *   PORT_DIN    MIDI IN / OUT
 *   PORT_USB    USB MIDI, cable 0 (cable 1 never reaches stock's router)
 *   PORT_PANEL  the pads (internal sources; ev_live_note, not mapped yet)
 * The MIDI task's iface bits are 0x08 and 0x10 for DIN, 0x02 and 0x04 for
 * USB; core passes them as PORT_DIN and PORT_USB. */
#ifndef ELEKLOADER_AR2_PORTS_H
#define ELEKLOADER_AR2_PORTS_H

#include <std1.h>

#define CORE_PORTS (PORT_DIN | PORT_USB | PORT_PANEL)

#endif
