/* SPDX-License-Identifier: GPL-2.0-or-later */
/* PROG: the smallest program change mod for the Digitone mk1 and Digitone Keys (core-dn1 4.0). Every program
 * change the unit receives shows for two seconds in the screen's top right corner: its channel and program
 * (1-based, as a controller names them), and + when the stock applies it too. The ones on MIDI channel 16 are
 * PROG's own: it takes them, so the stock never changes the pattern for them. A template for a mod that answers
 * program changes, a pedal's presets say (docs/ADAPTING.md, "Program changes"). */
#include "digitone-mk1/core3.h"

typedef void (*rect_fn)(void *bmp, int x0, int y0, int x1, int y1, int colour);
typedef void (*text_fn)(void *bmp, const void *font, int x, int y, int maxlen, const char *fmt, ...);

#define TAKE_CHANNEL  15                    /* MIDI channel 16 */

/* The last program change, and how many came and were taken: the MIDI task writes them, the UI task reads. */
struct prog_state {
    volatile int32_t count, taken;
    volatile int32_t channel, program, flags;
    volatile int32_t show;                  /* UI ticks left to show it (30 a second) */
};
struct prog_state prog;

/* ev_midi_pc: the MIDI input's task, not interrupt level. Nonzero takes it. */
int32_t prog_pc(int32_t channel, int32_t program, int32_t flags)
{
    prog.channel = channel;
    prog.program = program;
    prog.flags = flags;
    prog.count++;
    prog.show = 60;
    if (channel != TAKE_CHANNEL)
        return 0;                           /* the stock goes on: on PROG CH IN's channel, a new pattern */
    prog.taken++;
    return 1;
}

void prog_tick(void *ctrl)
{
    if (prog.show > 0) {
        prog.show--;
        *((unsigned char *)ctrl + 0x20) = 1;    /* recompose: shown, then gone */
    }
}

void prog_draw(void *bmp, void *ctrl)
{
    (void)ctrl;
    if (prog.show <= 0)
        return;
    ((rect_fn)fw_fillrect)(bmp, 84, 54, 127, 63, 0);
    ((text_fn)fw_textf)(bmp, (const void *)fw_font5, 87, 57, -1, "PC %d:%d%s", (int)prog.channel + 1,
                        (int)prog.program + 1, prog.flags & CORE_MIDI_PC_STOCK ? "+" : "");
    ((rect_fn)fw_fillrect)(bmp, 84, 54, 127, 63, -1);
}
