/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * usb-ot: the Octatrack's USB companion (OS 1.40C). The stock USB device
 * stack (mass storage only) answers the host; the places in it that seams.s
 * holds ask this file, which asks the functions in `ot_usb_functions`
 * (struct ot_usb_function, elekloader/sdk/include/octatrack/usb.h).
 *
 * With no function in the build every answer is stock's: the configuration
 * tables served are copies of stock's (with the lengths stock writes into
 * its own at run time), and every request goes on to stock's code.
 *
 * Facts from sambanks/octabam's `usb` module and markandrus/octemu's USB
 * MIDI (both MIT): the crossbar setting for the controller's RX FIFO, the
 * flush sequence, the uncached-alias rule. This code is elekloader's own.
 */
#include <stdint.h>
#include "octatrack/usb.h"

#define U8(a)       (*(volatile uint8_t *)(uintptr_t)(a))
#define U32(a)      (*(volatile uint32_t *)(uintptr_t)(a))
#define UNC(p)      ((volatile uint8_t *)OT_USB_UNCACHED(p))

#define MAXF        4               /* functions */
#define CFG         512             /* each configuration table */
#define PORTSC1     0xfc0b0184u
#define STOCK_ZERO  0x400e20a1u     /* stock's one-byte 0 (its GET_INTERFACE reply) */
#define EP0_SEND    ((void (*)(unsigned, const void *))0x4001d498)
#define EP0_ACK     ((void (*)(void))0x4001d524)

extern const struct ot_usb_function *const ot_usb_functions[];   /* pointers, then 0 */

/* What GET_DESCRIPTOR serves (seams.s's selectors, clamps and the device descriptor's
 * reference): stock's until a function is in the build. Written through the uncached alias. */
uint8_t otu_dev[20] __attribute__((aligned(4))) = {
    0x12, 1, 0x00, 0x02, 0, 0, 0, 0x40, 0x35, 0x19, 0x02, 0x00, 0x01, 0x00, 1, 2, 3, 1,
};
#define STOCK_CFG(type, pkt_lo, pkt_hi) \
    9, type, 32, 0, 1, 1, 0, 0xc0, 3, 9, 4, 0, 0, 2, 8, 6, 0x50, 0, \
    7, 5, 0x81, 2, pkt_lo, pkt_hi, 0, 7, 5, 0x01, 2, pkt_lo, pkt_hi, 0
uint8_t otu_cfg[4][CFG] __attribute__((aligned(32))) = {
    { STOCK_CFG(2, 0x40, 0x00) },   /* full speed */
    { STOCK_CFG(2, 0x00, 0x02) },   /* high speed */
    { STOCK_CFG(7, 0x40, 0x00) },   /* OTHER_SPEED at high speed: the full-speed one */
    { STOCK_CFG(7, 0x00, 0x02) },   /* OTHER_SPEED at full speed: the high-speed one */
};
static const uint8_t stock_cfg[2][32] = { { STOCK_CFG(2, 0x40, 0x00) }, { STOCK_CFG(2, 0x00, 0x02) } };
uint32_t otu_len[4] = { 32, 32, 32, 32 };
uint8_t otu_reply[256] __attribute__((aligned(32)));   /* EP0 replies, sent through the alias */
uint8_t otu_offered;

static const struct ot_usb_function *fn[MAXF];
static uint8_t first[MAXF];
static unsigned nfn;

unsigned ot_usb_speed(void)
{
    return (U32(PORTSC1) >> 26) & 3u;
}

unsigned ot_usb_first_interface(const struct ot_usb_function *f)
{
    for (unsigned k = 0; k < nfn; ++k)
        if (fn[k] == f)
            return first[k];
    return 0;
}

void ot_usb_flush(uint32_t bits)
{
    unsigned n = 0x10000, tries = 16;
    while ((OT_USB_ENDPTPRIME & bits) && --n)       /* a prime not yet taken survives a flush */
        ;
    while (tries--) {
        OT_USB_ENDPTFLUSH = bits;
        for (n = 0x10000; (OT_USB_ENDPTFLUSH & bits) && --n;)
            ;
        if (!(OT_USB_ENDPTSTAT & bits))
            break;
    }
    OT_USB_ENDPTCOMPLETE = bits;
}

void ot_usb_ack(void)
{
    EP0_ACK();
}

void ot_usb_stall(void)
{
    OT_USB_ENDPTCTRL(0) |= 0x00010001u;             /* TXS, RXS */
}

