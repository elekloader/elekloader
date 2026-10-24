/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * USB audio for the Octatrack, a function of usb-ot: the unit as a USB audio
 * input (UAC2) on its own USB port, 44.1 kHz, 24-bit samples in 4-byte
 * subslots, on EP3 IN (isochronous, asynchronous). Interfaces: AudioControl
 * (a fixed 44.1 kHz clock, an input terminal, a USB streaming output
 * terminal) and AudioStreaming (alt 0 idle, alt 1 streaming) under an
 * interface association.
 *
 * What the stream carries is a setting (usba_layout, an early card setting of
 * settings-ot: set at ev_boot_done, so in place when usb-ot composes the
 * descriptors at the controller's init). enabled() latches it for the boot:
 *
 *   OFF                  no function: usb-ot adds no interfaces
 *   MAIN                  2 channels: MAIN L/R
 *   M+C  (MAIN+CUE)       4 channels: MAIN L/R, CUE L/R
 *   MSTR (MASTER)         2 channels: track 8 L/R
 *   TRKS (TRACKS)        16 channels: tracks 1-8 L/R
 *   ALL  (TRACKS+MAIN+CUE) 20 channels: tracks 1-8 L/R, MAIN L/R, CUE L/R (the default)
 *
 * At high speed a packet every 250 us (bInterval 2) carries those channels;
 * at full speed one every 1 ms carries a stereo pair: the eight tracks' sum
 * (TRKS, ALL), track 8 (MSTR) or MAIN (MAIN, M+C).
 *
 * Facts from sambanks/octabam's USB audio modules (MIT; built on
 * markandrus/octemu's USB audio, MAIN and CUE found by Bryan T): the
 * read-back taps, the lag of MAIN and CUE against the tracks, the packet
 * sizes and the rate servo, the queue of four transfers and the cushion the
 * host's first poll sets, the clock-source replies. The code is elekloader's
 * own: the producer and the packet builder run on core-ot 0.4's
 * ev_frame_end, where octabam patched the frame interrupt's tail.
 */
#include <stdint.h>
#include "octatrack/core.h"
#include "octatrack/usb.h"

/* The values the setting stores on the card: append new layouts at the end. */
enum { L_OFF, L_MAIN, L_MAIN_CUE, L_MASTER, L_TRACKS, L_ALL, L_N };
#define L_DEFAULT       L_ALL

struct layout {
    uint8_t tracks, t8, main, cue;      /* the sources, 1 when in the stream */
    uint8_t nch;                        /* channels at high speed */
};
static const struct layout layouts[L_N] = {
    [L_OFF]      = { 0, 0, 0, 0, 0 },
    [L_MAIN]     = { 0, 0, 1, 0, 2 },
    [L_MAIN_CUE] = { 0, 0, 1, 1, 4 },
    [L_MASTER]   = { 0, 1, 0, 0, 2 },
    [L_TRACKS]   = { 1, 0, 0, 0, 16 },
    [L_ALL]      = { 1, 0, 1, 1, 20 },
};
#define NCH_MAX         20u

/* The setting. The variable is the default until settings-ot sets it from battery RAM at
 * ev_boot_done; without that companion it stays the default. The menu's value column shows four
 * characters: OFF, MAIN, M+C (MAIN+CUE), MSTR (MASTER: track 8), TRKS (TRACKS), ALL (TRACKS+MAIN+CUE). */
static const char *const layout_names[] = { "OFF", "MAIN", "M+C", "MSTR", "TRKS", "ALL" };
volatile int32_t usba_layout = L_DEFAULT;
const struct ot_setting usba_setting = {
    OT_SETTING_ABI, OT_TAG('U', 'S', 'B', 'A'), 1,      /* tag + key: the identity on the card */
    OT_SET_OPTION, OT_SET_CARD | OT_SET_EARLY,          /* early: read before the card is */
    "USB AUDIO", "LAYOUT",
    0, L_N - 1, layout_names, &usba_layout, L_DEFAULT, 0,
};

/* What this boot uses (enabled() latches it); the taps' places follow the layout. */
static const struct layout *lay = &layouts[L_OFF];
static uint8_t main_at, cue_at, lag;    /* MAIN's and CUE's first channel from 0; MAIN's lag (below) */

#define U8(a)           (*(volatile uint8_t *)(uintptr_t)(a))
#define U32(a)          (*(volatile uint32_t *)(uintptr_t)(a))
#define UNC32(p)        ((volatile uint32_t *)OT_USB_UNCACHED(p))

/* The taps. The eDMA writes each track's block (post-FX, before its level, the crossfader
 * and the master effects) into the read-back arena every frame, in two banks: bank b,
 * track t, frame f is at RB_BASE + b*1024 + t*128 + f*8, (L, R), 32-bit words with 24
 * bits of audio at the top. The bank to read is the one RB_PREV names. MAIN and CUE are
 * the words the DACs get: 16 frames of (L, R) each at MC_BASE, MAIN then CUE (+0x80), one
 * buffer the frame interrupt's DMA rewrites each frame. */
#define RB_BASE         0x80003190u
#define RB_PREV         0x800000e4u
#define MC_BASE         0x80005e60u
#define MASTER_ON       0x80000034u     /* the MASTER TRACK setting's byte */
#define BLOCK           16u             /* frames a frame interrupt */

/* MAIN and CUE of a frame interrupt are the mix of the tracks one block before the track
 * words read beside them, so with tracks in the stream they go into the ring one block
 * behind (`lag`). With MASTER TRACK on, CUE leads MAIN by two blocks (the mix sends MAIN
 * through track 8), so CUE goes two blocks ahead of MAIN's place. (octabam measured both.) */
#define CUE_LEAD        (2u * BLOCK)

/* The ring: FRAMES frames of nch words at high speed, of 2 at full speed, filled once a
 * frame interrupt and drained into the packets. The servo holds it TARGET frames deep. */
#define FRAMES          512u
#define TARGET          64u
#define NSLOT           4u              /* transfers kept queued: 1 ms of polls at high speed */
#define PKT_HS_MAX      (12u * 4u * NCH_MAX)    /* 12 frames, the most a 250 us packet takes */
#define PKT_FS          (45u * 8u)              /* 45 stereo frames, the most of a 1 ms one */
#define PKT_MAX         (PKT_HS_MAX > PKT_FS ? PKT_HS_MAX : PKT_FS)
#define STEP_HS         11025u          /* frames a packet x1000: 44.1 kHz at 250 us */
#define STEP_FS         44100u          /* ... at 1 ms */
#define SERVO_MAX       200             /* the correction's clamp, x1000 frames a packet */

#define EP3IN           OT_USB_EP_IN(3)
#define USBCMD          OT_USB_REG(0x140)
#define ATDTW           0x00004000u     /* USBCMD: the add-dTD tripwire */
#define TD_ACTIVE       0x80u

#define AC_CLOCK        0x10            /* the UAC2 entities */
#define AC_IT           0x11
#define AC_OT           0x12

/* SET CUR's data stage, on stock's EP0 OUT transfer descriptor and buffer. */
#define EP0_OUT_DTD     0x4ec95000u
#define EP0_OUT_BUF     0x4ec96000u

extern const struct ot_usb_function usb_audio;

/* The counters, in the order and shape (big-endian longs) of octabam's, so its tools read
 * them: the vendor request 0xC0/0x55 returns them. lastsamp and srcjump stay 0 here. */
struct usba_counters {
    uint32_t consumed, acc, overruns, underruns, lastn, lastfill, lastbank, bankdup;
    uint32_t lastsamp, srcjump, reprimes, minfill, maxfill, anchor, produced;
};
struct usba_counters usba_counters = { .lastbank = 0xffffffffu };
#define C               usba_counters

static uint32_t ring[FRAMES * NCH_MAX];
static uint32_t dtds[NSLOT][8] __attribute__((aligned(32)));
static uint32_t bufs[NSLOT][PKT_MAX / 4] __attribute__((aligned(32)));

/* The host's side, written from the USB interrupt (level 4): the alt setting it asked for,
 * and a count of the bus resets and session ends. The frame interrupt (level 5) owns EP3
 * and acts on them once a block. */
static volatile uint8_t req, gen;
static uint8_t running, run_gen, hs, tail, await;
static uint32_t step;

/* usb-ot's enabled(), once at the controller's init and before descriptors(): the layout this
 * boot uses. A value outside the table (a poke, a card from a newer build) is the default.
 * OFF leaves the function out: no interfaces, nothing for the host to see. */
static int enabled(void)
{
    int32_t v = usba_layout;
    if ((uint32_t)v >= L_N)
        v = L_DEFAULT;
    lay = &layouts[v];
    main_at = 16 * lay->tracks + 2 * lay->t8;
    cue_at = main_at + 2 * lay->main;
    lag = lay->tracks && lay->main ? BLOCK : 0;
    return lay->nch != 0;
}

static unsigned pkt_hs(void)
{
    return 12u * 4u * lay->nch;
}

static unsigned put(uint8_t *o, const uint8_t *b, unsigned n)
{
    for (unsigned k = 0; k < n; ++k)
        o[k] = b[k];
    return n;
}

static unsigned descriptors(uint8_t *o, unsigned first, unsigned speed)
{
    unsigned high = speed == OT_USB_HS;
    unsigned nch = high ? lay->nch : 2, pkt = high ? pkt_hs() : PKT_FS, n = 0;
    uint8_t lr = nch == 2 ? 3 : 0;      /* bmChannelConfig: front left and right */
    const uint8_t ac[] = {
        9, 4, first, 0, 0, 1, 1, 0x20, 0,                       /* AudioControl */
        9, 0x24, 1, 0x00, 0x02, 0x0a, 46, 0, 0,                 /* header: UAC 2.0, pro audio */
        8, 0x24, 0x0a, AC_CLOCK, 0x01, 0x05, 0, 0,              /* clock: internal fixed, read-only */
        17, 0x24, 0x02, AC_IT, 0x03, 0x06, 0, AC_CLOCK, nch, lr, 0, 0, 0, 0, 0, 0, 0,   /* line in */
        12, 0x24, 0x03, AC_OT, 0x01, 0x01, 0, AC_IT, AC_CLOCK, 0, 0, 0,             /* USB out */
    };
    const uint8_t as[] = {
        9, 4, first + 1, 0, 0, 1, 2, 0x20, 0,                   /* AudioStreaming alt 0: idle */
        9, 4, first + 1, 1, 1, 1, 2, 0x20, 0,                   /* alt 1: EP3 IN */
        16, 0x24, 1, AC_OT, 0, 1, 1, 0, 0, 0, nch, lr, 0, 0, 0, 0,  /* AS_GENERAL: PCM */
        6, 0x24, 2, 1, 4, 24,                                   /* FORMAT_TYPE_I: 4-byte subslots, 24 bits */
        7, 5, 0x83, 0x05, pkt & 0xff, pkt >> 8, high ? 2 : 1,  /* iso, asynchronous */
        8, 0x25, 1, 0, 0, 0, 0, 0,
    };
    n += put(o + n, ac, sizeof ac);
    n += put(o + n, as, sizeof as);
    return n;
}

static inline uint32_t le24(uint32_t w)        /* a read-back word -> its subslot, little-endian */
{
    w &= 0xffffff00u;
    __asm__("byterev %0" : "+d"(w));
    return w;
}

static inline uint32_t le24_sum(int32_t s)     /* a sum of samples (24-bit units), saturated */
{
    if (s > 0x7fffff)
        s = 0x7fffff;
    else if (s < -0x800000)
        s = -0x800000;
    return le24((uint32_t)s << 8);
}

static const volatile uint32_t *bank(void)
{
    uint32_t b = U32(RB_PREV);
    if (b == C.lastbank)
        ++C.bankdup;                    /* the banks did not alternate */
    C.lastbank = b;
    return (const volatile uint32_t *)(RB_BASE + (b & 1) * 1024);
}

static uint32_t *slot(uint32_t frame)
{
    return ring + (frame & (FRAMES - 1)) * (hs ? lay->nch : 2);
}

/* One block into the ring: every channel at high speed, the pair at full speed. */
static void produce(void)
{
    const volatile uint32_t *mc = (const volatile uint32_t *)MC_BASE;
    const volatile uint32_t *rb = lay->tracks || lay->t8 ? bank() : 0;
    uint32_t at = C.produced;
    if (!hs) {
        for (unsigned f = 0; f < BLOCK; ++f) {
            uint32_t *s = slot(at + f);
            if (lay->tracks) {
                int32_t l = 0, r = 0;
                for (unsigned t = 0; t < 8; ++t) {
                    l += (int32_t)rb[t * 32 + 2 * f] >> 8;
                    r += (int32_t)rb[t * 32 + 2 * f + 1] >> 8;
                }
                s[0] = le24_sum(l);
                s[1] = le24_sum(r);
            } else if (lay->t8) {
                s[0] = le24(rb[7 * 32 + 2 * f]);
                s[1] = le24(rb[7 * 32 + 2 * f + 1]);
            } else {
                s[0] = le24(mc[2 * f]);
                s[1] = le24(mc[2 * f + 1]);
            }
        }
        return;
    }
    uint32_t lead = lay->cue && U8(MASTER_ON) ? CUE_LEAD : 0;
    for (unsigned f = 0; f < BLOCK; ++f) {
        uint32_t *s = slot(at + f);
        if (lay->tracks) {
            for (unsigned t = 0; t < 8; ++t) {
                s[2 * t] = le24(rb[t * 32 + 2 * f]);
                s[2 * t + 1] = le24(rb[t * 32 + 2 * f + 1]);
            }
        } else if (lay->t8) {
            s[0] = le24(rb[7 * 32 + 2 * f]);
            s[1] = le24(rb[7 * 32 + 2 * f + 1]);
        }
        if (lay->main) {
            s = slot(at + f - lag);
            s[main_at] = le24(mc[2 * f]);
            s[main_at + 1] = le24(mc[2 * f + 1]);
        }
        if (lay->cue) {
            s = slot(at + f - lag + lead);
            s[cue_at] = le24(mc[32 + 2 * f]);
            s[cue_at + 1] = le24(mc[32 + 2 * f + 1]);
        }
    }
}

/* ---- EP3 IN: four transfers, the ChipIdea add-dTD procedure -------------------------- */

static volatile uint32_t *dtd(unsigned k)
{
    return UNC32(dtds[k & (NSLOT - 1)]);
}

static void prime(volatile uint32_t *td)
{
    volatile uint32_t *qh = OT_USB_DQH(3, 1);
    qh[2] = (uint32_t)(uintptr_t)td;
    qh[3] = 0;                          /* the overlay's token: not ACTIVE, not halted */
    OT_USB_ENDPTPRIME = EP3IN;
}

/* The oldest transfer still queued, in fill order from slot `from`; 0 if none is. */
static volatile uint32_t *oldest(unsigned from)
{
    for (unsigned k = 0; k < NSLOT; ++k)
        if (dtd(from + k)[1] & TD_ACTIVE)
            return dtd(from + k);
    return 0;
}

static void enqueue(volatile uint32_t *td)
{
    volatile uint32_t *prev = dtd(tail - 1);
    uint32_t st = 0;
    unsigned tries = 16;
    if (!(prev[1] & TD_ACTIVE)) {       /* the queue is empty */
        prime(td);
        return;
    }
    prev[0] = (uint32_t)(uintptr_t)td;  /* linked after the newest */
    if (OT_USB_ENDPTPRIME & EP3IN)
        return;                         /* a prime not yet taken reads the link */
    for (;;) {                          /* the tripwire: is the endpoint still running? */
        USBCMD |= ATDTW;
        st = OT_USB_ENDPTSTAT & EP3IN;
        if (USBCMD & ATDTW)
            break;
        if (!--tries)
            return;                     /* never settled: the next block's check primes it */
    }
    USBCMD &= ~ATDTW;
    if (!st)                            /* it retired the newest before the link: the head */
        prime(oldest(tail + 1));
}

/* The next packet into the tail slot: -> 1 when one was queued. */
static int build(void)
{
    volatile uint32_t *td = dtd(tail);
    volatile uint32_t *b;
    uint32_t fill, a, n, words;
    int32_t e;
    if ((td[1] & TD_ACTIVE) || !req)
        return 0;
    fill = C.produced - C.consumed;
    if (fill > FRAMES) {                /* the host stopped reading: start again */
        ++C.overruns;
        C.consumed = C.produced - TARGET;
        fill = TARGET;
    }
    a = step;
    if (!await) {                       /* the servo: send what is produced, at the unit's clock */
        if (fill < C.minfill)
            C.minfill = fill;
        if (fill > C.maxfill)
            C.maxfill = fill;
        e = ((int32_t)fill - (int32_t)TARGET) * 2;
        if (e > SERVO_MAX)
            e = SERVO_MAX;
        if (e < -SERVO_MAX)
            e = -SERVO_MAX;
        a += e;
    }
    a += C.acc;
    n = a / 1000;
    if (fill < n) {
        if (!oldest(0))
            ++C.underruns;              /* nothing queued: the host's next poll gets nothing */
        return 0;
    }
    C.acc = a - n * 1000;
    C.lastn = n;
    C.lastfill = fill;
    b = UNC32(bufs[tail]);
    words = hs ? lay->nch : 2;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t *s = slot(C.consumed + i);
        for (uint32_t w = 0; w < words; ++w)
            *b++ = s[w];
    }
    C.consumed += n;
    b = UNC32(bufs[tail]);
    td[0] = 1;                          /* no next */
    td[2] = (uint32_t)(uintptr_t)b;
    td[3] = ((uint32_t)(uintptr_t)b & 0xfffff000u) + 0x1000;   /* a packet may cross a page */
    td[1] = (n * words * 4) << 16 | TD_ACTIVE;   /* last: no IOC, nothing waits for it */
    enqueue(td);
    tail = (tail + 1) & (NSLOT - 1);
    return 1;
}

