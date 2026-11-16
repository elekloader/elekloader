/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core for the Octatrack, OS 1.40C: the handlers of core-ot 0.42's sequencer events (moments.s). */
#include <stdint.h>

extern uint32_t (*const ev_tempo_set[])(uint32_t, unsigned, unsigned);
extern int (*const ev_pattern_select[])(unsigned, unsigned, unsigned, unsigned);
extern void (*const ev_pattern_edited[])(int, unsigned, unsigned, unsigned);
extern int (*const ev_arr_mode[])(unsigned);
extern void (*const ev_arr_row[])(unsigned, unsigned);
extern int (*const ev_track_play[])(unsigned, int, unsigned, unsigned);

#define U8(a)  (*(volatile uint8_t *)(a))
#define U32(a) (*(volatile uint32_t *)(a))

uint32_t core_tempo_set_c(uint32_t tempo, unsigned src, unsigned ra)
{
    for (uint32_t (*const *f)(uint32_t, unsigned, unsigned) = ev_tempo_set; *f; ++f) {
        uint32_t t = (*f)(tempo, src, ra);
        if (t)
            tempo = t < 720 ? 720 : t > 7200 ? 7200 : t;
    }
    return tempo;
}

/* The wrapper's user callers: PTN+TRIG, the BANK window, MIDI program change. */
int core_pattern_select_c(unsigned bank, unsigned pattern, unsigned ra)
{
    int user = ra == 0x40056b6e || ra == 0x4007b382 || ra == 0x40005578;
    unsigned queued = U32(0x800065b8) == 1;
    int refused = 0;
    for (int (*const *f)(unsigned, unsigned, unsigned, unsigned) = ev_pattern_select; *f; ++f)
        if ((*f)(bank, pattern, ra, queued) && user)
            refused = 1;
    return refused;
}

void core_pattern_edited_c(int trk, unsigned ra)
{
    static const uint32_t loads[] = { 0x40022bba, 0x4006227e, 0x400623dc, 0x4006cec2, 0x40080422,
                                      0x40084cec, 0x400a069a, 0x400a1094, 0x400a141a };
    unsigned load = 0;
    for (unsigned i = 0; i < sizeof loads / sizeof loads[0]; ++i)
        load |= ra == loads[i];
    for (void (*const *f)(int, unsigned, unsigned, unsigned) = ev_pattern_edited; *f; ++f)
        (*f)(trk, U8(0x800065bd), U8(0x800065be), load);
}

/* on 1: before entering (nonzero = refused, the first taker); on 0: after leaving. */
int core_arr_mode_c(unsigned on)
{
    for (int (*const *f)(unsigned) = ev_arr_mode; *f; ++f)
        if ((*f)(on) && on)
            return 1;
    return 0;
}

void core_arr_row_c(unsigned row, unsigned ra)
{
    unsigned how = ra == 0x400a021c ? 0 : ra == 0x400a0362 ? 1 : ra == 0x4004a858 ? 2 : ra == 0x400a02ba ? 3 : 0xff;
    for (void (*const *f)(unsigned, unsigned) = ev_arr_row; *f; ++f)
        (*f)(row, how);
}

int core_track_play_c(unsigned trk, int slot, unsigned flags, unsigned mode)
{
    for (int (*const *f)(unsigned, int, unsigned, unsigned) = ev_track_play; *f; ++f)
        if ((*f)(trk, slot, flags, mode))
            return 1;
    return 0;
}
