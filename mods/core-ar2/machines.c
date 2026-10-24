/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.5: machines mods add, in the hidden slots 34..43 (OS 1.74).
 *
 * The machine table is three arrays of the main OS's data, each indexed by
 * the machine's number m (0..52):
 *
 *   0x401bf254 + 8m   long name, short name (pointers)
 *   0x401bf42c + 2m   home track (signed byte), voice mask (bit v: voice v)
 *   0x402906dc + 4m   the engine the audio interrupt calls
 *
 * and each machine owns its parameter records (0x401bfbc8 + 0x34 id)
 * whose owner (+0x00) is m, at sound indices 9..16 (+0x04). Stock hides
 * machines 34..52 with mask 0 and nothing else. Before the OS reads any of
 * this (core_boot_init, from core.s's .boot), core writes each core_machines entry into
 * it (analog-rytm-mk2/machines.h). The OS then builds the knobs' runtime
 * copies, the machine list and the MIDI maps from them as it does for its
 * own machines. elekloader refuses two entries for one slot (the profile's
 * slot_claims), so the first entry for a slot is the only one.
 */
#include <stdint.h>
#include "analog-rytm-mk2/machines.h"

#define NAMES ((const char **)0x401bf254)
#define AVAIL ((volatile uint8_t *)0x401bf42c)
#define ENGINES ((core_engine *)0x402906dc)
#define RECORDS ((uint8_t *)0x401bfbc8)
#define RECORD_SIZE 0x34
#define RECORD_COUNT 469
#define SLOT_FIRST 34
#define SLOT_LAST 43                /* 44..52's records are mods' spares */

extern const struct core_machine *const core_machines[];

#define L(p, o) (*(volatile int32_t *)((p) + (o)))

void core_boot_init(void)
{
    uint32_t done = 0;
    const struct core_machine *const *e;
    for (e = core_machines; *e; e++) {
        const struct core_machine *m = *e;
        int32_t s = m->slot;
        if (s < SLOT_FIRST || s > SLOT_LAST || (done >> (s - SLOT_FIRST) & 1))
            continue;
        done |= 1u << (s - SLOT_FIRST);
        ENGINES[s] = m->engine;
        AVAIL[2 * s] = (uint8_t)m->home;
        AVAIL[2 * s + 1] = m->voices;
        NAMES[2 * s] = m->name;
        NAMES[2 * s + 1] = m->label;
        for (int id = 0; id < RECORD_COUNT; id++) {
            uint8_t *r = RECORDS + RECORD_SIZE * id;
            int32_t k = L(r, 0x04) - 9;
            if (L(r, 0x00) != s || k < 0 || k > 7)
                continue;
            const struct core_machine_knob *n = &m->knobs[k];
            if (!n->name)
                continue;
            L(r, 0x08) = n->min << 8;               /* min, max, default: value << 8 */
            L(r, 0x0c) = n->max << 8;
            L(r, 0x10) = n->def << 8;
            L(r, 0x14) = n->centred;
            L(r, 0x28) = (int32_t)n->name;
            L(r, 0x2c) = (int32_t)m->name;         /* the group: the machine */
            L(r, 0x30) = (int32_t)n->label;
        }
    }
}
