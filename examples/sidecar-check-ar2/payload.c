/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sidecar-check (Analog Rytm MKII), part 6: payloads (core 0.11), checked
 * in RAM (README). The mod carries a 1 MB payload of a known pattern
 * (payload.s, partly random; mod.json's defsym BLOCKS may make it smaller). At its first ev_tick, and on CMD6_VERIFY, it finds the
 * payload through core (core_payload with its own id) and reads it back
 * whole against the pattern: a word that differs is one the boot copier
 * placed wrongly or something overwrote after boot. */
#include <stdint.h>
#include "analog-rytm-mk2/payload.h"

#define BLOCK_WORDS 64                          /* a block: 256 bytes (payload.s) */
#define TAG 0x5041594cu

enum {
    CMD6_NONE,
    CMD6_VERIFY,
};

#define RANDOM 48

static const uint32_t table[BLOCK_WORDS - 1 - RANDOM] = {
    0xe15d9d01u, 0x8c73fe28u, 0x8e85bde5u, 0x459dd5a5u,
    0x4bda3694u, 0x4a421a4eu, 0x32edf4c4u, 0xf2fd80c7u,
    0xfad6503fu, 0x0ea70bddu, 0x22906a7au, 0xe152535du,
    0x8144ee59u, 0xc5b3986eu, 0x15f6f9c3u
};

extern const uint32_t sc6_payload[], sc6_payload_end[];

struct {
    char magic[4];                              /* SCK6 */
    volatile int32_t cmd, arg;
    int32_t done;
    uint32_t ticks;
    uint32_t id;                                /* core_payload_id("sidecar-check") */
    const void *addr;                           /* core_payload's answer */
    uint32_t len;
    const void *own;                            /* the mod's own symbol: the same place */
    uint32_t own_len;
    uint32_t n_payloads;                        /* entries in core_payloads */
    uint32_t verifies, words, bad, first_bad;   /* the last verify: words read, wrong; first wrong offset */
    uint32_t total_bad;                         /* over every verify */
    uint32_t missing;                           /* 1: core_payload found nothing */
} sc6 = {{'S', 'C', 'K', '6'}};

static void verify(void)
{
    const uint32_t *p;
    uint32_t b, blocks, w, want, x = TAG, bad = 0, first = 0xffffffffu;
    const struct core_payload_entry *e;
    sc6.id = core_payload_id("sidecar-check");
    sc6.addr = core_payload(sc6.id, &sc6.len);
    sc6.own = sc6_payload;
    sc6.own_len = (uint32_t)((const char *)sc6_payload_end - (const char *)sc6_payload);
    for (sc6.n_payloads = 0, e = core_payloads; e->addr; e++)
        sc6.n_payloads++;
    sc6.missing = !sc6.addr;
    sc6.verifies++;
    sc6.words = 0;
    if (!sc6.addr || sc6.len != sc6.own_len || sc6.len % (BLOCK_WORDS * 4)) {
        sc6.bad = 0xffffffffu;
        return;
    }
    p = sc6.addr;
    blocks = sc6.len / (BLOCK_WORDS * 4);
    for (b = 0; b < blocks; b++)
        for (w = 0; w < BLOCK_WORDS; w++, p++) {
            if (!w)
                want = b ^ TAG;
            else if (w <= RANDOM) {
                x ^= x << 13;
                x ^= x >> 17;
                x ^= x << 5;
                want = x;
            } else
                want = table[w - 1 - RANDOM];
            if (*p != want) {
                if (!bad++)
                    first = (b * BLOCK_WORDS + w) * 4;
            }
        }
    sc6.words = blocks * BLOCK_WORDS;
    sc6.bad = bad;
    sc6.first_bad = first;
    sc6.total_bad += bad;
}

void sc_payload_tick(void *ctrl)
{
    (void)ctrl;
    if (!sc6.ticks++)
        verify();
    if (sc6.cmd == CMD6_VERIFY) {
        verify();
        sc6.cmd = CMD6_NONE;
        sc6.done++;
    }
}