static void clear_dtds(void)
{
    for (unsigned k = 0; k < NSLOT; ++k) {
        volatile uint32_t *td = dtd(k);
        for (unsigned w = 0; w < 8; ++w)
            td[w] = 0;
        td[0] = 1;
    }
    tail = 0;
}

/* SET_INTERFACE alt 1, seen in the frame interrupt: EP3 up at the port's speed, the ring
 * cleared where the stream starts, the reading cursor far enough behind for the cushion and
 * the first transfers. */
static void ep_up(void)
{
    volatile uint32_t *qh = OT_USB_DQH(3, 1);
    uint32_t at, start;
    ot_usb_flush(EP3IN);
    for (unsigned w = 0; w < 16; ++w)   /* past what stock initialises: its token is garbage */
        qh[w] = 0;
    hs = ot_usb_speed() == OT_USB_HS;
    step = hs ? STEP_HS : STEP_FS;
    qh[0] = 0x60000000u | (hs ? pkt_hs() : PKT_FS) << 16;   /* one packet a frame, no ZLT */
    qh[2] = 1;
    clear_dtds();
    OT_USB_ENDPTCTRL(3) = (OT_USB_ENDPTCTRL(3) & 0x0000ffffu) | 0x00840000u;   /* TX iso, on */
    start = C.produced - TARGET - NSLOT * (step / 1000);
    for (at = start - lag; at != C.produced + CUE_LEAD; ++at) {
        uint32_t *s = slot(at);
        for (unsigned w = 0; w < (hs ? lay->nch : 2u); ++w)
            s[w] = 0;
    }
    C.consumed = start;
    C.lastbank = 0xffffffffu;
    C.acc = 0;
    C.minfill = 0xffffffffu;
    C.maxfill = 0;
    C.anchor = 0;
    await = 1;
    running = 1;
    run_gen = gen;
}

