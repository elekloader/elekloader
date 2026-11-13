/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A probe of core-ot 0.10's events: counts each and logs its last arguments into `sqp` (read back by a
 * test harness from the emulator's memory).
 */
#include <stdint.h>
#include "octatrack/core.h"

enum { MAGIC, APPLY_N, APPLY_BANK, APPLY_PART, CHAIN_N, CHAIN_PAT, CHAIN_LEN, RESET_N, PANIC_N, WORDS };
volatile uint32_t sqp[WORDS] = { 0x53515031 };  /* 'SQP1' */

void sq_part_apply(unsigned bank, unsigned part)
{
    ++sqp[APPLY_N];
    sqp[APPLY_BANK] = bank;
    sqp[APPLY_PART] = part;
}

void sq_chain_add(unsigned pattern)
{
    ++sqp[CHAIN_N];
    sqp[CHAIN_PAT] = pattern;
    sqp[CHAIN_LEN] = OT_L(0x8000654e);          /* the chain's length before the routine */
}

void sq_seq_reset(void) { ++sqp[RESET_N]; }
void sq_midi_panic(void) { ++sqp[PANIC_N]; }
