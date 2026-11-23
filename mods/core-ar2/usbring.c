/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.19: the USB-MIDI sender's early wrap (README, "The USB-MIDI
 * sender's wrap"). Every address is OS 1.74's.
 *
 * Stock's sender (0x400029e0) packs each 1 to 3 MIDI bytes into a 4-byte
 * event in a 4 KB ring (0x4b80c000, next byte 0x413db25c), and the USB
 * controller sends each queued transfer straight from the ring. The count
 * of bytes queued and not yet sent is 0x413db258. For a call of more than
 * 0x2f bytes with the index above 0xfbf, stock skips the ring's tail: it
 * waits only until the unsent count is below index - 0x3f and sets the
 * index to 0 (0x40002a06..0x40002a2a). Its room test (unsent + batch <=
 * 0xffb, 0x40002a4e) counts bytes and never their place, so with the tail
 * skipped the new events from 0 can reach the oldest unsent ones and
 * overwrite up to 0x40 bytes before the controller has read them: the host
 * gets the event 1,008 events later in their place.
 *
 * core_usb_send_site runs before stock's code for such a call and waits
 * here until nothing is unsent, so the wrap starts on an empty ring and
 * stock's room test holds again. The wait ends early, and stock's own wait
 * takes over unchanged, when the port is not configured or USB is off (a
 * cancel or the configure brings the count to 0 by itself) or after
 * WRAP_TICKS; the counters say which ended it.
 */

#include <stdint.h>

#define TX_LEVEL (*(volatile int *)0x413db258)          /* bytes queued, not yet sent */
#define PORT_STATE (*(volatile int *)0x413db254)        /* 1: configured */
#define USB_ON (*(volatile unsigned *)0xfc0b01c4)
#define DTCN0 (*(volatile unsigned *)0xfc07000c)        /* DMA timer 0, 132 MHz */
#define WRAP_TICKS 132000000u                           /* 1 s */

struct {
    char magic[4];
    uint32_t wraps;     /* calls that would wrap early */
    uint32_t waited;    /* of those, calls that found bytes unsent */
    uint32_t emptied;   /* waits that ended with nothing unsent */
    uint32_t port_down; /* waits ended by the port or USB going down */
    uint32_t timed_out; /* waits ended by WRAP_TICKS: stock's own wait then */
    uint32_t max_ticks; /* the longest wait, in DMA timer 0 ticks */
} core_usb_wrap = {{'U', 'W', 'R', 'P'}};

/* from core_usb_send_site: the caller's task, the sender's mutex held, the
 * USB interrupt free to complete transfers. Uses d0, d1, a0, a1 only. */
void core_usb_wrap_wait(void)
{
    unsigned t0, t;
    core_usb_wrap.wraps++;
    if (TX_LEVEL == 0)
        return;
    core_usb_wrap.waited++;
    t0 = DTCN0;
    for (;;) {
        t = DTCN0 - t0;
        if (TX_LEVEL == 0) {
            core_usb_wrap.emptied++;
            break;
        }
        if (PORT_STATE != 1 || !USB_ON) {
            core_usb_wrap.port_down++;
            break;
        }
        if (t > WRAP_TICKS) {
            core_usb_wrap.timed_out++;
            break;
        }
    }
    if (t > core_usb_wrap.max_ticks)
        core_usb_wrap.max_ticks = t;
}
