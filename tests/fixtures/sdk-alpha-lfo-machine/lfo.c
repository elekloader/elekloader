/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "elekloader/sdk_alpha_machine.h"

static const struct elek_ui_knob knobs[] = {
    { .id = 0, .label = "XYZ" }, { .id = 1, .label = "RENAME" },
    { .id = 2, .label = "P03" }, { .id = 3, .label = "P04" },
    { .id = 4, .label = "P05" }, { .id = 5, .label = "P06" },
    { .id = 6, .label = "P07" }, { .id = 7, .label = "P08" },
    { .id = 8, .label = "P09" }, { .id = 9, .label = "P10" },
    { .id = 10, .label = "P11" }, { .id = 11, .label = "P12" },
    { .id = 12, .label = "P13" }, { .id = 13, .label = "P14" },
    { .id = 14, .label = "P15" },
};
static const struct elek_ui_page pages[] = {
    { .label = "FIRST", .knob = { &knobs[1], &knobs[2], &knobs[3], &knobs[0],
                                &knobs[4], &knobs[5], &knobs[6], &knobs[7] } },
    { .label = "SECOND", .knob = { &knobs[8], &knobs[9], &knobs[10], &knobs[11],
                                 &knobs[12], &knobs[13], &knobs[14], &knobs[0] } },
};
volatile int32_t alpha_lfo_page;
static void present(const struct elek_ui_context *ctx, struct elek_ui_presentation *out)
{
    alpha_lfo_page = ctx->page;
    if (ctx->knob[1]->value) {
        out->knob[0]->label = "QWE";
        out->knob[0]->visible = ctx->knob[1]->value != 127;
    }
}
static const struct elek_machine lfo = {
    .id = 126, .name = "Alpha LFO", .short_name = "ALFO",
    .pages = pages, .page_count = 2, .present = present,
};
ELEK_REGISTER_MACHINE(lfo);
