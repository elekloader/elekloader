/* SPDX-License-Identifier: GPL-2.0-or-later */
/* TELOS for the Octatrack, research probe: can the loader read its files at the card's first mount?
 * On the first ev_card with a card mounted (state 1, result 0) it reads /ELEKLOADER/TELOS/ACTIVE (its
 * first 16 bytes) and /ELEKLOADER/TELOS/MODS/BIG.BIN (256 KB) with core's file calls, in the task that
 * mounted the card.
 * tp[0] calls, [1] state<<16|mode, [2] ACTIVE open, [3] its read, [4..7] its bytes, [8] BIG open,
 * [9] its read, [10] its word sum, [11] the engine's job switch count at the time (0: no job yet). */
#include "octatrack/core.h"

volatile uint32_t tp[24];

static struct ot_file f;
static uint32_t big[65536];

void __attribute__((noinline)) tp_begin(void) { __asm__ volatile("" ::: "memory"); }
void __attribute__((noinline)) tp_end(void) { __asm__ volatile("" ::: "memory"); }

void tp_card(unsigned state, int result, unsigned mode)
{
    tp[0]++;
    if (tp[0] > 1 && tp[2])
        return;
    tp[1] = state << 16 | mode;
    if (state != 1 || result != 0)
        return;
    tp_begin();
    tp[2] = (uint32_t)ot_file_open(&f, "/ELEKLOADER/TELOS/ACTIVE", "r") | 0x80000000u;
    if ((int32_t)(tp[2] << 1) >= 0) {
        for (int q = 0; q < 6; q++) tp[12 + q] = ((uint32_t *)f.obj)[q];
        tp[3] = (uint32_t)ot_file_read(&f, (void *)&tp[4], 16);
        for (int q = 0; q < 6; q++) tp[18 + q] = ((uint32_t *)f.obj)[q];
        ot_file_close(&f);
    }
    tp[8] = (uint32_t)ot_file_open(&f, "/ELEKLOADER/TELOS/MODS/BIG.BIN", "r") | 0x80000000u;
    if ((int32_t)(tp[8] << 1) >= 0) {
        tp[9] = (uint32_t)ot_file_read(&f, big, sizeof big);
        ot_file_close(&f);
        uint32_t s = 0;
        for (unsigned i = 0; i < 65536; i++)
            s += big[i];
        tp[10] = s;
    }
    tp_end();
}
