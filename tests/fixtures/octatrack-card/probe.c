/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of core-ot 0.41's ev_card and ev_usb_disk (tests/test_octatrack.py and ot_emu).
 * cdp[0] entries logged, then up to 8 entries of one word each: 0xC0SSRRMM for ev_card (state, result
 * byte, mode), 0x0B00000N for ev_usb_disk(N). */
#include "octatrack/core.h"

volatile uint32_t cdp[9];

static void log1(uint32_t w)
{
    if (cdp[0] < 8)
        cdp[1 + cdp[0]] = w;
    cdp[0]++;
}

void cd_card(unsigned state, int result, unsigned mode)
{
    log1(0xc0000000u | (state & 0xff) << 16 | ((unsigned)result & 0xff) << 8 | (mode & 0xff));
}

void cd_usb(unsigned on)
{
    log1(0x0b000000u | on);
}
