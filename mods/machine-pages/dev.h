/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The SDK alpha's device layer for the Digitakt mk1 (OS 1.54): what the shared part
 * (../sdk-alpha/machine.c) needs of machine-pages and the firmware. An SDK machine is a core machine
 * whose page is an sdk_alpha_machine_ui. */
#ifndef SDK_ALPHA_DEV_H
#define SDK_ALPHA_DEV_H

#include "src.h"
#include "../sdk-alpha/machine.h"

#define SDK_ALPHA_MACHINES 32           /* the machines with a page, as pages.c */
#define SDK_ALPHA_VOICES 8              /* one a track */
#define dev_fillrect fw_fillrect

extern const struct cm_machine *const core_machines[];

static inline int dev_candidate(uint32_t i, const struct elek_machine **m)
{
    const struct cm_machine *d = core_machines[i];
    if (!d)
        return 0;
    const struct sdk_alpha_machine_ui *u = (const void *)core_machine_ui(d->id);
    *m = u && u->abi == sdk_alpha_dt1_ui_v1 && u->machine && u->machine->id == d->id ? u->machine : 0;
    return 1;
}
#endif
