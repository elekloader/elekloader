/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "elekloader/sdk_alpha_machine.h"

enum { PITCH, AMOUNT };
volatile uint32_t alpha_labels_calls, alpha_labels_bad_context;
volatile int32_t alpha_labels_page[8], alpha_labels_amount[8];
static const struct elek_ui_knob pitch = {
    .id = PITCH, .label = "PITCH", .min = ELEK_UI_BOUND(10), .max = ELEK_UI_BOUND(20),
};
static const struct elek_ui_knob amount = { .id = AMOUNT, .label = "AMOUNT" };
static const struct elek_ui_page pages[] = {
    { .label = "MAIN", .knob = { [ELEK_ENCODER_A] = &pitch } },
    { .label = "AMOUNT", .knob = { [ELEK_ENCODER_A] = &amount,
                                  [ELEK_ENCODER_H] = &pitch } },
};
static void hello_present(const struct elek_ui_context *ctx, struct elek_ui_presentation *out)
{
    ++alpha_labels_calls;
    uint32_t track = fw_active_track;
    if (track >= 8 || ctx->page < 0 || ctx->page > 1 || ctx->knob_count != 2 ||
        out->knob_count != 2 || !out->knob) { ++alpha_labels_bad_context; return; }
    alpha_labels_page[track] = ctx->page;
    alpha_labels_amount[track] = ctx->knob[AMOUNT]->value;
    static char text[] = "AMT 000";
    unsigned amount = ctx->knob[AMOUNT]->value;
    if (amount) {
        text[4] = '0' + amount / 100;
        text[5] = '0' + amount / 10 % 10;
        text[6] = '0' + amount % 10;
        out->knob[PITCH]->label = text;
        if (amount == 127)
            out->knob[PITCH]->visible = 0;
    }
}
static const struct elek_machine label_controls = {
    .id = 125, .name = "Alpha Labels", .short_name = "LABL",
    .pages = pages, .page_count = 2,
    .present = hello_present,
};
ELEK_REGISTER_MACHINE(label_controls);
