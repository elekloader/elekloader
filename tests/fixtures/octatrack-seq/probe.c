/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A probe of core-ot 0.10's events: counts each and logs its last arguments into `sqp` (read back by a
 * test harness from the emulator's memory).
 */
#include <stdint.h>
#include "octatrack/core.h"

enum { MAGIC, APPLY_N, APPLY_BANK, APPLY_PART, CHAIN_N, CHAIN_PAT, CHAIN_LEN, RESET_N, PANIC_N,
       LEN_N, LEN_TRK, LEN_LEN, LEN_FORCE, WRAP_N, WRAP_NOW, WRAP_OLD, REC_N, REC_TRK, REC_LEN,
       PAT_N, PAT_PEEK_N, PAT_LAST, PAT_FORCE, UI_N, REC_FORCE, CARD_N, CARD_OP, CARD_RES, CARD_INFLIGHT, SWITCH_N, TAB_N, TAB_LAST, DONE_N, DONE_OP, SET_N, SET_FRESH, SET_MAGIC, LFO_N, LFO_LAST, LFO_DIV, WORDS };
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
    return sqp[REC_FORCE] ? (int)sqp[REC_FORCE] : len;    /* poked: every take this long */
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

void sq_ui_tick(void) { ++sqp[UI_N]; }

void sq_card_cmd(const uint8_t *cmd, int result)
{
    ++sqp[CARD_N];
    sqp[CARD_OP] = cmd[5];                      /* the ATA command byte */
    sqp[CARD_RES] = (uint32_t)result;
    if (result != -1)
        ++sqp[CARD_INFLIGHT];
}

void sq_task_switch(void *task) { (void)task; ++sqp[SWITCH_N]; }

void sq_tab_draw(unsigned trk, int x, int y)
{
    ++sqp[TAB_N];
    sqp[TAB_LAST] = trk << 16 | (x & 0xff) << 8 | (y & 0xff);
}

void sq_card_done(const uint8_t *cmd) { ++sqp[DONE_N]; sqp[DONE_OP] = cmd[5]; }

void sq_settings_load(unsigned fresh)
{
    ++sqp[SET_N];
    sqp[SET_FRESH] = fresh;
    sqp[SET_MAGIC] = OT_L(0x80000074);          /* the live copy's "ANDY" */
}

/* With sqp[LFO_DIV] poked nonzero, every LFO steps that many times slower. */
int sq_lfo_step(void *rec, unsigned lfo, int inc)
{
    (void)rec;
    ++sqp[LFO_N];
    sqp[LFO_LAST] = lfo << 28 | ((uint32_t)inc & 0x0fffffff);
    return sqp[LFO_DIV] ? inc / (int)sqp[LFO_DIV] : inc;
}
