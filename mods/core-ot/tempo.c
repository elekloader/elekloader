/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core for the Octatrack, OS 1.40C: ev_tempo's handlers (core-ot 0.41, tempo.s). */
#include <stdint.h>

extern uint32_t (*const ev_tempo[])(uint32_t, unsigned);

/* In 0x4009c550, interrupts masked: the source as stock chose it (PATTERN TEMPO on at 0x80000024 and
 * not in an arrangement, 0x460d1aec 0: the pattern's). */
uint32_t core_tempo_c(uint32_t tempo)
{
    unsigned src = *(volatile uint8_t *)0x80000024 && !*(volatile uint32_t *)0x460d1aec;
    for (uint32_t (*const *f)(uint32_t, unsigned) = ev_tempo; *f; ++f) {
        uint32_t t = (*f)(tempo, src);
        if (t)
            tempo = t < 720 ? 720 : t > 7200 ? 7200 : t;
    }
    return tempo;
}
