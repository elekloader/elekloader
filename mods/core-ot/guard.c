/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * core-ot 0.36: an effect's choice (docs/ADAPTING.md, "A pattern's check and an effect's choice on the
 * Octatrack"; the declarations are in elekloader/sdk/include/octatrack/core.h, the sites in guard.s).
 */
#include <stdint.h>
#include "octatrack/core.h"

extern int (*const ev_fx_choose[])(uint32_t, uint32_t, uint32_t);

/* guard.s: YES in a chooser. Its row (FX1 0x460d5c94, FX2 0x460d5ca8) indexes the row descriptors
 * (0x400d6060, 0x400d6090), whose first long is the effect id. Nonzero = a handler refused it. */
int core_fx_choose(uint32_t slot)
{
    uint32_t row = *(volatile uint32_t *)(slot ? 0x460d5ca8u : 0x460d5c94u);
    const uint32_t *const *rows = (const uint32_t *const *)(slot ? 0x400d6090u : 0x400d6060u);
    uint32_t id = *rows[row];
    uint32_t trk = *(volatile uint8_t *)0x80000000u;
    for (int (*const *f)(uint32_t, uint32_t, uint32_t) = ev_fx_choose; *f; ++f)
        if ((*f)(slot, trk, id))
            return 1;
    return 0;
}
