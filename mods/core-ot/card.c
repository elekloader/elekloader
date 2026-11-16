/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core for the Octatrack, OS 1.40C: ev_card and ev_usb_disk's handlers (core-ot 0.41, card.s). */
#include <stdint.h>

extern void (*const ev_card[])(unsigned, int, unsigned);
extern void (*const ev_usb_disk[])(unsigned);

void core_card_c(unsigned mode, int result)
{
    unsigned state = *(volatile uint32_t *)0x460d1cb8;
    for (void (*const *f)(unsigned, int, unsigned) = ev_card; *f; ++f)
        (*f)(state, result, mode);
}

void core_usb_c(unsigned on)
{
    for (void (*const *f)(unsigned) = ev_usb_disk; *f; ++f)
        (*f)(on);
}
