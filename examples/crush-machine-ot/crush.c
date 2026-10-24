/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * CRUSH: the smallest source machine for the Octatrack (machines-ot). A CRUSH
 * track plays its FLEX slot's sample, with FLEX's page and knobs, through a
 * 5-bit crusher. It shows the parts of a machine: the record in ot_machines
 * (mod.json contributes it), the signature its tracks carry ("CR", 1), the
 * defaults a track takes, and a render that calls the stock FLEX renderer and
 * changes the samples it shipped.
 */
#include <stdint.h>
#include "octatrack/machines.h"

/* A track that becomes CRUSH takes FLEX's own 12 defaults, from the firmware (0x400d320c):
 * the knobs mean what they mean on FLEX. */
#define FLEX_DEFAULTS ((const uint8_t *)0x400d320cu)

static int crush_render(unsigned track, unsigned ping, unsigned start, unsigned end)
{
    volatile uint8_t *h = ot_render_header();   /* this call's header, before stock writes it */
    int r = ot_stock_render(track, ping, start, end);
    unsigned n = h[3];                           /* the samples stock shipped */
    volatile uint32_t *s = (volatile uint32_t *)(h + 16);
    for (unsigned i = 0; i < 2 * n; ++i)         /* L and R: keep each sample's top 5 bits */
        s[i] &= 0xf8000000u;
    return r;
}

const struct ot_machine crush_machine = {
    .abi = OT_MACHINE_ABI,
    .sig = { 'C', 'R' }, .sig_lo = 1, .sig_hi = 1,
    .label = "CRUSH",
    .flags = OT_MACHINE_PROTECT,
    .defaults = FLEX_DEFAULTS,
    .render = crush_render,
};
