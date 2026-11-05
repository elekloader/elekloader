/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The SDK alpha's device layer for the Digitakt mk1 (OS 1.54): what the shared part
 * (../sdk-alpha/machine.c) needs of machine-pages and the firmware. sdk_alpha_build contributes each
 * machine to the table sdk_alpha_machines, and its struct cm_machine, which names no page, to core_machines. */
#ifndef SDK_ALPHA_DEV_H
#define SDK_ALPHA_DEV_H

#include "src.h"
#include "../sdk-alpha/machine.h"

#define SDK_ALPHA_MACHINES 32           /* the machines with a page, as pages.c */
#define SDK_ALPHA_VOICES 8              /* one a track */
#define dev_fillrect fw_fillrect

extern const struct elek_machine *const sdk_alpha_machines[];   /* the table, ending in 0 */

static inline int dev_candidate(uint32_t i, const struct elek_machine **m)
{
    *m = sdk_alpha_machines[i];
    return *m != 0;
}
#endif
