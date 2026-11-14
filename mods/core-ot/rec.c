/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core for the Octatrack, OS 1.40C: ev_preview and ev_rec_cmd's handlers (core-ot 0.41, preview.s). */
#include <stdint.h>

extern void (*const ev_preview[])(unsigned, unsigned, unsigned, unsigned);
extern void (*const ev_rec_cmd[])(unsigned, unsigned, unsigned);

void core_preview_c(unsigned trk, unsigned on, unsigned flex, unsigned slot)
{
    for (void (*const *f)(unsigned, unsigned, unsigned, unsigned) = ev_preview; *f; ++f)
        (*f)(trk, on, flex, slot);
}

void core_rec_c(unsigned trk, unsigned cmd, unsigned quant)
{
    for (void (*const *f)(unsigned, unsigned, unsigned) = ev_rec_cmd; *f; ++f)
        (*f)(trk, cmd, quant);
}
