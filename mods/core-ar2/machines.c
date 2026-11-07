/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.5: machines mods add, in the hidden slots 34..43 (OS 1.74);
 * from 0.12 in 34..52, the TEST machines' slots 44..52 included, with
 * per-voice trig and velocity getters (core_voice_trigged, _velocity).
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
 *
 * (0.12) The engine table gets core's trampoline for the slot, which latches
 * the voice's trig and velocity for core_voice_trigged and
 * core_voice_velocity and calls the mod's engine. In the audio interrupt
 * the trig intake runs before the per-voice engine calls (firmware
 * reference 6.1), so the latch holds this block's trig.
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
#define SLOT_LAST 52
#define SLOT_TEST 44                /* 44..52: the TEST machines (0.12) */
#define SLOT_REF 35                 /* a hidden machine with all eight records */
#define NSLOTS (SLOT_LAST - SLOT_FIRST + 1)

/* (0.12) the per-voice trig getters' sources (README, "Trig getters") */
#define VOICES 8
#define TRACKS 13
#define OWNER ((const volatile uint8_t *)0x40291138)    /* the track voice v plays (bytes) */
#define TRIG ((const volatile int32_t *)0x8000e508)    /* per track: 1 trig, 2 release */
#define VELOCITY ((const volatile uint16_t *)0x800067c4) /* per track */

extern const struct core_machine *const core_machines[];

#define L(p, o) (*(volatile int32_t *)((p) + (o)))

/* (0.12) The engine table holds core's trampoline for each slot a mod
 * fills: it latches the voice's trig and velocity, as the audio interrupt
 * has them for this block (trig intake runs before the engines), then calls
 * the mod's engine. */
static core_engine mod_engine[NSLOTS];
static volatile uint8_t trigged[VOICES];
static volatile uint16_t velocity[VOICES];

static __attribute__((noinline)) void latch(int32_t v)
{
    if ((uint32_t)v >= VOICES)
        return;
    uint32_t t = OWNER[v];
    if (t >= TRACKS) {
        trigged[v] = 0;
        velocity[v] = 0;
        return;
    }
    trigged[v] = TRIG[t] == 1;
    velocity[v] = VELOCITY[t];
}

int core_voice_trigged(int32_t voice)
{
    return (uint32_t)voice < VOICES ? trigged[voice] : 0;
}

int core_voice_velocity(int32_t voice)
{
    return (uint32_t)voice < VOICES ? velocity[voice] : 0;
}

#define TRAMP(n) \
    static void tramp_##n(int32_t v, void *f, uint16_t *b, int32_t p) \
    { latch(v); mod_engine[n](v, f, b, p); }
TRAMP(0) TRAMP(1) TRAMP(2) TRAMP(3) TRAMP(4) TRAMP(5) TRAMP(6) TRAMP(7)
TRAMP(8) TRAMP(9) TRAMP(10) TRAMP(11) TRAMP(12) TRAMP(13) TRAMP(14)
TRAMP(15) TRAMP(16) TRAMP(17) TRAMP(18)
static const core_engine tramps[NSLOTS] = {
    tramp_0, tramp_1, tramp_2, tramp_3, tramp_4, tramp_5, tramp_6, tramp_7,
    tramp_8, tramp_9, tramp_10, tramp_11, tramp_12, tramp_13, tramp_14,
    tramp_15, tramp_16, tramp_17, tramp_18};

/* (0.12) A TEST record has no MIDI (+0x18 and +0x1c -1). A mod knob on one
 * takes the hidden machines' CC 16 + k (+0x18: the CC in the high half, no
 * LSB 0xffff in the low) and NRPN 1:k (+0x1c: msb << 7 | lsb) for sound
 * index 9 + k, copied from slot 35's record at the same index (34..42 carry
 * the same). +0x20 is 0 on every hidden record, 34..43 included, so it is
 * left as it is. */
static uint8_t *ref_record(int32_t k)
{
    for (int id = 0; id < RECORD_COUNT; id++) {
        uint8_t *r = RECORDS + RECORD_SIZE * id;
        if (L(r, 0x00) == SLOT_REF && L(r, 0x04) == 9 + k)
            return r;
    }
    return 0;
}

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
        mod_engine[s - SLOT_FIRST] = m->engine;
        ENGINES[s] = tramps[s - SLOT_FIRST];
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
            if (s >= SLOT_TEST) {
                uint8_t *q = ref_record(k);
                if (q) {
                    L(r, 0x18) = L(q, 0x18);       /* CC 16 + k */
                    L(r, 0x1c) = L(q, 0x1c);       /* NRPN 1:k */
                }
            }
        }
    }
}
