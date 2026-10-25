/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * USB audio in for the Octatrack, an add-on to usb-audio (mods/usb-audio-ot): the host's channels in
 * place of the unit's inputs, 24-bit at 44.1 kHz in 4-byte subslots, on EP3 OUT (isochronous,
 * asynchronous, with EP3 IN as its implicit feedback). usb-audio describes the stream and answers
 * its requests when this file's record, `usba_in`, says it is on; this file runs it:
 *
 *   USB   EP3 OUT keeps four transfers queued. Each one the controller has completed goes into a ring
 *         of 1,024 frames and is queued again (the ChipIdea add-dTD procedure, as usb-audio's EP3 IN).
 *   CPU   usb-ot's ev_dsp_feed, at the last state of the frame interrupt's transfer chain: the
 *         ring's next 16 frames go to DSP core 0 as one more host-port transfer.
 *   DSP   the inject (inject.asm), on the DSP bus's ev_dsp_rx, writes them over the inputs' slots
 *         of the RX block before anything reads it: the tracks, the recorders and the mix hear the
 *         host where they would hear the jacks.
 *
 * What the host's channels stand in for is a setting, USB AUDIO / IN (early, on the card): OFF, AB
 * (channels 1-2 = inputs A/B), CD (= C/D) or ABCD (1-4 = A-D). OFF is the default: until it is set
 * the unit is USB audio out only. It applies at the next power-up, like the layout, and needs an
 * output stream (a layout other than OFF): EP3 IN is the feedback the host sizes its packets by.
 *
 * The transfer to the DSP is two parts, C/D then A/B. Each is a flag word and, when the flag is 1,
 * 16 samples x 2 channels as (hi, lo) words, hi = sample[23:8], lo = sample[7:0] (the host port
 * carries 16 bits a word). A part whose flag is 0 has nothing after it, and its inputs are the
 * jacks'. A pair's transfer is 96 words, four channels' 160.
 *
 * Facts from sambanks/octabam's USB audio in (MIT; the four-channel form is Bryan T's usbin-test): the
 * transfer machine's state 7 and the second visit a transfer started there makes, the host-port
 * transfer and where it lands, the lengths it may write, the hi/lo split, the RX block's slots,
 * the implicit feedback, the queue head's Mult, the cushion, the counters and their order. The code
 * is elekloader's own.
 */
#include <stdint.h>
#include "octatrack/core.h"
#include "octatrack/usb.h"
#include "octatrack/usbaudio.h"

/* The setting's values, as the card stores them: append new ones at the end. */
enum { I_OFF, I_AB, I_CD, I_ABCD, I_N };
static const char *const in_names[] = { "OFF", "AB", "CD", "ABCD" };
volatile int32_t usbi_inputs = I_OFF;
const struct ot_setting usbi_setting = {
    OT_SETTING_ABI, OT_TAG('U', 'S', 'B', 'I'), 1,
    OT_SET_OPTION, OT_SET_CARD | OT_SET_EARLY,          /* early: usb-audio composes at the USB init */
    usba_group, "IN",                                   /* under usb-audio's heading, after LAYOUT */
    0, I_N - 1, in_names, &usbi_inputs, I_OFF, 0,
};

#define UNC32(p)        ((volatile uint32_t *)OT_USB_UNCACHED(p))
#define UNC16(p)        ((volatile uint16_t *)OT_USB_UNCACHED(p))

#define EP3OUT          OT_USB_EP_OUT(3)
#define USBCMD          OT_USB_REG(0x140)
#define ATDTW           0x00004000u     /* USBCMD: the add-dTD tripwire */
#define TD_ACTIVE       0x80u
#define TD_ERRS         0x68u           /* the token's halted, data buffer and transaction errors */

#define BLOCK           16u             /* frames a frame interrupt */
#define FRAMES          1024u           /* the ring */
#define NSLOT           4u              /* transfers kept queued */
#define MAXCH           4u
#define PKT_MAX         (45u * 4u * MAXCH)  /* a full-speed packet of four channels */
#define TARGET_HS       96u             /* the cushion before the stream goes to the DSP, 2.2 ms */
#define TARGET_FS       160u            /* a full-speed packet is 44 frames, a millisecond apart */
#define TX_PAIR         96u             /* words to the DSP: X:$x320..$x37f */
#define TX_FOUR         160u            /* ... $x39f */
#define CLOSED          4u              /* blocks of flags 0 sent after the stream closes: the banks alternate */

/* The counters, in the order and shape (big-endian longs) of octabam's, so its tools read them:
 * the vendor request 0xC0/0x56 returns them. `seconds` is usb-ot's count of feeds completed. */
struct usbi_counters {
    uint32_t produced, consumed, pkts, lastn, lastfill, underruns, overruns, reprimes;
    uint32_t bad, frames, seconds, minfill, maxfill, err, partial;
};
struct usbi_counters usbi_counters;
#define C               usbi_counters

static uint32_t ring[FRAMES * MAXCH];   /* the host's subslots as they came, MAXCH a frame */
static uint32_t dtds[NSLOT][8] __attribute__((aligned(32)));
static uint32_t bufs[NSLOT][PKT_MAX / 4] __attribute__((aligned(32)));
static uint16_t tx[TX_FOUR] __attribute__((aligned(32)));

