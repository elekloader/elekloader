/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test-only snapshots; the normal example has no probes or callback. */
#include "elekloader/sdk_alpha_machine.h"

enum { PITCH, ZERO, AMOUNT, FIXED, FOUR, FIVE, SIX, SEVEN, OTHER };
static const struct elek_ui_knob knobs[] = {
    [PITCH] = { .id = PITCH, .label = "PITCH", .min = ELEK_UI_BOUND(10), .max = ELEK_UI_BOUND(20) },
    [ZERO] = { .id = ZERO, .label = "ZERO", .max = ELEK_UI_BOUND(0) },
    [AMOUNT] = { .id = AMOUNT, .label = "AMOUNT" },
    [FIXED] = { .id = FIXED, .label = "FIXED", .min = ELEK_UI_BOUND(7), .max = ELEK_UI_BOUND(7) },
    [FOUR] = { .id = FOUR, .label = "FOUR" }, [FIVE] = { .id = FIVE, .label = "FIVE" },
    [SIX] = { .id = SIX, .label = "SIX" }, [SEVEN] = { .id = SEVEN, .label = "SEVEN" },
    [OTHER] = { .id = OTHER, .label = "OTHER", .min = ELEK_UI_BOUND(-10), .max = ELEK_UI_BOUND(10) },
};
static const struct elek_ui_page pages[] = {
    { .label = "CONTROLS", .knob = { [ELEK_ENCODER_A] = &knobs[AMOUNT],
        [ELEK_ENCODER_B] = &knobs[FOUR], [ELEK_ENCODER_C] = &knobs[FIVE],
        [ELEK_ENCODER_D] = &knobs[PITCH], [ELEK_ENCODER_E] = &knobs[SIX],
        [ELEK_ENCODER_F] = &knobs[SEVEN], [ELEK_ENCODER_H] = &knobs[ZERO] } },
    { .label = "OTHER", .knob = { [ELEK_ENCODER_A] = &knobs[OTHER],
        [ELEK_ENCODER_B] = &knobs[PITCH], [ELEK_ENCODER_H] = &knobs[FIXED] } },
};
volatile uint32_t alpha_controls_calls, alpha_controls_bad_context;
volatile int32_t alpha_controls_page[8];
volatile int32_t alpha_controls_raw[8][sizeof knobs / sizeof *knobs];
volatile int32_t alpha_controls_value[8][sizeof knobs / sizeof *knobs];
static void controls_present(const struct elek_ui_context *ctx, struct elek_ui_presentation *out)
{
    ++alpha_controls_calls;
    uint32_t track = fw_active_track;
    if (track >= 8 || ctx->page < 0 || ctx->page >= 2 || ctx->page_count != 2 ||
        ctx->knob_count != sizeof knobs / sizeof *knobs || !ctx->knob || out->knob_count || out->knob) {
        ++alpha_controls_bad_context;
        return;
    }
    alpha_controls_page[track] = ctx->page;
    for (int k = 0; k < sizeof knobs / sizeof *knobs; ++k) {
        alpha_controls_raw[track][k] = ctx->knob[k]->raw_value;
        alpha_controls_value[track][k] = ctx->knob[k]->value;
    }
}
static const struct elek_machine controls = {
    .id = 123, .name = "Alpha Controls", .short_name = "CTRL",
    .pages = pages, .page_count = 2,
    .present = controls_present,
};
ELEK_REGISTER_MACHINE(controls);
