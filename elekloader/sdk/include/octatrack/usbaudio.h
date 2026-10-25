/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * usb-audio (mods/usb-audio-ot) and its audio in (mods/usb-audio-in-ot). usb-audio is the USB audio
 * function: one AudioControl interface, the clock, and the stream to the host (EP3 IN). Audio in is
 * an add-on: when its record `usba_in` is in the build (usb-audio imports it weakly) and says it is
 * on, usb-audio adds the stream from the host to the same function: its terminals, a third
 * interface (AudioStreaming, alt 0 and 1, EP3 OUT, isochronous, asynchronous) and EP3 IN marked as
 * its implicit feedback. usb-audio answers the requests; the add-on runs the endpoint.
 *
 * Without the add-on usb-audio is what it was: the DSP bus, which audio in needs for its DSP side,
 * takes SPATIALIZER off the FX menus.
 */
#ifndef ELEKLOADER_OCTATRACK_USBAUDIO_H
#define ELEKLOADER_OCTATRACK_USBAUDIO_H

#include <stdint.h>

#define USBA_IN_ABI         1u

struct usba_in {
    uint32_t abi;                       /* USBA_IN_ABI */
    /* usb-audio's enabled(), once at the controller's init (an early setting is in place): the
     * host's channels this boot, 0 (none), 2 or 4. Not called when usb-audio itself is off. */
    unsigned (*channels)(void);
    /* the host's alternate setting on the IN interface: SET_INTERFACE, and 0 at a bus reset or a
     * session end (the USB interrupt) */
    void (*set_alt)(unsigned alt);
    unsigned (*get_alt)(void);
    int (*setup)(const volatile uint8_t *pkt);  /* a request usb-audio does not answer: >0 answered */
};

extern const char usba_group[];         /* usb-audio's settings heading, "USB AUDIO" */
unsigned usba_in_packet(unsigned speed);    /* EP3 OUT's wMaxPacketSize at `speed`, as described */

#endif