static void ep_down(void)
{
    ot_usb_flush(EP3IN);
    OT_USB_ENDPTCTRL(3) &= 0x0000ffffu;  /* the TX half; RX is EP3 OUT's */
    clear_dtds();
    running = 0;
}

/* The packets, once a block. Brought up, the queue fills and waits for the host's first
 * poll (a transfer retired): the host starts polling some time after SET_INTERFACE (about
 * 460 frames on macOS, octabam measured), and the cursor is set TARGET frames behind the
 * producer then, so the stream starts with the cushion the servo holds. */
static void kick(void)
{
    if (await == 2) {
        unsigned k = 0;
        while (k < NSLOT && (dtd(k)[1] & TD_ACTIVE))
            ++k;
        if (k == NSLOT)
            return;                     /* every one still queued: no poll yet */
        if (C.produced - C.consumed > TARGET) {
            C.anchor = C.produced - C.consumed - TARGET;
            C.consumed = C.produced - TARGET;
        }
        await = 0;
    }
    for (unsigned k = 0; k < NSLOT && build(); ++k)
        ;
    if (await == 1)
        await = 2;
    if (!((OT_USB_ENDPTSTAT | OT_USB_ENDPTPRIME) & EP3IN)) {   /* idle with one queued: prime */
        volatile uint32_t *td = oldest(tail);
        if (td) {
            prime(td);
            ++C.reprimes;
        }
    }
}

