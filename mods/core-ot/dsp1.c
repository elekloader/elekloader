/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * core-ot 0.39: one more block a frame to DSP core 1 (docs/ADAPTING.md, "A block to DSP core 1 on the
 * Octatrack"; the declarations are in elekloader/sdk/include/octatrack/core.h, the site in dsp1.s).
 * The transfer is set up as the chain's own states set theirs (state 2, 0x4000495c): the host port to
 * core 1, eDMA channel 0's source and 64-byte minor loops, the DSP told where and how many words
 * (host command 0x88 takes them), then the loop count and the start; usb-ot's feed.c does the same for
 * core 0.
 */
#include <stdint.h>
#include "octatrack/core.h"

#define U8(a)       (*(volatile uint8_t *)(uintptr_t)(a))
#define U16(a)      (*(volatile uint16_t *)(uintptr_t)(a))
#define U32(a)      (*(volatile uint32_t *)(uintptr_t)(a))
#define LOOP        64u                 /* bytes a minor loop: 32 words */

volatile uint8_t ot_dsp1_feeding;       /* a transfer is on its way (dsp1.s clears it at its completion) */
volatile uint8_t ot_dsp1_in_feed;       /* dsp1.s is running ev_dsp_feed1's handlers */
volatile uint32_t ot_dsp1_fed;

/* The eDMA reads in 16-byte bursts and faults on a source that is not 16-byte aligned (Modwerk's
 * hardware traps; ot_emu does not model it), so such a block is refused. */
int ot_dsp_feed1(const volatile void *block, unsigned words, unsigned dsp_at)
{
    if (!ot_dsp1_in_feed || ot_dsp1_feeding || !words || words > OT_DSP_FEED1_MAX || words % (LOOP / 2) ||
        ((uintptr_t)block & 15) || dsp_at > 0xffffu)
        return 0;
    U8(0xfc0a400c) = 1;                 /* the host port reaches core 1 */
    U32(0xfc045008) = LOOP;
    U32(0xfc045000) = (uint32_t)(uintptr_t)block;
    U16(0x20000000) = 0x81;
    U16(0x2000001c) = (uint16_t)dsp_at;
    U16(0x2000001c) = (uint16_t)(words - 1);
    U16(0x20000004) = 0x88;
    U16(0xfc045014) = (uint16_t)(0x8000 | (2 * words / LOOP));
    U16(0xfc04501c) = (uint16_t)(0x8000 | (2 * words / LOOP));
    ot_dsp1_feeding = 1;
    U8(0xfc04401e) = 0;                 /* start channel 0 */
    return 1;
}
