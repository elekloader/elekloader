/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of core-ot 0.32's PERSONALIZE rows (tests/test_octatrack.py and ot_emu).
 * prp[0] sets, [1] the last delta, [2] the last press, [3] the value, [4] texts. */
#include "octatrack/core.h"

volatile uint32_t prp[6];
const char pr_label[] = "PROBE ROW";
static char pr_buf[4];

void pr_set(int32_t delta, int32_t press)
{
    prp[0]++; prp[1] = (uint32_t)delta; prp[2] = (uint32_t)press;
    prp[3] = (prp[3] + (uint32_t)delta) & 7;
}

const char *pr_get(void)
{
    prp[4]++;
    pr_buf[0] = 'V'; pr_buf[1] = (char)('0' + (prp[3] & 7)); pr_buf[2] = 0;
    return pr_buf;
}
