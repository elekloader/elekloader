/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DSPLOAD's ColdFire half: ot_dsp_load (octatrack/dspload.h) places a blob in the shared window's
 * clear gap, relocates it there and stages it as a message; the pump sends each message to DSP
 * core 1's X:0x2901.. over the host port, 100 halfwords a frame, then its doorbell, three times;
 * the copier (copier.asm) copies it into the window and binds its slot, and the dispatcher
 * (run.asm, core 0) calls it every frame.
 *
 * The host transfer and its timing are sambanks/octabam's DLPUMP's (MIT, modules/dlpump/pump.s):
 * the stock host write's register sequence (FUN_40001b18), core 1 chosen by the chip select, only
 * the low 16 bits of each word arriving, and only on a frame whose ping word (0x800000e0) is 0 (a
 * transfer started on a ping-1 frame is lost, measured in octabam's port). Core 0 is never written:
 * octabam measured that a transfer there at the frame's entry stalls its chain. The chip select goes
 * back to core 0 after: the frame interrupt sends its own command to whichever core is selected.
 *
 * 0.2: the guard. The gap is above what any stock effect writes with its default knobs, but a reverb
 * with other settings may reach it. Each block goes in as [guard][stub][code][guard]: the two guard
 * words hold 0xc0ffee, and the slot's entry is the stub, which reads both before it jumps to the
 * code; if either has changed it clears the slot's BOUND word and returns, so the code never runs
 * again (the slot stays dry). A write that lands inside the code without reaching either end is
 * not caught: a reverb's buffer is thousands of words, the gap 597.
 * 0.2 also sends the blobs of the ot_dsp_blobs table (mod.json "dsp_load", elekloader.sdk.build),
 * each once, from ev_tick: a TELOS mod's are in it after the boot's link.
 */
#include <stdint.h>
#include "octatrack/core.h"
#include "octatrack/dspload.h"

#define HOSTW(o)    (*(volatile uint16_t *)(0x20000000u + (o)))
#define CSEL        (*(volatile uint8_t *)0xfc0a400cu)
#define PING        (*(volatile uint32_t *)0x800000e0u)
#define BELL        0x2900u                     /* core 1's X: the doorbell, then the message */
#define RING        0xa55au
#define CHUNK       100u
#define SENDS       3u                          /* every block, three times */
#define GAP         30u                         /* quiet frames between them */
#define WIN         0x30000u
#define GAP_LO      0x33dabu                    /* the window's clear gap, after the slots */
#define GAP_HI      0x34000u
#define MAXW        (GAP_HI - GAP_LO)
#define GUARD       0xc0ffeeu
#define SLOTS_X     0x33da3u                    /* x: the slots, 2 words each: BOUND, entry */

/* The guard's stub, DSP56300 (dsp_asm; relative branches only, so the same words anywhere):
 *     move x:>LO,a ; cmp #>$c0ffee,a ; bne out ; move x:>HI,a ; cmp #>$c0ffee,a ; bne out
 *     move #>CODE,r0 ; nop ; jmp (r0)
 * out: clr a ; move a,x:>BOUND ; rts
 * Words 1, 7, 13 and 18 are LO, HI, CODE and BOUND. It uses only a, r0 and the condition codes,
 * which ev_dsp_rx's contract lets a handler change. */
static const uint32_t stub[] = {
    0x56f000, 0, 0x0140c5, GUARD, 0x0d1042, 0x00000c, 0x56f000, 0, 0x0140c5, GUARD, 0x0d1042, 0x000006,
    0x60f400, 0, 0x000000, 0x0ae080, 0x200013, 0x567000, 0, 0x00000c,
};
#define STUB_N      (sizeof stub / sizeof stub[0])
#define WRAP        (1 + STUB_N + 1)            /* the words a block takes besides its code */

volatile uint32_t ot_dsp_counts[8];

struct msg {
    uint16_t len;                               /* halfwords from X:0x2901 */
    uint8_t sends, wait;
    uint16_t h[4 + 2 * MAXW + 2];
};
static struct msg q[OT_DSP_SLOTS];
static uint32_t nq, next = GAP_LO;

/* The stock host write (DLPUMP's dl_xfer): n halfwords to X:dest of the selected core. */
static void xfer(uint32_t dest, const uint16_t *src, uint32_t n)
{
    CSEL = 1;                                   /* core 1 */
    HOSTW(0) = 0x81;
    HOSTW(0x1c) = (uint16_t)(0x4000 | dest);
    HOSTW(0x1c) = (uint16_t)(n - 1);
    HOSTW(4) = 0x88;                            /* host command $08: write X */
    while (n--) {
        uint16_t v = *src++;
        while (!(HOSTW(8) & 6))
            ;
        HOSTW(0x14) = 0;
        HOSTW(0x18) = (uint16_t)(v >> 8);
        HOSTW(0x1c) = v;
    }
}

static uint32_t put(struct msg *m, uint32_t i, uint32_t w, uint32_t sum)
{
    m->h[4 + 2 * i] = (uint16_t)(w >> 8);
    m->h[5 + 2 * i] = (uint16_t)(w & 0xff);
    return (sum + w) & 0xffffff;
}

int ot_dsp_load(const struct ot_dsp_blob *b)
{
    struct msg *m;
    uint32_t i, k, at, code, n, sum = 0, r = 0;
    if (nq >= OT_DSP_SLOTS || !b || !b->n || b->entry >= b->n || next + b->n + WRAP > GAP_HI)
        return -1;
    k = nq;
    m = &q[k];
    at = next;
    code = at + 1 + STUB_N;
    n = b->n + WRAP;
    m->h[0] = (uint16_t)k;
    m->h[1] = (uint16_t)(at - WIN);
    m->h[2] = (uint16_t)n;
    m->h[3] = 1;                                /* the entry: the stub, after the first guard */
    sum = put(m, 0, GUARD, sum);
    for (i = 0; i < STUB_N; i++) {
        uint32_t w = stub[i];
        if (i == 1)
            w = at;                             /* the first guard */
        else if (i == 7)
            w = code + b->n;                    /* the last */
        else if (i == 13)
            w = code + b->entry;
        else if (i == 18)
            w = SLOTS_X + 2 * k;                /* this slot's BOUND */
        sum = put(m, 1 + i, w, sum);
    }
    for (i = 0; i < b->n; i++) {
        uint32_t w = b->words[i] & 0xffffff;
        while (r < b->nrel && b->rel[r] < i)
            r++;
        if (r < b->nrel && b->rel[r] == i)
            w = (w + code) & 0xffffff;          /* an address in the code: it moves with it */
        sum = put(m, 1 + STUB_N + i, w, sum);
    }
    sum = put(m, n - 1, GUARD, sum);
    m->h[4 + 2 * n] = (uint16_t)(sum >> 8);
    m->h[5 + 2 * n] = (uint16_t)(sum & 0xff);
    m->len = (uint16_t)(4 + 2 * n + 2);
    m->wait = 0;
    next += n;
    m->sends = SENDS;                           /* last: the pump may look at it from now on */
    nq++;
    ot_dsp_counts[0]++;
    return (int)k;
}

/* ev_tick (the sys task): the blobs of the ot_dsp_blobs table, each once (mod.json "dsp_load"). After
 * the card's mount it holds a TELOS mod's too, so it is read again until every slot is taken. */
extern const struct ot_dsp_blob *const ot_dsp_blobs[];
static const struct ot_dsp_blob *sent_b[OT_DSP_SLOTS * 4];
static uint32_t nsent_b;
volatile int32_t ot_dsp_table_slot[OT_DSP_SLOTS * 4];

void dsl_tick(void)
{
    const struct ot_dsp_blob *const *t;
    uint32_t i;
    for (t = ot_dsp_blobs; *t && nsent_b < sizeof sent_b / sizeof sent_b[0]; t++) {
        for (i = 0; i < nsent_b && sent_b[i] != *t; i++)
            ;
        if (i < nsent_b)
            continue;
        ot_dsp_table_slot[nsent_b] = ot_dsp_load(*t);
        sent_b[nsent_b++] = *t;
    }
}

/* The pump's state: which message, how far, and whether its doorbell is due. */
static uint32_t cur = 0xffffffffu, sent, bell;

/* ev_frame_begin: the frame interrupt's first point, before the stock chain. */
void dsl_frame(void)
{
    uint32_t i, n;
    ot_dsp_counts[3]++;
    if (PING != 0)
        return;
    ot_dsp_counts[4]++;
    if (cur == 0xffffffffu) {                   /* idle: the next message with a send left */
        for (i = 0; i < nq; i++)
            if (q[i].wait)
                q[i].wait--;
        for (i = 0; i < nq && cur == 0xffffffffu; i++)
            if (q[i].sends && !q[i].wait)
                cur = i, sent = 0, bell = 0;
        if (cur == 0xffffffffu)
            return;
    }
    if (!bell) {
        n = q[cur].len - sent;
        if (n > CHUNK)
            n = CHUNK;
        xfer(BELL + 1 + sent, q[cur].h + sent, n);
        ot_dsp_counts[1]++;
        sent += n;
        if (sent >= q[cur].len)
            bell = 1;                           /* the doorbell on the next quiet frame */
    } else {
        static const uint16_t ring = RING;
        xfer(BELL, &ring, 1);
        ot_dsp_counts[2]++;
        q[cur].sends--;
        q[cur].wait = GAP;
        cur = 0xffffffffu;
    }
    CSEL = 0;                                   /* the frame code expects core 0 selected */
}
