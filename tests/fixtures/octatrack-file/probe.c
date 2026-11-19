/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of core-ot 0.34's ev_file (tests/test_octatrack.py and ot_emu).
 * fpp[0..3] opens, reads, writes, closes; [4] bytes read; [5] bytes written; [6..13] the last path. */
#include "octatrack/core.h"

volatile uint32_t fpp[14];

void fp_file(uint32_t op, void *f, const void *a, uintptr_t b)
{
    (void)f;
    if (op < 4)
        fpp[op]++;
    if (op == 1)
        fpp[4] += b;
    if (op == 2)
        fpp[5] += b;
    if (op == 0 && a) {
        const char *s = a;
        char *d = (char *)&fpp[6];
        int i = 0;
        for (; i < 31 && s[i]; i++)
            d[i] = s[i];
        d[i] = 0;
    }
}