/* The host's side (the USB interrupt, level 4): the alternate setting it asked for, and a count of
 * the times it went to 0. The feed (level 6) owns EP3 OUT and acts on them once a frame. */
static volatile uint8_t req, gen;
static uint8_t mode, nch;               /* this boot's, from the setting (channels()) */
static uint8_t running, run_gen, prefill, ri, closed;
static uint16_t opkt, target;

/* ---- the record usb-audio reads ------------------------------------------------------------ */

static unsigned channels(void)
{
    int32_t v = usbi_inputs;
    mode = (uint32_t)v < I_N ? v : I_OFF;
    nch = mode == I_ABCD ? 4 : mode ? 2 : 0;
    return nch;
}

static void set_alt(unsigned alt)
{
    req = alt;
    if (!alt)
        ++gen;                          /* down, even if it comes back up before the next frame */
}

static unsigned get_alt(void)
{
    return req;
}

static int setup(const volatile uint8_t *p)
{
    if (p[0] != 0xc0 || p[1] != 0x56)   /* vendor, device: the counters */
        return 0;
    ot_usb_send(sizeof C, &C);
    return 1;
}

const struct usba_in usba_in = { USBA_IN_ABI, channels, set_alt, get_alt, setup };

/* ---- EP3 OUT ------------------------------------------------------------------------------- */

static volatile uint32_t *dtd(unsigned k)
{
    return UNC32(dtds[k & (NSLOT - 1)]);
}

static void prime(volatile uint32_t *td)
{
    volatile uint32_t *qh = OT_USB_DQH(3, 0);
    qh[2] = (uint32_t)(uintptr_t)td;
    qh[3] = 0;                          /* the overlay's token: not ACTIVE, not halted */
    OT_USB_ENDPTPRIME = EP3OUT;
}

/* Slot k's transfer, one packet into its buffer; ACTIVE is written last. */
static void fill(unsigned k)
{
    volatile uint32_t *td = dtd(k);
    uint32_t b = (uint32_t)(uintptr_t)UNC32(bufs[k & (NSLOT - 1)]);
    td[0] = 1;                          /* no next */
    td[2] = b;
    td[3] = (b & 0xfffff000u) + 0x1000; /* a packet may cross a page */
    td[1] = (uint32_t)opkt << 16 | TD_ACTIVE;   /* no IOC: the feed looks */
}

/* The oldest transfer still queued, in queue order from slot `from`; 0 if none is. */
static volatile uint32_t *oldest(unsigned from)
{
    for (unsigned k = 0; k < NSLOT; ++k)
        if (dtd(from + k)[1] & TD_ACTIVE)
            return dtd(from + k);
    return 0;
}

/* Slot k, filled again, after k - 1, the newest. */
static void enqueue(unsigned k)
{
    volatile uint32_t *td = dtd(k), *prev = dtd(k - 1);
    uint32_t st = 0;
    unsigned tries = 16;
    if (!(prev[1] & TD_ACTIVE)) {       /* every one before it has landed: the queue is empty */
        prime(td);
        return;
    }
    prev[0] = (uint32_t)(uintptr_t)td;
    if (OT_USB_ENDPTPRIME & EP3OUT)
        return;                         /* a prime not yet taken reads the link */
    for (;;) {                          /* the tripwire: is the endpoint still running? */
        USBCMD |= ATDTW;
        st = OT_USB_ENDPTSTAT & EP3OUT;
        if (USBCMD & ATDTW)
            break;
        if (!--tries)
            return;                     /* never settled: the next frame's check primes it */
    }
    USBCMD &= ~ATDTW;
    if (!st)                            /* it ran out before the link: from the oldest */
        prime(oldest(k));
}

/* SET_INTERFACE alt 1 on the IN interface, seen here: EP3 OUT up at the port's speed, four
 * transfers queued, the ring empty and the DSP given the jacks until the cushion is there. */
static void ep_up(void)
{
    volatile uint32_t *qh = OT_USB_DQH(3, 0);
    unsigned hs;
    ot_usb_flush(EP3OUT);
    for (unsigned w = 0; w < 16; ++w)   /* the whole queue head: stock leaves it garbage */
        qh[w] = 0;
    hs = ot_usb_speed() == OT_USB_HS;
    opkt = usba_in_packet(hs ? OT_USB_HS : OT_USB_FS);
    target = hs ? TARGET_HS : TARGET_FS;
    qh[0] = 0x60000000u | (uint32_t)opkt << 16;  /* Mult 1 (an iso OUT with 0 loses most packets) */
    qh[2] = 1;
    OT_USB_ENDPTCTRL(3) = (OT_USB_ENDPTCTRL(3) & 0xffff0000u) | 0x00000084u;  /* RX iso, on */
    C.produced = C.consumed = 0;
    C.minfill = 0xffffffffu;
    C.maxfill = 0;
    prefill = 1;
    for (unsigned k = 0; k < NSLOT; ++k)
        fill(k);
    for (unsigned k = 0; k + 1 < NSLOT; ++k)
        dtd(k)[0] = (uint32_t)(uintptr_t)dtd(k + 1);
    ri = 0;
    prime(dtd(0));
    running = 1;
    run_gen = gen;
}

