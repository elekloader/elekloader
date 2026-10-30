/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "elekloader/sdk_alpha_machine.h"

enum { PITCH, AMOUNT };
static const struct elek_ui_knob pitch = {
    .id = PITCH, .label = "PITCH", .min = ELEK_UI_BOUND(10), .max = ELEK_UI_BOUND(20),
};
static const struct elek_ui_knob amount = { .id = AMOUNT, .label = "AMOUNT" }; /* 0..127 */
static const struct elek_ui_page pages[] = {
    { .label = "MAIN", .knob = { [ELEK_ENCODER_A] = &pitch } },
    { .label = "AMOUNT", .knob = { [ELEK_ENCODER_A] = &amount,
                                  [ELEK_ENCODER_H] = &pitch } },
};
static const struct elek_machine sdk_hello_world = {
    .id = 122, .name = "SDK Hello", .short_name = "HELO",
    .pages = pages, .page_count = 2,
};
ELEK_REGISTER_MACHINE(sdk_hello_world);
