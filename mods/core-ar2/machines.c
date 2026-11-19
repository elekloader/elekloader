/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.5: machines mods add, in the hidden slots 34..43 (OS 1.74);
 * from 0.13 in 34..52, the TEST machines' slots 44..52 included, with
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
 * An entry whose engine is 0 keeps the slot's stock engine (no trampoline: the
 * trig getters are not latched for it), and one whose name or label is 0 keeps
 * the slot's stock name or label, so a mod can unhide a stock hidden machine as
 * a claim the linker checks (machines.h).
 *
 * (0.13) The engine table gets core's trampoline for the slot, which latches
 * the voice's trig and velocity for core_voice_trigged and
 * core_voice_velocity (and its note end for core_voice_released) and calls
 * the mod's engine. In the audio interrupt
 * the trig intake runs before the per-voice engine calls, so the latch
 * holds this block's trig.
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
#define SLOT_TEST 44                /* 44..52: the TEST machines (0.13) */
#define SLOT_REF 35                 /* a hidden machine with all eight records */
#define NSLOTS (SLOT_LAST - SLOT_FIRST + 1)
#define SRC_PAGES ((uint8_t *)0x40288028)   /* (0.14) per machine 0x24: name, ids A..H */
#define SRC_PAGE_SIZE 0x24

/* (0.13) the per-voice trig getters' sources (README, "Trig getters") */
#define VOICES 8
#define TRACKS 13
#define OWNER ((const volatile uint8_t *)0x40291138)    /* the track voice v plays (bytes) */
#define TRIG ((const volatile int32_t *)0x8000e508)    /* per track: 1 trig, 2 release */
#define VELOCITY ((const volatile uint16_t *)0x800067c4) /* per track */

extern const struct core_machine *const core_machines[];

#define L(p, o) (*(volatile int32_t *)((p) + (o)))

/* (0.13) The engine table holds core's trampoline for each slot a mod
 * fills: it latches the voice's trig and velocity, as the audio interrupt
 * has them for this block (trig intake runs before the engines), then calls
 * the mod's engine. */
static core_engine mod_engine[NSLOTS];
static const struct core_machine *slot_machine[NSLOTS];
static volatile uint8_t trigged[VOICES];
static volatile uint8_t released[VOICES];   /* 1: the trig state's 2, 2: its 3 */
static volatile uint16_t velocity[VOICES];

static __attribute__((noinline)) void latch(int32_t v)
{
    if ((uint32_t)v >= VOICES)
        return;
    uint32_t t = OWNER[v];
    if (t >= TRACKS) {
        trigged[v] = 0;
        released[v] = 0;
        velocity[v] = 0;
        return;
    }
    int32_t state = TRIG[t];
    trigged[v] = state == 1;
    released[v] = state == 2 ? 1 : state == 3 ? 2 : 0;
    velocity[v] = VELOCITY[t];
}

int core_voice_trigged(int32_t voice)
{
    return (uint32_t)voice < VOICES ? trigged[voice] : 0;
}

/* The voice's note end this block, from its owner track's trig state: 1
 * for a release (state 2: a pad, key or MIDI note-off, 0x4011e426; the
 * note's length running out; the sequencer's note-off), 2 for a cut of
 * every track at once (state 3: 0x4012171e, which the kit and project
 * loads call), else 0. Read like core_voice_trigged. Stock's stop
 * (0x4009c19a, 0x4009c43a) and the kit and project loads call 0x4011e426
 * for every track, so state 2 can come for a note that already ended; core
 * reports it as stock has it; a mod that needs one end per note keeps its
 * own flag. */
