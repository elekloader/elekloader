/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The SDK alpha's device layer for the Digitone mk1: what the shared part (../sdk-alpha/machine.c)
 * needs of the firmware. A mod built by sdk_alpha_build contributes its registration to the table
 * sdk_alpha_machines. */
#ifndef SDK_ALPHA_DEV_H
#define SDK_ALPHA_DEV_H

#include "digitone-mk1/core3.h"
/* the names tests/sdk_alpha_devices.py reads */
#define sdk_alpha_labels dnm_sdk_labels
#define sdk_alpha_visibility dnm_sdk_visibility
#include "../sdk-alpha/machine.h"

#define SDK_ALPHA_MACHINES 16
#define SDK_ALPHA_VOICES 8
#define dev_fillrect fw_fillrect

extern const struct sdk_alpha_registration *const sdk_alpha_machines[];   /* the table, ending in 0 */

static inline int dev_candidate(uint32_t i, const struct elek_machine **m)
{
    const struct sdk_alpha_registration *r = sdk_alpha_machines[i];
    *m = r ? r->ui->machine : 0;
    return r != 0;
}
#endif