void ot_usb_send(unsigned len, const void *data)
{
    volatile uint8_t *r = UNC(otu_reply);
    const uint8_t *d = data;
    unsigned w = OT_USB_WLENGTH;
    if (len > w)
        len = w;
    if (len > sizeof otu_reply)
        len = sizeof otu_reply;
    for (unsigned k = 0; k < len; ++k)
        r[k] = d[k];
    EP0_SEND(len, (const void *)r);
}

/* The controller's arbitration on the SDRAM and SRAM-backdoor slaves, USB first: without it
 * its 16-byte RX FIFO loses packet tails under a busy project (octabam's measurement). */
static void crossbar(void)
{
    U32(0xfc040024) = 0x3ff;                        /* SCM BCR */
    U32(0xfc004200) = 0x60504321;                   /* XBS PRS2 (SDRAM): USB first */
    U32(0xfc004400) = 0x60504321;                   /* XBS PRS4 (SRAM backdoor) */
    U32(0xfc004210) = 0x10;                         /* CRS2, CRS4: fixed priority, after the PRS */
    U32(0xfc004410) = 0x10;
}

/* Stock's configuration header and mass-storage interface, then each function's IAD and
 * descriptors; -> the length. */
static unsigned compose(uint8_t *table, unsigned hs, unsigned type)
{
    volatile uint8_t *o = UNC(table);
    const uint8_t *stock = stock_cfg[hs ? 1 : 0];
    unsigned n = 9, nif = 1, k;
    for (k = 9; k < 32; ++k)                        /* stock's mass-storage interface, 2 endpoints */
        o[n++] = stock[k];
    for (k = 0; k < nfn; ++k) {
        const struct ot_usb_function *f = fn[k];
        if (f->iad_class) {
            o[n] = 8, o[n + 1] = 0x0b, o[n + 2] = first[k], o[n + 3] = f->interfaces;
            o[n + 4] = f->iad_class, o[n + 5] = f->iad_subclass, o[n + 6] = f->iad_protocol, o[n + 7] = 0;
            n += 8;
        }
        n += f->descriptors((uint8_t *)(o + n), first[k], hs ? OT_USB_HS : OT_USB_FS);
        nif += f->interfaces;
    }
    o[0] = 9, o[1] = type, o[2] = n & 0xff, o[3] = n >> 8, o[4] = nif;
    o[5] = 1, o[6] = 0, o[7] = 0xc0, o[8] = 3;
    return n;
}

/* The controller's init (0x4001e030). */
void otu_init(void)
{
    unsigned next = 1, iad = 0, k;
    nfn = 0;
    for (const struct ot_usb_function *const *p = ot_usb_functions; *p && nfn < MAXF; ++p) {
        const struct ot_usb_function *f = *p;
        if (f->abi != OT_USB_ABI || !f->descriptors)
            continue;
        fn[nfn] = f;
        first[nfn++] = next;
        next += f->interfaces;
        iad |= f->iad_class != 0;
    }
    if (!nfn)
        return;                                     /* stock's tables, stock's arbitration */
    crossbar();
    otu_len[2] = compose(otu_cfg[2], 0, 7);
    otu_len[3] = compose(otu_cfg[3], 1, 7);
    otu_len[0] = compose(otu_cfg[0], 0, 2);
    otu_len[1] = compose(otu_cfg[1], 1, 2);
    if (iad) {
        volatile uint8_t *d = UNC(otu_dev);
        d[4] = 0xef, d[5] = 2, d[6] = 1;            /* interface-association composite */
    }
    for (k = 0; k < nfn; ++k)
        if (fn[k]->init)
            fn[k]->init();
}

/* The functions that own interface `iface` / endpoint address `ep`. */
static const struct ot_usb_function *by_interface(unsigned iface, unsigned *rel)
{
    for (unsigned k = 0; k < nfn; ++k)
        if (iface >= first[k] && iface < first[k] + fn[k]->interfaces) {
            *rel = iface - first[k];
            return fn[k];
        }
    return 0;
}

static uint32_t ep_bit(unsigned ep)
{
    return ep & 0x80 ? OT_USB_EP_IN(ep & 15) : OT_USB_EP_OUT(ep & 15);
}

static const struct ot_usb_function *by_endpoint(unsigned ep)
{
    uint32_t b = ep_bit(ep);
    for (unsigned k = 0; k < nfn; ++k)
        if (fn[k]->endpoints & b)
            return fn[k];
    return 0;
}