static void ep_down(void)
{
    ot_usb_flush(EP3OUT);
    OT_USB_ENDPTCTRL(3) &= 0xffff0000u; /* the RX half off; TX is EP3 IN's */
    for (unsigned k = 0; k < NSLOT; ++k) {
        volatile uint32_t *td = dtd(k);
        for (unsigned w = 0; w < 8; ++w)
            td[w] = 0;
        td[0] = 1;
    }
    running = 0;
    closed = 0;                         /* flags 0 go out until both banks have them */
}

/* Every packet that has landed into the ring, in queue order, each transfer queued again. */
static void retire(void)
{
    unsigned bytes = 4u * nch;
    for (unsigned lap = 0; lap < NSLOT; ++lap) {
        volatile uint32_t *td = dtd(ri);
        uint32_t tok = td[1], left = tok >> 16 & 0x7fff, got, n;
        if (tok & TD_ACTIVE)
            break;                      /* nothing more has landed */
        ++C.pkts;
        if (tok & TD_ERRS)
            ++C.bad, ++C.err;
        if (left <= opkt) {
            const volatile uint32_t *b = UNC32(bufs[ri]);
            got = opkt - left;
            if (got % bytes)
                ++C.bad, ++C.partial;   /* not whole frames: the tail is dropped */
            n = got / bytes;
            C.lastn = n;
            for (uint32_t f = 0; f < n; ++f) {
                uint32_t *s = ring + ((C.produced + f) & (FRAMES - 1)) * MAXCH;
                for (unsigned c = 0; c < nch; ++c)
                    s[c] = *b++;
            }
            C.produced += n;
            if (C.produced - C.consumed > FRAMES) {
                ++C.overruns;           /* the host is a whole ring ahead: start again */
                C.consumed = C.produced - target;
            }
        }
        fill(ri);
        enqueue(ri);
        ri = (ri + 1) & (NSLOT - 1);
    }
}

/* Idle with a transfer still queued: prime the oldest. */
static void selfheal(void)
{
    if (!((OT_USB_ENDPTSTAT | OT_USB_ENDPTPRIME) & EP3OUT)) {
        volatile uint32_t *td = oldest(ri);
        if (td) {
            prime(td);
            ++C.reprimes;
        }
    }
}

/* ---- the transfer to the DSP ----------------------------------------------------------------- */

/* One part: its flag, then 16 frames of two channels from the ring, at channel `at`. */
static volatile uint16_t *part(volatile uint16_t *t, unsigned at)
{
    *t++ = 1;
    for (unsigned f = 0; f < BLOCK; ++f) {
        const uint32_t *s = ring + ((C.consumed + f) & (FRAMES - 1)) * MAXCH + at;
        for (unsigned c = 0; c < 2; ++c) {
            uint32_t w = s[c];          /* the subslot's bytes: 0, then the sample's, low first */
            __asm__("byterev %0" : "+d"(w));
            *t++ = w >> 16;             /* sample[23:8] */
            *t++ = w >> 8 & 0xff;       /* sample[7:0] */
        }
    }
    return t;
}

/* The next block, or the jacks while the cushion fills (at the start, and after the ring ran dry). */
static void build(void)
{
    volatile uint16_t *t = UNC16(tx);
    uint32_t fill = C.produced - C.consumed;
    C.lastfill = fill;
    if (fill < (prefill ? target : BLOCK)) {
        if (!prefill)
            ++C.underruns;
        prefill = 1;
        t[0] = t[1] = 0;
        return;
    }
    prefill = 0;
    if (fill < C.minfill)
        C.minfill = fill;
    if (fill > C.maxfill)
        C.maxfill = fill;
    if (mode == I_AB)
        *t++ = 0;
    else
        t = part(t, mode == I_ABCD ? 2 : 0);   /* C/D: the host's 1-2, or 3-4 of four */
    if (mode == I_CD)
        *t = 0;
    else
        part(t, 0);                     /* A/B: the host's 1-2 */
    C.consumed += BLOCK;
}

/* usb-ot's ev_dsp_feed: once a frame, after the frame's stock transfers, at level 6. */
void usbi_feed(void)
{
    C.seconds = ot_dsp_fed;
    ++C.frames;
    if (running && (!req || run_gen != gen))
        ep_down();
    if (req && !running && nch)
        ep_up();
    if (running) {
        OT_USB_ENDPTCOMPLETE = EP3OUT;  /* none asked for (no IOC): kept from going stale */
        retire();
        selfheal();
        build();
    } else {
        volatile uint16_t *t = UNC16(tx);
        if (closed >= CLOSED)
            return;                     /* both banks hold flags 0: nothing to send */
        t[0] = t[1] = 0;
        ++closed;
    }
    ot_dsp_feed(UNC16(tx), nch == 4 ? TX_FOUR : TX_PAIR);
}
