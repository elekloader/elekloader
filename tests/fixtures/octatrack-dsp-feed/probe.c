/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe for usb-ot's ot_dsp_feed (tests/probe_octatrack_dsp_feed.py): an ev_dsp_feed handler that,
 * when df_cfg.on is set, offers a 32-word block at df_cfg.offset bytes from a 32-byte aligned
 * buffer (through the uncached alias) and logs what ot_dsp_feed answered and the eDMA channel 0's
 * source address afterwards. The probe writes the configuration into the image's RAM before the
 * sequencer starts. Not a mod to ship. */
#include <stdint.h>
#include "octatrack/usb.h"

#define TCD0_SADDR  (*(volatile uint32_t *)0xfc045000)
#define UNCACHED    0x08000000u

struct df_config {
    uint32_t on;        /* 1: offer a block each frame */
    uint32_t offset;    /* bytes from the buffer's start */
};
struct df_config df_cfg;

uint32_t df_buf[48] __attribute__((aligned(32)));
uint32_t df_calls;      /* handler calls */
uint32_t df_taken;      /* ot_dsp_feed answered 1 */
uint32_t df_refused;    /* it answered 0 */
uint32_t df_src;        /* eDMA channel 0's source address after the last call */
uint32_t df_src_moved;  /* calls after which it was the offered block's */

void df_feed(void)
{
    uintptr_t b = (uintptr_t)df_buf + UNCACHED + df_cfg.offset;
    int r;

    df_calls++;
    if (!df_cfg.on)
        return;
    r = ot_dsp_feed((const volatile void *)b, 32);
    if (r)
        df_taken++;
    else
        df_refused++;
    df_src = TCD0_SADDR;
    if (df_src == b)
        df_src_moved++;
}