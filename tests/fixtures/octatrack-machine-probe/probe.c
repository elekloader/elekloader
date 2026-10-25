/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A probe of machines-ot 0.2 (read back by a test harness from the emulator's memory): a machine,
 * PROBE ("PB", 1), with OT_MACHINE_LIST_FIRST and a track_page (STATIC's page: a sample page, so the
 * screen shows it as it is), which counts its ui_tick, track_page and leave calls and logs ev_machine_set.
 */
#include <stdint.h>
#include "octatrack/machines.h"

#define FLEX_DEFAULTS ((const uint8_t *)0x400d320cu)
#define STATIC_PAGE 0x400d301cu

enum { MAGIC, TICKS, PAGES, SETS, LOG, LEAVES = LOG + 8, WORDS };  /* LOG: 8 x track << 16 | from << 8 | to */
volatile uint32_t mpb[WORDS] = { 0x4d504231 };   /* 'MPB1' */

static void mp_tick(void)
{
    ++mpb[TICKS];
}

static uint32_t mp_page(const volatile uint8_t *slot)
{
    (void)slot;
    ++mpb[PAGES];
    return STATIC_PAGE;
}

static void mp_leave(unsigned track)
{
    (void)track;
    ++mpb[LEAVES];
}

const struct ot_machine mp_machine = {
    .abi = OT_MACHINE_ABI,
    .sig = { 'P', 'B' }, .sig_lo = 1, .sig_hi = 1,
    .label = "PROBE",
    .flags = OT_MACHINE_PROTECT | OT_MACHINE_LIST_FIRST,
    .defaults = FLEX_DEFAULTS,
    .track_page = mp_page,
    .ui_tick = mp_tick,
    .leave = mp_leave,
};

void mp_machine_set(volatile uint8_t *part, unsigned track, unsigned from, unsigned to)
{
    (void)part;
    uint32_t n = mpb[SETS]++;
    if (n < 8)
        mpb[LOG + n] = track << 16 | from << 8 | to;
}