/* core-ot's ev_frame_end: once a frame interrupt, 16 frames. Nothing runs while the host
 * has not asked for the stream (and with the layout OFF it never does: usb-ot has no
 * function to give it the request). */
void usba_frame(void)
{
    if (running && (!req || run_gen != gen))
        ep_down();
    if (!req)
        return;
    if (!running)
        ep_up();
    produce();
    C.produced += BLOCK;
    kick();
}

/* ---- the host's requests (the USB interrupt) ------------------------------------------- */

static void down(void)
{
    req = 0;
    ++gen;
    ot_usb_flush(EP3IN);
}

static int set_interface(unsigned rel, unsigned alt)
{
    if (rel == 0)
        return alt == 0 ? 0 : -1;
    if (alt > 1)
        return -1;
    req = alt;                          /* the frame interrupt brings EP3 up or down */
    if (!alt)
        ot_usb_flush(EP3IN);            /* nothing queued answers a poll after alt 0 */
    return 0;
}

static unsigned get_interface(unsigned rel)
{
    return rel ? req : 0;
}

/* SET CUR of the clock's rate. It is read-only, but some hosts set it to the rate they read
 * and give the device up if that STALLs (octabam: the Outbox 8). Stock takes no control OUT
 * data, so the data stage is received here: 44100 is ACKed, another rate STALLs. */
