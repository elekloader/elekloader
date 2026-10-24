/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * core-ot 0.5's exports (docs/ADAPTING.md, "The hook bus on the Octatrack"; the declarations are
 * in elekloader/sdk/include/octatrack/core.h): the page-2 writer, the paced MIDI queue and the
 * frame meter's reset. Octabam's CC MAP, TEMPO BUS and MODE DEFAULTS each make the same five
 * stores by hand; this is the one copy. The addresses are the firmware's, read from the OS 1.40C
 * image's page-2 editors (0x4003a474 PLAYBACK, 0x4003adec AMP, 0x4003abe4 FX1, 0x4003a9dc FX2),
 * which this does not copy but follows store for store.
 */
#include <stdint.h>
#include "octatrack/core.h"

#define U8(a)           (*(volatile uint8_t *)(uintptr_t)(a))
#define U32(a)          (*(volatile uint32_t *)(uintptr_t)(a))

/* ---- interrupts -------------------------------------------------------------------------------- */
static inline uint32_t irq_off(void)
{
    uint32_t sr;
    __asm__ volatile("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) : : "memory");
    return sr;
}

static inline void irq_on(uint32_t sr)
{
    __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "memory");
}

/* ---- the frame meter --------------------------------------------------------------------------- */
void ot_meter_reset(void)
{
    uint32_t sr = irq_off();
    ot_meter.frames = ot_meter.last = ot_meter.worst = ot_meter.overruns = 0;
    ot_meter.late = ot_meter.total = 0;
    irq_on(sr);
}

/* ---- page 2 ------------------------------------------------------------------------------------ */
#define DB              U32(0x46c82456)         /* the current bank's RAM */
#define PART_SIZE       6322u
#define MIRROR          0x1001614eu             /* the battery-backed copy of the bank's RAM */
#define DIRTY_PARTS     0x95048u                /* DB+: bit p, Part p changed since saved */
#define DIRTY_PARTS_CS1 0x100b145eu             /* the same bits, battery-backed */
#define DIRTY_BANK      0x9b332u                /* DB+: a long */
#define DIRTY_PROJECT   0x100f8598u             /* a long */
#define LANE            0x80000810u             /* + 72 t: the engine's live knob bytes */
#define REDRAW          0x46c7d244u             /* + 20 slot + 4: a knob to draw again */
#define MACHINE         0x8eda2u                /* in a Part: + t, the machine type 0-4 */
#define RESOLVE         ((uint32_t (*)(uint32_t, uint32_t))0x40031da4)   /* (track, page kind) -> descriptor */

/* A page's place in a Part: where its six bytes for track 0 start, the live lane's offset, and the
 * resolver's page kind. PLAYBACK has a set of six for each machine type, after the first. */
static const struct page { uint32_t store; uint8_t lane, kind; } pages[4] = {
    { 0x8ef5a, 0x20, 0 },       /* PLAYBACK */
    { 0x8f078, 0x2c, 2 },       /* AMP */
    { 0x8f07e, 0x32, 3 },       /* FX1 */
    { 0x8f084, 0x38, 4 },       /* FX2 */
};

int32_t ot_page2_write(unsigned trk, unsigned page, unsigned slot, int32_t val)
{
    if (trk > 7 || page > 3 || slot > 5)
        return OT_P2_REFUSED;
    unsigned part = U8(0x80000003);
    if (part > 3)
        return OT_P2_REFUSED;
    uint32_t desc = RESOLVE(trk, pages[page].kind);
    if (!desc)
        return OT_P2_REFUSED;
    /* the knob's range: the descriptor's 12 minimums, then its 12 counts, from byte 0x6a; page 2's
     * knobs are numbers 6-11 */
    int32_t lo = *(volatile int32_t *)(uintptr_t)(desc + 0x6a + 4 * (slot + 6));
    int32_t n = *(volatile int32_t *)(uintptr_t)(desc + 0x9a + 4 * (slot + 6));
    if (n < 1)
        return OT_P2_REFUSED;
    if (val > lo + n - 1)
        val = lo + n - 1;
    if (val < lo)
        val = lo;

    uint32_t db = DB;
    uint32_t off = part * PART_SIZE + pages[page].store + trk * 30 + slot;
    if (page == OT_P2_PLAYBACK) {
        unsigned type = U8(db + part * PART_SIZE + MACHINE + trk);
        if (type > 4)
            return OT_P2_REFUSED;
        off += 6 * type;
    }
    U8(db + off) = (uint8_t)val;                        /* the Part */
    U8(MIRROR + off) = (uint8_t)val;                    /* its battery-backed copy */
    U8(db + DIRTY_PARTS) |= (uint8_t)(1u << part);
    U8(DIRTY_PARTS_CS1) |= (uint8_t)(1u << part);
    U32(db + DIRTY_BANK) = 1;
    U32(DIRTY_PROJECT) = 1;
    U8(LANE + 72 * trk + pages[page].lane + slot) = (uint8_t)val;   /* the engine */
    U32(REDRAW + 20 * slot + 4) = 20;
    return val;
}

/* ---- paced MIDI out ---------------------------------------------------------------------------- */
#define MIDI_SEND       ((void (*)(unsigned, const uint8_t *))0x40010bc8)    /* (len, msg) */
#define DIN_RING_COUNT  U32(0x400b967c)         /* bytes waiting in the 4 KB DIN ring */
#define DIN_RING_MAX    3072u                   /* send only while it holds fewer than this less the message */
#define ENGINE_WAIT     U32(0x460d17ce + 0xc)   /* the engine's TCB when it waits for a command: nonzero = idle */
#define QSIZE           512u

static uint8_t queue[QSIZE];
static uint8_t flat[255];                       /* a record that wraps, in a row */
static volatile uint32_t qhead, qtail;          /* the oldest record's length byte; where the next goes */

unsigned ot_midi_paced_space(void)
{
    unsigned used = (qtail - qhead) % QSIZE;
    return QSIZE - 1 - used;
}

int ot_midi_send_paced(const uint8_t *msg, unsigned len)
{
    if (len < 1 || len > 255)
        return 0;
    uint32_t sr = irq_off();
    if (len + 1 > ot_midi_paced_space()) {
        irq_on(sr);
        return 0;
    }
    uint32_t t = qtail;
    queue[t] = (uint8_t)len;
    t = (t + 1) % QSIZE;
    for (unsigned k = 0; k < len; ++k) {
        queue[t] = msg[k];
        t = (t + 1) % QSIZE;
    }
    qtail = t;
    irq_on(sr);
    return 1;
}

/* Called first in each ev_tick120: the oldest message goes out if the DIN ring has room for it and
 * the engine is idle. One a tick: 120 messages a second, 360 bytes of the ring's 3,125. */
void otp_drain(void)
{
    if (qhead == qtail || !ENGINE_WAIT)
        return;
    unsigned len = queue[qhead];
    if (DIN_RING_COUNT + len > DIN_RING_MAX)
        return;
    uint32_t h = (qhead + 1) % QSIZE;
    const uint8_t *msg = &queue[h];
    if (h + len > QSIZE) {                      /* the record wraps: copy it out in a row */
        for (unsigned k = 0; k < len; ++k)
            flat[k] = queue[(h + k) % QSIZE];
        msg = flat;
    }
    MIDI_SEND(len, msg);
    uint32_t sr = irq_off();
    qhead = (h + len) % QSIZE;
    irq_on(sr);
}
