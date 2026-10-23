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
#endif
