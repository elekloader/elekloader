/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Private SDK example: STATE selects a digit glyph; STOCK restores its native graphic. */
#include "elekloader/sdk_alpha_machine.h"

enum { STATE, STOCK };
static const struct elek_glyph state_glyphs[] = {
    { .rows = { [3] = 0x0100, 0x0300, 0x0100, 0x0100, 0x0100, 0x0100, 0x0380 } }, /* 1 */
    { .rows = { [3] = 0x0380, 0x0440, 0x0040, 0x0080, 0x0100, 0x0200, 0x07c0 } }, /* 2 */
    { .rows = { [3] = 0x0780, 0x0040, 0x0040, 0x0380, 0x0040, 0x0040, 0x0780 } }, /* 3 */
    { .rows = { [3] = 0x0080, 0x0180, 0x0280, 0x0480, 0x07c0, 0x0080, 0x0080 } }, /* 4 */
};
static const struct elek_knob knobs[] = {
    [STATE] = { "STATE", .min = 1, .max = 4, .glyph = &state_glyphs[0] },
    [STOCK] = { "STOCK", .max = 1 },
};
static void present(const struct elek_ui *ui)
{
    ui->view[STATE].glyph = ui->knob[STOCK] ? 0 : &state_glyphs[ui->knob[STATE] - 1];
}
static const struct elek_machine glyph_machine = {
    .id = 127, .name = "SDK Glyphs", .short_name = "GLYP", ELEK_KNOBS(knobs),
    .pages = { { "GLYPHS", { [ELEK_A] = &knobs[STATE], [ELEK_B] = &knobs[STOCK] } } },
    .present = present,
};
ELEK_REGISTER_MACHINE(glyph_machine);
