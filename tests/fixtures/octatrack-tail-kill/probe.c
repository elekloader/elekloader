/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe for trig-events-ot's ev_tail_kill (tests/probe_octatrack_tail_kill.py): two handlers, A
 * (order 10) and B (order 90). A counts and logs each call and, for the tracks in tk_cfg.mark,
 * stores a marker in the record's FX2 SETUP slot 11, as a handler for an effect of its own would
 * zero its bytes; B counts and records the byte it finds there. The probe writes the configuration
 * into the image's RAM before the sequencer starts. Not a mod to ship. */
#include "octatrack/trig.h"

struct tk_config {
    uint32_t mark;      /* the tracks A marks: bit t */
};
struct tk_config tk_cfg;

uint32_t tk_a[8];       /* calls A was given, by track */
uint32_t tk_b[8];       /* calls B was given */
uint8_t tk_seen[8];     /* the byte B found at the record's slot 11, by track, last call */

/* A's log: the track, the FX2 id halfword it found, the byte at slot 11, the record's address */
struct ent { uint8_t track, slot11; uint16_t id; uint32_t rec; };
struct ent tk_log[64];
uint32_t tk_n;

void tk_handler_a(uint8_t *record, unsigned track)
{
    if (tk_n < 64) {
        struct ent *e = &tk_log[tk_n++];
        e->track = (uint8_t)track;
        e->slot11 = record[OT_REC_FX2_SLOT11];
        e->id = *(volatile uint16_t *)(record + OT_REC_FX2_ID);
        e->rec = (uint32_t)(uintptr_t)record;
    }
    tk_a[track & 7]++;
    if ((tk_cfg.mark >> (track & 7)) & 1)
        record[OT_REC_FX2_SLOT11] = 0x5a;
}

void tk_handler_b(uint8_t *record, unsigned track)
{
    tk_b[track & 7]++;
    tk_seen[track & 7] = record[OT_REC_FX2_SLOT11];
}