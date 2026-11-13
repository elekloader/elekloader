/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A probe of core-ot 0.10's events: counts each and logs its last arguments into `sqp` (read back by a
 * test harness from the emulator's memory).
 */
#include <stdint.h>
#include "octatrack/core.h"

enum { MAGIC, APPLY_N, APPLY_BANK, APPLY_PART, CHAIN_N, CHAIN_PAT, CHAIN_LEN, RESET_N, PANIC_N,
       LEN_N, LEN_TRK, LEN_LEN, LEN_FORCE, WRAP_N, WRAP_NOW, WRAP_OLD, REC_N, REC_TRK, REC_LEN,
       PAT_N, PAT_PEEK_N, PAT_LAST, PAT_FORCE, WORDS };
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

/* With sqp[LEN_FORCE] poked nonzero, every track wraps at that length. */
int sq_track_length(unsigned trk, int len)
{
    ++sqp[LEN_N];
    sqp[LEN_TRK] = trk;
    sqp[LEN_LEN] = (uint32_t)len;
    return sqp[LEN_FORCE] ? (int)sqp[LEN_FORCE] : len;
}

void sq_pattern_wrap(unsigned bank, unsigned pattern, unsigned old_bank, unsigned old_pattern)
{
    ++sqp[WRAP_N];
    sqp[WRAP_NOW] = bank << 8 | pattern;
    sqp[WRAP_OLD] = old_bank << 8 | old_pattern;
}

int sq_rec_length(unsigned trk, int len)
{
    ++sqp[REC_N];
    sqp[REC_TRK] = trk;
    sqp[REC_LEN] = (uint32_t)len;
    return len;
}

/* With sqp[PAT_FORCE] poked nonzero, track 0 plays pattern PAT_FORCE - 1. */
int sq_track_pattern(unsigned trk, int pattern, unsigned step, unsigned peek)
{
    ++sqp[peek ? PAT_PEEK_N : PAT_N];
    sqp[PAT_LAST] = trk << 24 | (pattern & 0xff) << 16 | step;
    if (trk == 0 && sqp[PAT_FORCE])
        return (int)sqp[PAT_FORCE] - 1;
    return pattern;
}
