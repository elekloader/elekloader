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
#endif
