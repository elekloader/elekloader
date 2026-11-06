/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sidecar-check (Analog Rytm MKII), part 5: remote fields (core 0.11),
 * checked in RAM (README). Each kind has a sidecar field (its mirror, kept
 * by core) and a stock field beside it (in the record's own bytes, which
 * stock carries wherever it copies the record). CMD5_SEED gives every place
 * the same fresh value in both; after any stock action, CMD5_VERIFY reads
 * both again everywhere: a place whose two values differ is a move core
 * missed (or made wrongly). Every ev_items event is logged. */
#include <stdint.h>
#include "analog-rytm-mk2/store.h"
#include "analog-rytm-mk2/items.h"

STORE_FIELD(sc5_ts);    /* stock: sound 12, kit 16, pattern 16 */
STORE_FIELD(sc5_tk);
STORE_FIELD(sc5_tp);
STORE_FIELD(sc5_ss);    /* sidecar: the same widths */
STORE_FIELD(sc5_sk);
STORE_FIELD(sc5_sp);

#define RING 64

enum {
    CMD5_NONE,
    CMD5_SEED,          /* arg: the first value; every place gets the next */
    CMD5_VERIFY,
    CMD5_CLEAR,         /* the log back to 0 */
    CMD5_READ,          /* arg: kind << 28 | ref & 0x0fffffff: its two values into read_* */
    CMD5_SET,           /* the place of CMD5_READ's ref: arg into both fields */
};

struct sc5_ev { int32_t kind; uint32_t from, to; int32_t n; uint32_t tick; };
struct sc5_bad { int32_t kind; uint32_t ref, stock, side; };

struct {
    char magic[4];                              /* SCK5 */
    volatile int32_t cmd, arg;
    int32_t done;
    uint32_t ticks;
    /* CMD5_VERIFY */
    uint32_t checked, bad, zero, nonzero;       /* places read; differing; both 0; both set */
    struct sc5_bad first[16];
    /* CMD5_READ */
    uint32_t read_ref;
    int32_t read_kind;
    uint32_t read_addr, read_stock, read_side;
    /* ev_items */
    uint32_t n, resyncs;
    struct sc5_ev e[RING];
} sc5 = {{'S', 'C', 'K', '5'}};
const unsigned sc5_size = sizeof sc5;          /* remote.c */

static uint32_t next_value;

static unsigned stock_f(int kind)
{
    return kind == CORE_ITEM_SOUND ? STORE(sc5_ts) : kind == CORE_ITEM_KIT ? STORE(sc5_tk) : STORE(sc5_tp);
}

static unsigned side_f(int kind)
{
    return kind == CORE_ITEM_SOUND ? STORE(sc5_ss) : kind == CORE_ITEM_KIT ? STORE(sc5_sk) : STORE(sc5_sp);
}

/* the places a stock field can be read in: live (patterns, kits, their
 * sounds), saved kits and their sounds, the pool */
static int place(int i, int *kind, uint32_t *ref)
{
    if (i < 129) {                              /* patterns: WORKING, then 0..127 */
        *kind = CORE_ITEM_PATTERN;
        *ref = CORE_ITEM_REF(CORE_ITEM_LIVE, i ? i - 1 : CORE_ITEM_WORKING);
        return 1;
    }
    i -= 129;
    if (i < 256) {
        *kind = CORE_ITEM_KIT;
        *ref = CORE_ITEM_REF(i < 128 ? CORE_ITEM_LIVE : CORE_ITEM_SAVED, i & 127);
        return 1;
    }
    i -= 256;
    if (i < 256 * 12) {
        *kind = CORE_ITEM_SOUND;
        *ref = CORE_ITEM_REF(i / 12 < 128 ? CORE_ITEM_LIVE : CORE_ITEM_SAVED, (i / 12 & 127) * 16 + i % 12);
        return 1;
    }
    i -= 256 * 12;
    if (i < 128) {
        *kind = CORE_ITEM_SOUND;
        *ref = CORE_ITEM_REF(CORE_ITEM_POOL, i);
        return 1;
    }
    return 0;
}

static unsigned mask(int kind)
{
    return kind == CORE_ITEM_SOUND ? 0xfff : 0xffff;
}

static void seed(void)
{
    int i, kind;
    uint32_t ref, v;
    void *a;
    next_value = sc5.arg;
    for (i = 0; place(i, &kind, &ref); i++)
        if ((a = core_item_addr(kind, ref)) != 0) {
            do
                v = next_value++ & mask(kind);
            while (!v);
            core_store_set(stock_f(kind), a, 0, v);
            core_store_set(side_f(kind), a, 0, v);
        }
}

static void verify(void)
{
    int i, kind;
    uint32_t ref, s, d;
    void *a;
    sc5.checked = sc5.bad = sc5.zero = sc5.nonzero = 0;
    for (i = 0; place(i, &kind, &ref); i++) {
        if ((a = core_item_addr(kind, ref)) == 0)
            continue;
        sc5.checked++;
        s = core_store_get(stock_f(kind), a, 0);
        d = core_store_get(side_f(kind), a, 0);
        if (s != d) {
            if (sc5.bad < 16)
                sc5.first[sc5.bad] = (struct sc5_bad){kind, ref, s, d};
            sc5.bad++;
        } else if (s) {
            sc5.nonzero++;
        } else {
            sc5.zero++;
        }
    }
}

void sc_items(int32_t kind, uint32_t from, uint32_t to, int32_t n)
{
    if (kind == CORE_ITEM_RESYNC)
        sc5.resyncs++;
    sc5.e[sc5.n++ % RING] = (struct sc5_ev){kind, from, to, n, sc5.ticks};
}

void sc_items_tick(void *ctrl)
{
    int kind;
    void *a;
    (void)ctrl;
    sc5.ticks++;
    switch (sc5.cmd) {
    case CMD5_NONE:
        return;
    case CMD5_SEED:
        seed();
        break;
    case CMD5_VERIFY:
        verify();
        break;
    case CMD5_CLEAR:
        sc5.n = sc5.resyncs = 0;
        break;
    case CMD5_READ:
        sc5.read_kind = kind = (uint32_t)sc5.arg >> 28;
        sc5.read_ref = (uint32_t)sc5.arg & 0x0fffffff;
        a = core_item_addr(kind, sc5.read_ref);
        sc5.read_addr = (uint32_t)a;
        sc5.read_stock = a ? core_store_get(stock_f(kind), a, 0) : 0xffffffff;
        sc5.read_side = a ? core_store_get(side_f(kind), a, 0) : 0xffffffff;
        sc5.read_ref = core_item_ref(kind, a);  /* core_item_ref round-trips */
        break;
    case CMD5_SET:
        if ((a = core_item_addr(sc5.read_kind, (uint32_t)sc5.read_ref)) != 0) {
            core_store_set(stock_f(sc5.read_kind), a, 0, sc5.arg);
            core_store_set(side_f(sc5.read_kind), a, 0, sc5.arg);
        }
        break;
    }
    sc5.done = sc5.cmd;
    sc5.cmd = CMD5_NONE;
}
