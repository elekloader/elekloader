/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The SDK alpha's device layer for the Digitone II (OS 1.12, UI only): what the shared part
 * (../sdk-alpha/machine.c) needs of the firmware. A mod built by sdk_alpha_build contributes its
 * registration to the table sdk_alpha_machines. */
#ifndef SDK_ALPHA_DEV_H
#define SDK_ALPHA_DEV_H

/* the names tests/sdk_alpha_devices.py reads */
#define sdk_alpha_labels dn2_sdk_labels
#define sdk_alpha_visibility dn2_sdk_visibility
#include "../sdk-alpha/machine.h"

#define SDK_ALPHA_MACHINES 16
#define dev_fillrect dn2_fillrect
#define dev_active dn2_sdk_active
const struct sdk_alpha_entry *dn2_sdk_active(void);   /* sdk_alpha.c */

extern void dn2_fillrect(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t on);
extern const struct sdk_alpha_registration *const sdk_alpha_machines[];   /* the table, ending in 0 */

static inline int dev_candidate(uint32_t i, const struct elek_machine **m)
{
    const struct sdk_alpha_registration *r = sdk_alpha_machines[i];
    *m = r ? r->ui->machine : 0;
    return r != 0;
}
#endif
