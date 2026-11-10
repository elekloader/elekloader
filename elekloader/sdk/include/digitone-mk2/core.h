/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef DIGITONE_MK2_CORE_H
#define DIGITONE_MK2_CORE_H
#include <stdint.h>
enum { CORE_KEYS_GROUPS = 7 };
enum { CORE_KEYS_OK, CORE_KEYS_INVALID, CORE_KEYS_BUSY, CORE_KEYS_OVERLAP };
/* core-dn2 0.1: call input functions only from ev_tick/the UI task.
 * set takes the synthetic mask, not the merged physical state. */
int core_keys_set(unsigned group, unsigned mask);
int core_keys_clear(void);
int core_encoder_turn(unsigned encoder, int delta); /* 1..9, -30..30 except 0 */
uint32_t core_millis(void); /* 120 Hz callback, 8/9 ms resolution; unsigned wrap at 2^32 */
unsigned core_keys_lock(void);
void core_keys_unlock(unsigned saved_sr);
void core_additem(void *menu, const void *row);
/* Read-only queue observation: [1] is pending count, [4] capacity minus one. */
extern volatile uint32_t fw_ui_queue[];
unsigned char fw_sysex_send(const void *data, unsigned length, const void *abort, unsigned port);

/* core-dn2 0.3: DSP peek: up to CORE_DSP_PEEK_MAX words from a word-aligned L1 or L2 address, one halfword
 * per audio block (about 3 KB/s). Only a build with a SHARC mod has the DSP side; without one a
 * peek stays busy. Start from any task; the answer arrives in the audio interrupt. */
enum { CORE_DSP_PEEK_MAX = 1024 };
enum { CORE_DSP_PEEK_OK, CORE_DSP_PEEK_INVALID, CORE_DSP_PEEK_BUSY, CORE_DSP_PEEK_REFUSED };
int core_dsp_peek(unsigned address, unsigned count);          /* starts a read; ends the last */
int core_dsp_peek_status(unsigned *halfwords_received, unsigned *halfwords_total);
const volatile unsigned short *core_dsp_peek_data(void);     /* halfword i of the read, low first */
void core_dsp_peek_stop(void);
/* The block handler's span as stock measures it (EMUCLK cycles, the DSP reply's word 0): the
 * latest, and the peak since the last call. It is not the DSP's load. */
void core_dsp_span(unsigned *latest, unsigned *peak);
/* core-dn2 0.3, with a SHARC mod: the DSP address of a record of how busy the DSP is, for the
 * peek. Six words: seq; the idle task's cycles and all cycles over the last 1024 blocks; the same
 * for the busiest such window; seq again (equal seqs: one window). The DSP runs at 1 GHz (666,667
 * cycles a block). Busy (1 - idle / all) is time outside FreeRTOS's idle task, which includes
 * stock's audio task waiting, so it is not headroom. */
enum { CORE_DSP_BUSY_RECORD = 0x2e0630 };
/* core-dn2 0.4: 25-bit words for DSP code. Each frame to the DSP carries the newest CORE_DSP_POSTS posts in
 * its tail (byte 0xa80), after the peek's request: tail word 4 + 2i a post's number, 1, 2, ... wrapping
 * past 0xffffffff to 1 (0: none), and 5 + 2i its word, with the number's low seven bits in bits 25-31, in
 * no order. DSP code takes the ones numbered after the last it took, oldest first, and skips one whose
 * bits 25-31 do not match its number: the DSP was still receiving it. More posts than CORE_DSP_POSTS
 * between two frames lose the oldest. From any task or interrupt. */
enum { CORE_DSP_POSTS = 5, CORE_DSP_POST_MASK = 0x1ffffff };
void core_dsp_post(unsigned word);
/* core-dn2 0.4: each MIDI CC the unit receives, before the stock applies it (the MIDI task): track 0-15,
 * 16 the FX track; flags bit 0 (CORE_MIDI_CC_AUTO), on the auto channel. Nonzero takes it. */
enum { CORE_MIDI_CC_AUTO = 1 };
typedef int32_t (*ev_midi_cc_fn)(int32_t track, int32_t cc, int32_t value, int32_t flags);
#endif
