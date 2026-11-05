/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sidecar-check (Analog Rytm MKII), part 2: step tags and ev_steps (core
 * 0.11), checked in RAM (README). A command word, written by the emulator
 * or a readback tool, makes the mod act on the next ev_tick. */
#include <stdint.h>
#include "analog-rytm-mk2/steps.h"

#define RING 64
#define NEW ((unsigned (*)(unsigned))0x400827c4)        /* stock's operator new body */
#define DELETE ((void (*)(void *))0x40082a4c)

enum {
    CMD_NONE,
    CMD_TAG,            /* tag steps 1 and 6 of track 1 in the pattern edited: 0x101, 0x106 */
    CMD_HEAP,           /* measure the heap's free space (heap below) */
    CMD_FILL,           /* tag every step of patterns 0..arg-1: (pattern << 10 | step) & 0xffff */
    CMD_CLEAR,          /* clear every tag of the 128 patterns */
    CMD_WALK,           /* count the tags by core_step_tags (walk below) */
};

struct sc_ev { int16_t kind, pattern, track, step, n, owner; };

struct {
    char magic[4];                          /* SCK2 */
    int32_t owner;                          /* core_step_owner */
    int32_t tables;                         /* core_step_tables */
    volatile int32_t cmd, arg;              /* written from outside; cmd back to 0 when done */
    int32_t done;                           /* the last command run */
    int32_t pattern;                        /* core_step_pattern_now */
    int32_t watch;                          /* a pattern to show too (written from outside), -1 */
    int16_t now[16];                        /* tags of track 1's steps 1..16 in the pattern edited */
    int16_t seen[16];                       /* and in pattern watch */
    uint32_t n;                             /* events logged */
    uint32_t kinds[6];                      /* by kind */
    struct sc_ev e[RING];                   /* the last RING, at n % RING */
    int32_t walk_live, walk_copies, walk_sum;  /* CMD_WALK: tags in patterns, in copies, handles' sum */
    uint32_t heap_largest, heap_total, heap_blocks;   /* CMD_HEAP, bytes */
    uint32_t ticks;
} sc2 = {{'S', 'C', 'K', '2'}, -1, 0, 0, 0, 0, -1, -1};

const struct core_step_owner sc_owner = {"SIDECAR CHECK"};

void sc_steps(int32_t kind, int32_t pattern, int32_t track, int32_t step, int32_t n, int32_t owner)
{
    struct sc_ev e = {kind, pattern, track, step, n, owner};
    if ((unsigned)kind < 6)
        sc2.kinds[kind]++;
    sc2.e[sc2.n++ % RING] = e;
}

static void walk_fn(int32_t pattern, int32_t track, int32_t step, int32_t handle)
{
    (void)track; (void)step;
    if (pattern >= 0)
        sc2.walk_live++;
    else
        sc2.walk_copies++;
    sc2.walk_sum += handle;
}

/* the largest block operator new gives now (to 4 KB), by halving */
static unsigned largest(void)
{
    unsigned lo = 0, hi = 32u << 20, m;
    void *p;
    while (hi - lo > 4096) {
        m = lo + (hi - lo) / 2;
        if ((p = (void *)NEW(m)) != 0) {
            DELETE(p);
            lo = m;
        } else {
            hi = m;
        }
    }
    return lo;
}

/* the free space: the largest block, then the largest again with that one
 * taken, up to 64 blocks, all given back */
static void heap(void)
{
    void *held[64];
    unsigned n = 0, total = 0, b;
    sc2.heap_largest = largest();
    while (n < 64 && (b = largest()) >= 4096 && (held[n] = (void *)NEW(b)) != 0) {
        total += b;
        n++;
    }
    sc2.heap_total = total;
    sc2.heap_blocks = n;
    while (n)
        DELETE(held[--n]);
}

void sc_tags_tick(void *ctrl)
{
    int i, p, t, s;
    int32_t o;
    (void)ctrl;
    sc2.ticks++;
    sc2.owner = o = core_step_owner(&sc_owner);
    sc2.tables = core_step_tables();
    sc2.pattern = core_step_pattern_now();
    for (i = 0; i < 16; i++) {
        sc2.now[i] = core_step_tag(sc2.pattern, 0, i, o);
        sc2.seen[i] = core_step_tag(sc2.watch, 0, i, o);
    }
    switch (sc2.cmd) {
    case CMD_TAG:
        core_step_tag_set(sc2.pattern, 0, 0, o, 0x101);
        core_step_tag_set(sc2.pattern, 0, 5, o, 0x106);
        break;
    case CMD_HEAP:
        heap();
        break;
    case CMD_FILL:
        for (p = 0; p < sc2.arg && p < 128; p++)
            for (t = 0; t < 13; t++)
                for (s = 0; s < 64; s++)
                    core_step_tag_set(p, t, s, o, (p << 10 | t << 6 | s) & 0xffff);
        break;
    case CMD_CLEAR:
        for (p = 0; p < 128; p++)
            for (t = 0; t < 13; t++)
                for (s = 0; s < 64; s++)
                    core_step_tag_set(p, t, s, o, -1);
        break;
    case CMD_WALK:
        sc2.walk_live = sc2.walk_copies = sc2.walk_sum = 0;
        core_step_tags(o, walk_fn);
        break;
    default:
        return;
    }
    sc2.done = sc2.cmd;
    sc2.cmd = CMD_NONE;
}
