/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of core-ot 0.40's ev_engine_job (tests/test_octatrack.py and ot_emu).
 * jpp[0] jobs started, [1] the last started type, [2] the first word of its message,
 * [3] jobs ended, [4] the last ended type. */
#include "octatrack/core.h"

volatile uint32_t jpp[5];

void jp_job(unsigned type, const void *msg)
{
    if (msg) {
        jpp[0]++;
        jpp[1] = type;
        jpp[2] = *(const uint32_t *)msg;
    } else {
        jpp[3]++;
        jpp[4] = type;
    }
}