int core_voice_released(int32_t voice)
{
    return (uint32_t)voice < VOICES ? released[voice] : 0;
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

/* (0.13) A TEST record has no MIDI (+0x18 and +0x1c -1). A mod knob on one
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

/* (0.14) looks.c: the look core_machines gives stock record id, while a
 * mod fills its slot (CORE_MACHINE_LOOKS; machines.h), else 0 */
const struct core_machine_look *core_machine_look_of(int32_t id)
{
    const uint8_t *r;
    const struct core_machine *m;
    const struct core_machine_look *l;
    int32_t s, k;
    if ((uint32_t)id >= RECORD_COUNT)
        return 0;
    r = RECORDS + RECORD_SIZE * id;
    s = L(r, 0x00);
    k = L(r, 0x04) - 9;
    if (s < SLOT_FIRST || s > SLOT_LAST || k < 0 || k > 7)
        return 0;
    m = slot_machine[s - SLOT_FIRST];
    if (!m || !(m->flags & CORE_MACHINE_LOOKS) || !m->looks || !m->knobs[k].name)
        return 0;
    l = &m->looks[k];
    return l->look || l->ui ? l : 0;
}

/* (0.14) Which knob of the SRC page shows a record is not in the record:
 * the machine's entry in the SRC page table (0x40288028 + 0x24m: the
 * page's name, then the ids on A..H; read through 0x400ff606, for the
 * knobs' draw and turn, 0x400d35b0) lists them. Every public machine and
 * the hidden 34..42 put sound index 9 (LEV) on H; the TEST machines put
 * it on A and the rest in an order of each test's own (44 and 46: 15 and
 * 16 on E and F). For a TEST slot a mod fills, core writes the public
 * order: index 10..16 on A..G, 9 on H, a position whose index has no
 * record getting the entry's own empty value. The sound's words, the CCs
 * and NRPNs follow the index, not the knob, so they do not move. */
static void place_knobs(int32_t s)
{
    int32_t *ids = (int32_t *)(SRC_PAGES + SRC_PAGE_SIZE * s + 4);
    int32_t at[8], none = 0;
    int have_none = 0, k;
    for (k = 0; k < 8; k++)
        at[k] = -1;
    for (k = 0; k < 8; k++) {
        int32_t id = ids[k], i;
        uint8_t *r = RECORDS + RECORD_SIZE * id;
        if ((uint32_t)id < RECORD_COUNT && L(r, 0x00) == s
                && (i = L(r, 0x04) - 9) >= 0 && i < 8 && at[i] < 0) {
            at[i] = id;
        } else {
            none = id;
            have_none = 1;
        }
    }
    for (k = 0; k < 8; k++)
        if (at[k] < 0) {
            if (!have_none)
                return;                     /* not a layout core knows: leave it */
            at[k] = none;
        }
    for (k = 0; k < 7; k++)
        ids[k] = at[k + 1];
    ids[7] = at[0];
}

/* (0.14) Stock engines for any machine. The default machine of
 * track t is the signed long at 0x401bf3fc + 4t (stock's accessor
 * 0x40007130). A machine's knob records are filed by param_tables_build
 * (0x40006cd8) by id only; their range and default live in the static
 * records, so core files them itself, after its own writes, in rec_of.
 * The interrupt calls ENGINES[m](v, 0x800063c0, blk, pitch) for each voice
 * v whose owner track's machine (0x8000ea00[t]) is m, without checking the
 * mask; blk is the track's live sound words from index 9
 * (0x8000f7a8 + 0x54t + 18). An engine keeps the voice's state at
 * 0x8000fef8 + 32v (word 0 a phase: 0 on a reset trig, 4 on a stop, -1 on
 * a machine change) and its noise and wave buffers; UT NOISE (machine 15)
 * reads only blk. Only the destination-list builder (0x400aa280) reads a
 * record's +0x24, when a list is built, so the boot write is early enough. */
#define DEFAULT_MACHINE ((const volatile int32_t *)0x401bf3fc)
#define MACHINES 53
#define NO_RECORD 0xffff
#define VOICE_STATE ((volatile int32_t *)0x8000fef8)   /* 32 bytes per voice; word 0: phase */
static uint16_t rec_of[MACHINES][8];

int core_track_default_machine(int32_t track)
{
    return (uint32_t)track < 12 ? DEFAULT_MACHINE[track] : -1;
}

core_engine core_machine_engine(int32_t m)
{
    return (uint32_t)m < MACHINES ? ENGINES[m] : 0;
}

int core_machine_voices(int32_t m)
{
    return (uint32_t)m < MACHINES ? AVAIL[2 * m + 1] : 0;
}

int core_machine_knob(int32_t m, int32_t k, int32_t *min, int32_t *max, int32_t *def)
{
    if ((uint32_t)m >= MACHINES || (uint32_t)k >= 8 || rec_of[m][k] == NO_RECORD)
        return 0;
    const uint8_t *r = RECORDS + RECORD_SIZE * rec_of[m][k];
    if (min)
        *min = L(r, 0x08);
    if (max)
        *max = L(r, 0x0c);
    if (def)
        *def = L(r, 0x10);
    return 1;
}

int core_machine_defaults(int32_t m, uint16_t *blk)
{
    int got = 0;
    if ((uint32_t)m >= MACHINES)
        return 0;
    for (int k = 0; k < 8; k++) {
        if (rec_of[m][k] == NO_RECORD)
            continue;
        blk[k] = (uint16_t)L(RECORDS + RECORD_SIZE * rec_of[m][k], 0x10);
        got |= 1 << k;
    }
    return got;
}

void core_voice_engine_reset(int32_t voice)
{
    if ((uint32_t)voice < VOICES)
        VOICE_STATE[8 * voice] = -1;
}

static void file_records(void)
{
    for (int m = 0; m < MACHINES; m++)
        for (int k = 0; k < 8; k++)
            rec_of[m][k] = NO_RECORD;
    for (int id = 0; id < RECORD_COUNT; id++) {
        const uint8_t *r = RECORDS + RECORD_SIZE * id;
        uint32_t m = L(r, 0x00), k = L(r, 0x04) - 9;
        if (m < MACHINES && k < 8 && rec_of[m][k] == NO_RECORD)
            rec_of[m][k] = id;
    }
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
        if (m->engine) {                /* 0: the slot's stock engine, as it is */
            mod_engine[s - SLOT_FIRST] = m->engine;
            ENGINES[s] = tramps[s - SLOT_FIRST];
        }
        slot_machine[s - SLOT_FIRST] = m;
        AVAIL[2 * s] = (uint8_t)m->home;
        AVAIL[2 * s + 1] = m->voices;
        if (m->name)                    /* 0: the slot's stock names, as they are */
            NAMES[2 * s] = m->name;
        if (m->label)
            NAMES[2 * s + 1] = m->label;
        if (s >= SLOT_TEST && m->engine)   /* a mod's own machine; a stock one keeps its order */
            place_knobs(s);
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
            if (m->name)
                L(r, 0x2c) = (int32_t)m->name;     /* the group: the machine */
            L(r, 0x30) = (int32_t)n->label;
            if (s >= SLOT_TEST) {
                uint8_t *q = ref_record(k);
                if (q) {
                    L(r, 0x18) = L(q, 0x18);       /* CC 16 + k */
                    L(r, 0x1c) = L(q, 0x1c);       /* NRPN 1:k */
                    if (m->flags & CORE_MACHINE_DESTS)
                        L(r, 0x24) = L(q, 0x24);   /* LFO, velocity, aftertouch */
                }
            }
        }
    }
    file_records();
}
