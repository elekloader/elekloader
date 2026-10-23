/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * usb-ot (the Octatrack's USB companion, mods/usb-ot): a USB function (USB
 * MIDI, USB audio, ...) is a mod that contributes a pointer to its
 * `struct ot_usb_function` to the `ot_usb_functions` table:
 *
 *     "contribute": [{"to": "ot_usb_functions", "order": 20, "data": "00000000",
 *                     "relocs": [[0, "abs32", "sym:my_function", 0]]}],
 *     "requires": ["core", "usb"],
 *     "resources": {"names": ["usb:ep2"]}
 *
 * The companion owns the stock USB device stack's places once. At the
 * controller's init it numbers the functions' interfaces after the stock
 * mass-storage interface 0, in table order, and composes the configuration
 * descriptors (full speed, high speed, and each as OTHER_SPEED) from what each
 * function writes. It routes SET_INTERFACE, GET_INTERFACE, class and vendor
 * requests, CLEAR_FEATURE and endpoint completions to the function they
 * belong to. Claim each endpoint you use by name (usb:ep2, usb:ep3-in,
 * usb:ep3-out): two functions that want the same one are refused together.
 *
 * Memory the controller reads or writes (dTDs, buffers, replies) must be
 * reached through the uncached alias, OT_USB_UNCACHED(p): the controller
 * does not see the data cache.
 */
#ifndef ELEKLOADER_OCTATRACK_USB_H
#define ELEKLOADER_OCTATRACK_USB_H

#include <stdint.h>

#define OT_USB_ABI          2u          /* the record below; a function built for 1 (no `enabled`) is always on */
#define OT_USB_ABI_1        1u
#define OT_USB_FS           0u          /* speeds, as PORTSC1 bits 27:26 read */
#define OT_USB_HS           2u

/* An endpoint's bit in the ENDPT* registers (ENDPTCOMPLETE, ENDPTPRIME, ENDPTFLUSH, ...). */
#define OT_USB_EP_OUT(n)    (1u << (n))
#define OT_USB_EP_IN(n)     (1u << (16 + (n)))

#define OT_USB_REG(off)     (*(volatile uint32_t *)(0xfc0b0000u + (off)))
#define OT_USB_ENDPTPRIME   OT_USB_REG(0x1b0)
#define OT_USB_ENDPTFLUSH   OT_USB_REG(0x1b4)
#define OT_USB_ENDPTSTAT    OT_USB_REG(0x1b8)
#define OT_USB_ENDPTCOMPLETE OT_USB_REG(0x1bc)
#define OT_USB_ENDPTCTRL(n) OT_USB_REG(0x1c0 + 4 * (n))

/* A queue head: endpoint n, IN or OUT (the stock list, 64 bytes each). */
#define OT_USB_DQH(n, in)   ((volatile uint32_t *)(0x4ec94800u + (2u * (n) + ((in) ? 1u : 0u)) * 64u))
#define OT_USB_UNCACHED(p)  ((void *)((uintptr_t)(p) | 0x08000000u))

/* The setup packet being answered, as it came (little-endian fields). */
#define OT_USB_SETUP        ((const volatile uint8_t *)0x46c8ce08u)
#define OT_USB_W(o)         ((unsigned)OT_USB_SETUP[o] | (unsigned)OT_USB_SETUP[(o) + 1] << 8)
#define OT_USB_WVALUE       OT_USB_W(2)
#define OT_USB_WINDEX       OT_USB_W(4)
#define OT_USB_WLENGTH      OT_USB_W(6)

struct ot_usb_function {
    uint32_t abi;                       /* OT_USB_ABI; OT_USB_ABI_1 ends the record before `enabled` */
    const char *name;
    uint8_t interfaces;                 /* how many it adds */
    uint8_t iad_class, iad_subclass, iad_protocol;  /* an interface association before them; class 0: none */
    uint32_t endpoints;                 /* its ENDPT* bits: CLEAR_FEATURE and completions go by them */
    /* its interface and endpoint descriptors (no IAD), its first interface number `first`, at
     * `speed`; -> their length. Called at the controller's init, for each speed. */
    unsigned (*descriptors)(uint8_t *out, unsigned first, unsigned speed);
    void (*init)(void);                 /* the controller's init, after the composition */
    void (*configured)(unsigned speed); /* SET_CONFIGURATION, after down: bring its endpoints up */
    void (*down)(void);                 /* bus reset, session end, and before configured */
    int (*set_interface)(unsigned rel, unsigned alt);   /* its interface `rel` (0 = its first); <0 STALLs */
    unsigned (*get_interface)(unsigned rel);
    int (*setup)(const volatile uint8_t *pkt);  /* a request for it: >0 answered (sent or ACKed), else 0 */
    void (*irq)(uint32_t complete);     /* ENDPTCOMPLETE & its endpoints: clear the bits it takes */
    /* ABI 2. Called once at the controller's init, before the function's other calls. A 0 result
     * leaves the function out of this boot: no interfaces, no descriptors, none of the calls above.
     * A null pointer is always on. An early setting (OT_SET_EARLY) is in place by then, so this is
     * where to read one and latch what descriptors() needs; it takes effect at the next boot. */
    int (*enabled)(void);
};

/* What usb-ot exports. */
unsigned ot_usb_speed(void);
unsigned ot_usb_first_interface(const struct ot_usb_function *f);
void ot_usb_flush(uint32_t endpoints);              /* the ChipIdea flush, bounded, then their completions cleared */
void ot_usb_ack(void);                              /* EP0's zero-length status */
void ot_usb_stall(void);                            /* EP0 STALL, both directions */
void ot_usb_send(unsigned len, const void *data);   /* EP0 IN data, min(len, wLength), copied to its reply buffer */
unsigned ot_usb_irq_off(void);                      /* -> the SR before, with interrupts masked */
void ot_usb_irq_restore(unsigned sr);

#endif
