/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of core-ot 0.38's ot_render_meter: rmp[0..3] track 1's last, worst, calls, total; [4] ticks. */
#include "octatrack/core.h"

volatile uint32_t rmp[5];

void rm_tick(void)
{
    rmp[0] = ot_render_meter.last[0];
    rmp[1] = ot_render_meter.worst[0];
    rmp[2] = ot_render_meter.calls[0];
    rmp[3] = ot_render_meter.total[0];
    rmp[4]++;
}