static int set_rate(void)
{
    volatile uint32_t *td = (volatile uint32_t *)EP0_OUT_DTD;
    unsigned n = 100000;
    td[0] = 1;
    td[2] = EP0_OUT_BUF;
    td[1] = 64u << 16 | 0x8000u | TD_ACTIVE;
    OT_USB_DQH(0, 0)[2] = EP0_OUT_DTD;
    OT_USB_ENDPTPRIME = OT_USB_EP_OUT(0);
    while ((td[1] & TD_ACTIVE) && --n)
        ;
    if (td[1] & TD_ACTIVE) {
        ot_usb_flush(OT_USB_EP_OUT(0));
        return 0;                       /* the host never sent it: the stall */
    }
    OT_USB_ENDPTCOMPLETE = OT_USB_EP_OUT(0);   /* before stock's completion loop sees it */
    if (U32(EP0_OUT_BUF) != 0x44ac0000u)
        return 0;
    ot_usb_ack();
    return 1;
}

static int setup(const volatile uint8_t *p)
{
    static const uint8_t cur[4] = { 0x44, 0xac, 0, 0 };   /* 44100 */
    static const uint8_t range[14] = { 1, 0, 0x44, 0xac, 0, 0, 0x44, 0xac, 0, 0, 0, 0, 0, 0 };
    static const uint8_t valid[1] = { 1 };
    unsigned bm = p[0], rq = p[1], cs = p[3];
    if (bm == 0xc0 && rq == 0x55) {     /* vendor, device: the counters */
        ot_usb_send(sizeof C, &C);
        return 1;
    }
    if ((bm != 0xa1 && bm != 0x21) || p[4] != ot_usb_first_interface(&usb_audio) || p[5] != AC_CLOCK)
        return 0;
    if (bm == 0x21)
        return cs == 1 && rq == 1 && OT_USB_WLENGTH == 4 ? set_rate() : 0;
    if (cs == 1 && rq == 1)
        ot_usb_send(sizeof cur, cur);
    else if (cs == 1 && rq == 2)
        ot_usb_send(sizeof range, range);
    else if (cs == 2 && rq == 1)
        ot_usb_send(sizeof valid, valid);
    else
        return 0;
    return 1;
}

static void irq(uint32_t c)
{
    OT_USB_ENDPTCOMPLETE = c & EP3IN;   /* none asked for (no IOC): kept from going stale */
}

const struct ot_usb_function usb_audio = {
    .abi = OT_USB_ABI,
    .name = "USB audio",
    .interfaces = 2,
    .iad_class = 1,                     /* audio, UAC2 (protocol 0x20) */
    .iad_protocol = 0x20,
    .endpoints = EP3IN,
    .descriptors = descriptors,
    .down = down,
    .set_interface = set_interface,
    .get_interface = get_interface,
    .setup = setup,
    .irq = irq,
    .enabled = enabled,
};
