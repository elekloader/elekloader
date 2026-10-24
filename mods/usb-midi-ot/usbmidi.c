/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * USB MIDI for the Octatrack, a function of usb-ot: class-compliant USB-MIDI
 * in and out on the unit's USB port, mirroring the DIN ports. Interfaces:
 * AudioControl and MIDIStreaming under an interface association; EP2 bulk
 * both ways (64 bytes at full speed, 512 at high speed).
 *
 * The stock firmware carries a dormant USB-MIDI half (EP2's bring-up, its
 * receive prime and its transmit encoder); this calls it, as markandrus's
 * octemu first did. What octabam's `usb` module found (MIT): the 512-byte
 * receive transfer at high speed, room in the firmware's 32-byte MIDI FIFO
 * before each event (else the framer runs once), the flush before EP2 is
 * brought up again, transmit stopped at a bus reset or a cable pull, and
 * each USB clock (0xF8) timestamped as the DIN interrupt does. The code is
 * elekloader's own.
 */
#include <stdint.h>
#include "octatrack/usb.h"

#define U32(a)          (*(volatile uint32_t *)(uintptr_t)(a))

#define EP2_INIT        ((void (*)(void))0x4001d0e4)                 /* EP2's dQHs, 64 bytes, RX primed */
#define EP2_RX_PRIME    ((void (*)(uint32_t))0x4001d184)             /* (buf): a 64-byte receive */
#define ENCODER         ((void (*)(unsigned, const uint8_t *))0x4001d204)  /* (len, bytes): event packets out */
#define RX_ENQUEUE      ((void (*)(unsigned))0x40092bbc)             /* (byte): the MIDI FIFO, as the UART's */
#define RX_BUF          0x4ecc9000u     /* the receive buffer (its 4 KB page alone) */
#define RX_DTD          0x4ec953e0u     /* next pointer; token at +4, page 0 at +8 */
#define QH_PTR_EP2OUT   0x46c8ce14u     /* the long holding EP2 OUT's dQH */
#define FIFO_COUNT      0x46100b7cu     /* bytes in the firmware's 32-byte MIDI FIFO */
#define DTCN0           0xfc07000cu     /* DMA timer 0's count */
#define CLK_T           0x46c8345au     /* the count at the last 0xF8 */
#define CLK_DT          0x46c83466u     /* the count between the last two: the clock handler's tempo */
#define TD_512          0x02008080u     /* 512 bytes, IOC, ACTIVE */
#define TD_ERRORS       0x68u           /* halted, data buffer error, transaction error */
#define EP2             (OT_USB_EP_OUT(2) | OT_USB_EP_IN(2))
#define CAP             256

void usbmidi_run_framer(void);          /* framer.s: one pass of the framer, as its interrupt */

static volatile uint8_t up, tx_busy, hs;
static uint8_t acc[CAP], snd[CAP];
static volatile unsigned acc_len;
volatile uint32_t usbmidi_drops, usbmidi_errs;
static const uint8_t cin_len[16] = { 0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1 };

static unsigned put(uint8_t *o, const uint8_t *b, unsigned n)
{
    for (unsigned k = 0; k < n; ++k)
        o[k] = b[k];
    return n;
}

static unsigned descriptors(uint8_t *o, unsigned first, unsigned speed)
{
    unsigned pkt = speed == OT_USB_HS ? 512 : 64, n = 0;
    const uint8_t ac[] = { 9, 4, first, 0, 0, 1, 1, 0, 0,
                           9, 0x24, 1, 0x00, 0x01, 9, 0, 1, first + 1 };
    const uint8_t ms[] = { 9, 4, first + 1, 0, 2, 1, 3, 0, 0,
                           7, 0x24, 1, 0x00, 0x01, 37, 0,           /* MS header */
                           6, 0x24, 2, 1, 1, 0,                     /* IN jack, embedded 1 */
                           6, 0x24, 2, 2, 2, 0,                     /* IN jack, external 2 */
                           9, 0x24, 3, 1, 3, 1, 2, 1, 0,            /* OUT jack, embedded 3 */
                           9, 0x24, 3, 2, 4, 1, 1, 1, 0 };          /* OUT jack, external 4 */
    const uint8_t out[] = { 9, 5, 0x02, 2, pkt & 0xff, pkt >> 8, 0, 0, 0, 5, 0x25, 1, 1, 1 };
    const uint8_t in[] = { 9, 5, 0x82, 2, pkt & 0xff, pkt >> 8, 0, 0, 0, 5, 0x25, 1, 1, 3 };
    n += put(o + n, ac, sizeof ac);
    n += put(o + n, ms, sizeof ms);
    n += put(o + n, out, sizeof out);
    n += put(o + n, in, sizeof in);
    return n;
}

static void flush_ep2(void)
{
    ot_usb_flush(EP2);
    OT_USB_DQH(2, 0)[3] = 0;            /* the overlay tokens: a flushed dTD still reads ACTIVE */
    OT_USB_DQH(2, 1)[3] = 0;
}