/* Bus reset (0x4001e91c) and session end (0x4001e952). */
void otu_down(void)
{
    for (unsigned k = 0; k < nfn; ++k)
        if (fn[k]->down)
            fn[k]->down();
}

/* SET_CONFIGURATION (0x4001d9ca), after stock has brought EP1 up. */
void otu_configured(void)
{
    unsigned sp = ot_usb_speed();
    for (unsigned k = 0; k < nfn; ++k) {
        if (fn[k]->down)
            fn[k]->down();
        if (fn[k]->configured)
            fn[k]->configured(sp);
    }
}

/* SET_INTERFACE (0x4001dd04): -> 1 when it was a function's (answered), 0 for stock's. */
unsigned otu_set_interface(void)
{
    unsigned rel;
    const struct ot_usb_function *f = by_interface(OT_USB_WINDEX & 0xff, &rel);
    if (!f)
        return 0;
    if (f->set_interface ? f->set_interface(rel, OT_USB_WVALUE) >= 0 : OT_USB_WVALUE == 0)
        ot_usb_ack();
    else
        ot_usb_stall();
    return 1;
}

/* GET_INTERFACE (0x4001d824): -> the address of the one-byte reply. */
uint32_t otu_get_interface(void)
{
    unsigned rel;
    const struct ot_usb_function *f = by_interface(OT_USB_WINDEX & 0xff, &rel);
    volatile uint8_t *r = UNC(otu_reply);
    if (!f)
        return STOCK_ZERO;
    r[0] = f->get_interface ? f->get_interface(rel) : 0;
    return (uint32_t)(uintptr_t)r;
}

/* CLEAR_FEATURE(ENDPOINT_HALT) (0x4001daec), which stock answers for EP1 only: a
 * function's endpoint gets its stall cleared and its data toggle reset. -> 1 when answered. */
unsigned otu_clear_feature(void)
{
    unsigned ep = OT_USB_WINDEX & 0xff, n = ep & 15;
    if (!by_endpoint(ep) || n == 0 || n > 3)
        return 0;
    if (ep & 0x80)
        OT_USB_ENDPTCTRL(n) = (OT_USB_ENDPTCTRL(n) & ~0x00010000u) | 0x00400000u;   /* TXS off, TXR */
    else
        OT_USB_ENDPTCTRL(n) = (OT_USB_ENDPTCTRL(n) & ~0x00000001u) | 0x00000040u;   /* RXS off, RXR */
    ot_usb_ack();
    return 1;
}

/* A request stock does not answer: offered to its function (by the recipient interface or
 * endpoint), or to every function for a device or other recipient. -> 1 when answered. */
static unsigned offer(void)
{
    unsigned rcpt = OT_USB_SETUP[0] & 0x1f, rel;
    const struct ot_usb_function *f = 0;
    if (rcpt == 1)
        f = by_interface(OT_USB_WINDEX & 0xff, &rel);
    else if (rcpt == 2)
        f = by_endpoint(OT_USB_WINDEX & 0xff);
    if (f)
        return f->setup && f->setup(OT_USB_SETUP) > 0;
    if (rcpt == 1 || rcpt == 2)
        return 0;
    for (unsigned k = 0; k < nfn; ++k)
        if (fn[k]->setup && fn[k]->setup(OT_USB_SETUP) > 0)
            return 1;
    return 0;
}

/* Stock's unknown-request tail (0x4001de64): offered once; a refusal goes on to the stall. */
unsigned otu_setup_unknown(void)
{
    if (offer())
        return 1;
    otu_offered = 1;
    return 0;
}

/* Stock's stall store (0x4001de6e), also reached from its standard requests' refusals. */
unsigned otu_setup_stall(void)
{
    if (otu_offered) {
        otu_offered = 0;
        return 0;
    }
    return offer();
}

/* GET_DESCRIPTOR(CONFIGURATION / OTHER_SPEED)'s length (0x4001d858, 0x4001d896). */
unsigned otu_clamp(unsigned wlength, unsigned other)
{
    unsigned hs = ot_usb_speed() == OT_USB_HS;
    unsigned len = otu_len[other ? (hs ? 2 : 3) : (hs ? 1 : 0)];
    return wlength < len ? wlength : len;
}

/* The USB interrupt's setup-or-completion branch (0x4001e606): each function's completions. */
void otu_irq(void)
{
    uint32_t c = OT_USB_ENDPTCOMPLETE;
    for (unsigned k = 0; k < nfn; ++k)
        if (fn[k]->irq && (c & fn[k]->endpoints))
            fn[k]->irq(c & fn[k]->endpoints);
}
