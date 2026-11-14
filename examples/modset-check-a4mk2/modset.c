/* SPDX-License-Identifier: GPL-2.0-or-later */
/* modset-check: core-a4mk2 0.7's ev_mod_set (analog-four-mk2/ui.h). It
 * changes nothing the unit does, except while armed (MS TEST selected in
 * SETTINGS, again to disarm): then it takes every depth change of slot 4
 * (the setup page's knob J), so the depth stays.
 *
 * Everything it sees is in ms_check (an exported symbol), for an emulator's
 * or a unit's memory read:
 *   magic         0x4d534331 ("MSC1") once ev_tick ran
 *   pre, post     the calls of each; taken, the pre calls it took
 *   source_n[7]   the calls per source (MODSRC_VELOCITY .. MODSRC_PERF; 6
 *                 MODSRC_NONE), kind_n[2] per kind (MOD_DEST, MOD_DEPTH)
 *   last_pre, last_post   the last call's record of each
 *   ring[16], ring_n      the last 16 calls in order (ring_n counts them all;
 *                 call k is ring[k % 16])
 *   armed, selects, ticks
 */
#include <stdint.h>
#include <std1.h>
#include "analog-four-mk2/ui.h"

extern void core_additem(void *menu, const void *row);
extern void ms_label(void);

struct ms_rec {
    uint8_t pre, source, slot, kind, track, pad[3];
    int32_t value;
};

struct ms_check {
    uint32_t magic;
    uint32_t pre, post, taken;
    uint32_t source_n[7], kind_n[2];
    struct ms_rec last_pre, last_post;
    uint32_t ring_n;
    struct ms_rec ring[16];
    uint32_t armed, selects, ticks;
};
volatile struct ms_check ms_check;

static void ms_store(volatile struct ms_rec *r, int pre, const struct core_mod_set *m)
{
    r->pre = (uint8_t)pre;
    r->source = m->source;
    r->slot = m->slot;
    r->kind = m->kind;
    r->track = m->track;
    r->value = m->value;
}

int ms_mod_set(int pre, const struct core_mod_set *m)
{
    unsigned s = m->source <= MODSRC_PERF ? m->source : 6u;
    ms_store(&ms_check.ring[ms_check.ring_n & 15u], pre, m);
    ms_check.ring_n++;
    if (pre) {
        ms_check.pre++;
        ms_store(&ms_check.last_pre, pre, m);
        ms_check.source_n[s]++;
        if (m->kind <= MOD_DEPTH)
            ms_check.kind_n[m->kind]++;
        if (ms_check.armed && m->kind == MOD_DEPTH && m->slot == 4) {
            ms_check.taken++;
            return 1;
        }
    } else {
        ms_check.post++;
        ms_store(&ms_check.last_post, pre, m);
    }
    return 0;
}

void ms_tick(void)
{
    ms_check.magic = 0x4d534331u;
    ms_check.ticks++;
}

static void ms_select(void)
{
    ms_check.selects++;
    ms_check.armed ^= 1;
}

static int ms_nothing(void)
{
    return 0;
}

static const void *const ms_row[4] = {
    (const void *)ms_label, (const void *)ms_select,
    (const void *)ms_nothing, (const void *)ms_nothing,
};

void ms_settings(void *menu)
{
    core_additem(menu, ms_row);
}
