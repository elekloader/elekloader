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
/* core-dn2 0.5: for a mod that links more mods on the unit (TELOS); mods/core-dn2/README.md has the sites. */
extern void fw_fillrect(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t colour);
/* printf text (128 bytes); flags 0 plain, bit 1 centre, bit 2 right-align. */
extern int32_t fw_textf(void *bmp, const void *font, int32_t x, int32_t y, uint32_t flags, const char *fmt, ...);
extern const char fw_font5[];                           /* the stock small font */
extern void fw_blit(void *dst, const void *src, int32_t x, int32_t y, int32_t centre);
extern uint32_t fw_op_new(uint32_t size);               /* the firmware's heap */
extern void fw_invalidate(void *view);                  /* the view is drawn again */
/* The card, 512-byte sectors, from ev_card_ready on, from a task (never an interrupt): 0, or < 0. Stock
 * writes nothing from sector 0x458000 up; ElekLabs digikit keeps its store from 0x800000. */
extern int32_t fw_card_read(uint32_t sector, uint32_t bytes, void *buf);
extern int32_t fw_card_write(uint32_t sector, uint32_t bytes, const void *buf);
/* ev_card_ready: the card is up (status 0), nothing else is: the init task, before the project and the UI
 * task. A handler must not take the firmware's heap. */
typedef void (*ev_card_ready_fn)(int32_t status);
/* Project slots, in a background worker task. ev_project_slot_write: slot (0-127; 128 the working copy)
 * is about to be written; ev_project_saved: that write returned (ok nonzero: written); ev_project_loaded:
 * a LOAD PROJECT finished (err 0: LOAD OK). The boot's own load of the working copy calls none. */
typedef void (*ev_project_slot_write_fn)(void *project, int32_t slot, int32_t progress);
typedef void (*ev_project_saved_fn)(void *project, int32_t slot, int32_t ok);
typedef void (*ev_project_loaded_fn)(int32_t slot, int32_t err);
/* ev_crash: an exception the firmware does not handle, interrupts masked; the stock printer runs after the
 * handlers. A handler that wants the unit back calls core_restart_now. */
typedef void (*ev_crash_fn)(void *frame);
extern void core_restart(void) __attribute__((noreturn));       /* the firmware's own restart, from a task */
extern void core_restart_now(void) __attribute__((noreturn));   /* at once: from ev_crash, or interrupts masked */
/* The OS's fourteen static tasks: {count, the init task's row, the TCB's saved-SP offset}, then {TCB,
 * stack's low end, stack size} each. */
struct core_task { unsigned long tcb, stack, size; };
struct core_tasks { unsigned long n, init, sp_at; struct core_task t[16]; };
extern const struct core_tasks core_tasks;
#endif
