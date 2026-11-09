/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The SDK alpha's device layer for the Digitone mk1: what the shared part (../sdk-alpha/machine.c)
 * needs of the firmware. A mod built by sdk_alpha_build contributes each machine to the table
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
#define SDK_ALPHA_TRACKS 4             /* the synth tracks: their CCs, and mono machines */
#define dev_fillrect fw_fillrect
#define dev_active dnm_sdk_active
const struct sdk_alpha_entry *dnm_sdk_active(void);   /* sdk_alpha.c */
/* mono machines, on core-dn1 5.3's ev_live_note */
#define dev_note_on(t, n, v, source) fw_note_on(t, n, v, source, 0, -1)
#define dev_note_off fw_note_off

static inline int32_t dev_track_machine(int32_t track)
{
    uint8_t *kit = fw_kit;
    return kit ? FW_SOUND_SLOT(kit, track, 0) >> 8 : -1;      /* slot 0: the machine */
}

extern const struct elek_machine *const sdk_alpha_machines[];   /* the table, ending in 0 */

static inline int dev_candidate(uint32_t i, const struct elek_machine **m)
{
    *m = sdk_alpha_machines[i];
    return *m != 0;
}
#endif