/* A bus reset, a session end (the cable pulled), and before SET_CONFIGURATION: EP2 down,
 * what was queued for the old session dropped. */
static void down(void)
{
    unsigned sr = ot_usb_irq_off();
    up = 0;
    tx_busy = 0;
    acc_len = 0;
    ot_usb_irq_restore(sr);
    flush_ep2();
}

static void prime512(void)
{
    U32(RX_DTD + 4) = TD_512;
    U32(RX_DTD + 8) = RX_BUF;
    U32(RX_DTD) = 0xdead0001u;          /* no next dTD */
    U32(U32(QH_PTR_EP2OUT) + 8) = RX_DTD;
    OT_USB_ENDPTPRIME |= OT_USB_EP_OUT(2);
}

static void configured(unsigned speed)
{
    EP2_INIT();
    hs = speed == OT_USB_HS;
    if (hs) {                           /* 512-byte packets: the dQHs, and the receive re-primed */
        OT_USB_DQH(2, 0)[0] = 0x22000000u;
        OT_USB_DQH(2, 1)[0] = 0x22000000u;
        ot_usb_flush(OT_USB_EP_OUT(2));
        OT_USB_DQH(2, 0)[3] = 0;
        prime512();
    }
    tx_busy = 0;
    up = 1;
}

/* EP2 IN free and something queued: the queue, as one transfer, through the encoder. */
static void kick(void)
{
    unsigned sr = ot_usb_irq_off(), n;
    if (tx_busy || !acc_len) {
        ot_usb_irq_restore(sr);
        return;
    }
    tx_busy = 1;
    n = acc_len;
    for (unsigned k = 0; k < n; ++k)
        snd[k] = acc[k];
    acc_len = 0;
    ot_usb_irq_restore(sr);
    ENCODER(n, snd);
}

static void append(const uint8_t *msg, unsigned len)
{
    unsigned sr;
    if (!up || !len)
        return;
    sr = ot_usb_irq_off();
    if (acc_len + len > CAP) {
        ++usbmidi_drops;
    } else {
        for (unsigned k = 0; k < len; ++k)
            acc[acc_len + k] = msg[k];
        acc_len += len;
    }
    ot_usb_irq_restore(sr);
    kick();
}

/* core-ot's ev_midi_out and ev_midi_out_rt: what the DIN port sends, the USB port sends too. */
void usbmidi_out(const unsigned char *msg, unsigned len)
{
    append(msg, len);
}

void usbmidi_out_rt(unsigned byte)
{
    uint8_t b = byte;
    append(&b, 1);
}

/* USB-MIDI event packets in -> the firmware's MIDI FIFO, as the DIN interrupt fills it. */
static void decode(unsigned n)
{
    const volatile uint8_t *p = (const volatile uint8_t *)RX_BUF;
    for (unsigned e = 0; e < n / 4; ++e, p += 4) {
        unsigned k = cin_len[p[0] & 15], sr;
        if (!k)
            continue;
        for (;;) {                      /* room for the whole event, or the framer takes a byte */
            sr = ot_usb_irq_off();
            if (U32(FIFO_COUNT) + k <= 32)
                break;
            ot_usb_irq_restore(sr);
            usbmidi_run_framer();
        }
        for (unsigned i = 0; i < k; ++i) {
            unsigned b = p[1 + i];
            if (b == 0xf8) {
                uint32_t t = U32(DTCN0);
                U32(CLK_DT) = t - U32(CLK_T);
                U32(CLK_T) = t;
            }
            RX_ENQUEUE(b);
        }
        ot_usb_irq_restore(sr);
    }
}

static void irq(uint32_t c)
{
    if (c & OT_USB_EP_IN(2)) {          /* the transfer is done: the next */
        OT_USB_ENDPTCOMPLETE = OT_USB_EP_IN(2);
        tx_busy = 0;
        kick();
    }
    if (c & OT_USB_EP_OUT(2)) {
        uint32_t tok;
        unsigned got;
        OT_USB_ENDPTCOMPLETE = OT_USB_EP_OUT(2);
        tok = U32(RX_DTD + 4);
        got = (hs ? 512u : 64u) - ((tok >> 16) & 0x7fff);
        if (tok & TD_ERRORS)
            ++usbmidi_errs;
        else if (up && got <= 512)
            decode(got);
        if (!up)
            return;
        if (hs)
            prime512();
        else
            EP2_RX_PRIME(RX_BUF);
    }
}

const struct ot_usb_function usb_midi = {
    .abi = OT_USB_ABI,
    .name = "USB MIDI",
    .interfaces = 2,
    .iad_class = 1,                     /* audio (AudioControl + MIDIStreaming) */
    .endpoints = EP2,
    .descriptors = descriptors,
    .configured = configured,
    .down = down,
    .irq = irq,
};
