/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sidecar-check (Analog Rytm MKII), 0.8 (0.9: no part 5): the records' readback and the
 * commands over USB MIDI, for a unit run that can't peek or poke RAM
 * (README, "Over MIDI"). Every message starts F0 7D 53 43 ("SC"):
 *
 *   in   01 <part> <cmd> <a0> <a1> <a2> <a3> <a4> F7
 *        part 1..4, 6, 7's command word: arg = a0 << 28 | a1 << 21 | a2 << 14 |
 *        a3 << 7 | a4 is written, then cmd (part 1 has none)
 *   in   02 <part> F7
 *        send part's record back: the whole record, from its magic
 *   out  03 <part> <off: 3 x 7 bits> <n> <2n nibbles, high first> F7
 *        n (at most 48) bytes of the record from off
 *   out  04 <part> <size: 3 x 7 bits> F7
 *        the record's end
 *
 * The messages in are taken in ev_sysex_in (MIDI task); the record goes out
 * from ev_tick through core_midi_try_send, up to 8 messages a tick, a
 * refused one again on the next tick. */
#include <stdint.h>
#include "analog-rytm-mk2/moments.h"

extern char sc[], sc2[], sc3[], sc4[], sc6[], sc7[];
extern const unsigned sc1_size, sc2_size, sc3_size, sc4_size, sc6_size, sc7_size;

#define PARTS 7
#define CHUNK 48

static char *rec(int part, unsigned *size, int *cmd)
{
    switch (part) {
    case 1: *size = sc1_size; *cmd = -1; return sc;
    case 2: *size = sc2_size; *cmd = 12; return sc2;
    case 3: *size = sc3_size; *cmd = 4; return sc3;
    case 4: *size = sc4_size; *cmd = 4; return sc4;
    /* part 5 (remote fields) left core 0.11 (sidecar-check 0.9): no record */
    case 6: *size = sc6_size; *cmd = 4; return sc6;
    case 7: *size = sc7_size; *cmd = 4; return sc7;
    }
    return 0;
}

static volatile int dump_part;                  /* the record going out, 0 none */
static unsigned dump_at;                        /* its next byte */

struct {
    char magic[4];                              /* SCRM */
    uint32_t cmds, dumps, sent, refused;
} sc_remote = {{'S', 'C', 'R', 'M'}};

int sc_remote_in(const unsigned char *d, int len)
{
    char *r;
    unsigned size;
    int cmd, part;
    uint32_t arg;
    if (len < 7 || d[0] != 0xf0 || d[1] != 0x7d || d[2] != 0x53 || d[3] != 0x43)
        return 0;
    part = d[5];
    if (!(r = rec(part, &size, &cmd)))
        return 1;
    if (d[4] == 1 && len >= 13 && cmd >= 0) {
        arg = (uint32_t)d[7] << 28 | (uint32_t)d[8] << 21 | (uint32_t)d[9] << 14 |
              (uint32_t)d[10] << 7 | d[11];
        *(volatile int32_t *)(r + cmd + 4) = (int32_t)arg;
        *(volatile int32_t *)(r + cmd) = d[6];
        sc_remote.cmds++;
    } else if (d[4] == 2 && !dump_part) {
        dump_at = 0;
        dump_part = part;
        sc_remote.dumps++;
    }
    return 1;
}

static void put3(unsigned char *o, unsigned v)
{
    o[0] = (unsigned char)(v >> 14 & 0x7f);
    o[1] = (unsigned char)(v >> 7 & 0x7f);
    o[2] = (unsigned char)(v & 0x7f);
}

/* ev_tick (moments.c): the record going out */
void sc_remote_tick(void)
{
    unsigned char m[12 + 2 * CHUNK];
    const unsigned char *r;
    unsigned size, n, i;
    int cmd, k;
    if (!dump_part || !(r = (const unsigned char *)rec(dump_part, &size, &cmd)))
        return;
    for (k = 0; k < 8; k++) {
        m[0] = 0xf0;
        m[1] = 0x7d;
        m[2] = 0x53;
        m[3] = 0x43;
        m[5] = (unsigned char)dump_part;
        if (dump_at >= size) {
            m[4] = 4;
            put3(m + 6, size);
            m[9] = 0xf7;
            if (!core_midi_try_send(10, m)) {
                sc_remote.refused++;
                return;
            }
            sc_remote.sent++;
            dump_part = 0;
            return;
        }
        n = size - dump_at < CHUNK ? size - dump_at : CHUNK;
        m[4] = 3;
        put3(m + 6, dump_at);
        m[9] = (unsigned char)n;
        for (i = 0; i < n; i++) {
            m[10 + 2 * i] = r[dump_at + i] >> 4;
            m[11 + 2 * i] = r[dump_at + i] & 15;
        }
        m[10 + 2 * n] = 0xf7;
        if (!core_midi_try_send((int)(11 + 2 * n), m)) {
            sc_remote.refused++;
            return;
        }
        sc_remote.sent++;
        dump_at += n;
    }
}
