/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Private SDK example: STATE selects a digit glyph; STOCK restores its native graphic. */
#include "elekloader/sdk_alpha_machine.h"

enum { STATE, STOCK };
static const struct elek_ui_glyph state_glyphs[] = {
    { .rows = { [3] = 0x0100, 0x0300, 0x0100, 0x0100, 0x0100, 0x0100, 0x0380 } }, /* 1 */
    { .rows = { [3] = 0x0380, 0x0440, 0x0040, 0x0080, 0x0100, 0x0200, 0x07c0 } }, /* 2 */
    { .rows = { [3] = 0x0780, 0x0040, 0x0040, 0x0380, 0x0040, 0x0040, 0x0780 } }, /* 3 */
    { .rows = { [3] = 0x0080, 0x0180, 0x0280, 0x0480, 0x07c0, 0x0080, 0x0080 } }, /* 4 */
};
static const struct elek_ui_knob state = {
    .id = STATE, .label = "STATE",
    .min = ELEK_UI_BOUND(1), .max = ELEK_UI_BOUND(4),
    .glyph = &state_glyphs[0],
};
static const struct elek_ui_knob stock = {
    .id = STOCK, .label = "STOCK", .max = ELEK_UI_BOUND(1),
};
static const struct elek_ui_page pages[] = {
    { .label = "GLYPHS", .knob = { [ELEK_ENCODER_A] = &state, [ELEK_ENCODER_B] = &stock } },
};
static void present(const struct elek_ui_context *ctx, struct elek_ui_presentation *out)
{
    out->knob[STATE]->glyph = ctx->knob[STOCK]->value ? 0 :
        &state_glyphs[ctx->knob[STATE]->value - 1];
}
static const struct elek_machine glyph_machine = {
    .id = 127, .name = "SDK Glyphs", .short_name = "GLYP",
    .pages = pages, .page_count = 1, .present = present,
};
ELEK_REGISTER_MACHINE(glyph_machine);
