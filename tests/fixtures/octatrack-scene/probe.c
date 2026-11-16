/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of core-ot 0.41's ev_scene_load (tests/test_octatrack.py and ot_emu).
 * scp[0] loads, [1] the last slot (0 A, 1 B), [2] scene, [3] bank, [4] Part. */
#include "octatrack/core.h"

volatile uint32_t scp[5];

void sc_load(unsigned slot, unsigned scene, unsigned bank, unsigned part)
{
    scp[0]++;
    scp[1] = slot;
    scp[2] = scene;
    scp[3] = bank;
    scp[4] = part;
}
