/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of core-ot 0.29's ev_popup (tests/test_octatrack.py and ot_emu).
 * ppp[0] lists opened, [1] the last one's rows, [2] its labels array, [3] its caller, [4] PROBE ROW
 * chosen, [5] the rows it opened with. */
#include "octatrack/core.h"

volatile uint32_t ppp[8];

#define DESIGN_MENU ((const char *const *)0x400c0aa0u)   /* LFO DESIGN's YES list (RANDOMIZE ...) */

static void pp_row(void)
{
    ppp[4]++;
}

void pp_popup(struct ot_popup *p)
{
    ppp[0]++; ppp[1] = p->n; ppp[2] = (uint32_t)p->menu; ppp[3] = p->ret;
    if (p->menu == OT_POPUP_VIEW || p->menu == OT_POPUP_SLICE || p->menu == OT_POPUP_SLICE_ADD
        || p->menu == DESIGN_MENU)
        ot_popup_add(p, "PROBE ROW", pp_row);
    ppp[5] = p->n;
}
