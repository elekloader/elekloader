/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * usb-ot 0.3: the DSP feed (elekloader/sdk/include/octatrack/usb.h). seams.s's state-7 seam runs
 * ev_dsp_feed's handlers once a frame; one of them may start one more host-port transfer to DSP
 * core 0 here. It is not USB's: USB audio in is its first user, and it is kept apart so that it can
 * move to the DSP bus.
 *
 * The transfer is set up the way the chain's own states set theirs (0x40004aaa, say): eDMA
 * channel 0's source and its 64-byte minor loops, then the DSP's host port (HI08) told where and how
 * many words, then the host command that takes them, then the loop count and the start. Its
 * destination, $6320, lands at the bank + $320 after the DSP's host-DMA mask. (sambanks/octabam's
 * USB audio in found the place and the form, MIT; the code is elekloader's own.)
 */
#include <stdint.h>
#include "octatrack/usb.h"

#define U8(a)       (*(volatile uint8_t *)(uintptr_t)(a))
#define U16(a)      (*(volatile uint16_t *)(uintptr_t)(a))
#define U32(a)      (*(volatile uint32_t *)(uintptr_t)(a))

#define CORE_SEL    U8(0xfc0a400c)      /* which DSP core the host port reaches: 0 */
#define TCD0_SADDR  U32(0xfc045000)     /* eDMA channel 0: source */
#define TCD0_NBYTES U32(0xfc045008)     /* bytes a minor loop */
#define TCD0_CITER  U16(0xfc045014)     /* minor loops, current and */
#define TCD0_BITER  U16(0xfc04501c)     /* at the start */
#define EDMA_SSRT   U8(0xfc04401e)      /* channel n's start */
#define HI_ICR      U16(0x20000000)
#define HI_CVR      U16(0x20000004)
#define HI_TX       U16(0x2000001c)
#define DSP_AT      0x6320u
#define LOOP        64u                 /* bytes a minor loop: 32 words */

volatile uint8_t otu_feeding;           /* a feed is on its way (seams.s clears it at its completion) */
volatile uint8_t otu_in_feed;           /* seams.s is running ev_dsp_feed's handlers */
volatile uint32_t ot_dsp_fed;

int ot_dsp_feed(const volatile void *block, unsigned words)
{
    if (!otu_in_feed || otu_feeding || !words || words > OT_DSP_FEED_MAX || words % (LOOP / 2))
        return 0;
    CORE_SEL = 0;
    TCD0_NBYTES = LOOP;
    TCD0_SADDR = (uint32_t)(uintptr_t)block;
    HI_ICR = 0x81;
    HI_TX = DSP_AT;
    HI_TX = words - 1;
    HI_CVR = 0x88;
    TCD0_CITER = 0x8000 | (2 * words / LOOP);
    TCD0_BITER = 0x8000 | (2 * words / LOOP);
    otu_feeding = 1;
    EDMA_SSRT = 0;
    return 1;
}
