/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * core-ot 0.8: the SETUP windows' knobs (docs/ADAPTING.md, "Edits, Parts and the screen on the
 * Octatrack"; the declarations are in elekloader/sdk/include/octatrack/core.h, the sites in screen.s).
 */
#include <stdint.h>
#include "octatrack/core.h"

extern void (*const ev_setup_dial[])(struct ot_dial *);

/* screen.s: one knob about to be drawn; page, knob, value, flags, formatter and widget are set. */
void otd_dial(struct ot_dial *d)
{
    d->track = OT_TRACK;
    d->part = OT_PART;
    for (void (*const *f)(struct ot_dial *) = ev_setup_dial; *f; ++f)
        (*f)(d);
}
