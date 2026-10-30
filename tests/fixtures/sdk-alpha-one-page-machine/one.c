/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "elekloader/sdk_alpha_machine.h"
static const struct elek_ui_knob knobs[] = { { .label = "SINGLE" } };
static const struct elek_ui_page pages[] = {
    { .label = "SINGLE", .knob = { [ELEK_ENCODER_D] = &knobs[0] } },
};
static const struct elek_machine one = {
    .id = 124, .name = "Alpha One Page", .short_name = "ONE",
    .pages = pages, .page_count = 1, .knobs = knobs, .knob_count = 1,
};
ELEK_REGISTER_MACHINE(one);
