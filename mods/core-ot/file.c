/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * core-ot 0.34: the card file layer's calls (docs/ADAPTING.md, "The card file calls on the Octatrack";
 * the declarations are in elekloader/sdk/include/octatrack/core.h, the sites in file.s).
 */
#include <stdint.h>
#include "octatrack/core.h"

extern void (*const ev_file[])(uint32_t, void *, const void *, uintptr_t);
static volatile uint8_t file_busy;

/* file.s, at a call's entry. A handler's own file calls do not come back here. */
void core_file(uint32_t op, void *f, const void *a, uintptr_t b)
{
    if (file_busy)
        return;
    file_busy = 1;
    if (op == 3)
        b = 0;
    for (void (*const *h)(uint32_t, void *, const void *, uintptr_t) = ev_file; *h; ++h)
        (*h)(op, f, a, b);
    file_busy = 0;
}
