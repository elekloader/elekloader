/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "elekloader/sdk_alpha_machine.h"

enum { PITCH, AMOUNT, KNOB_COUNT };
static const struct elek_ui_knob knobs[KNOB_COUNT] = {
    [PITCH] = { .label = "PITCH", .min = ELEK_UI_BOUND(10), .max = ELEK_UI_BOUND(20) },
    [AMOUNT] = { .label = "AMOUNT" }, /* omitted bounds: 0..127 */
};
static const struct elek_ui_page pages[] = {
    { .label = "MAIN", .knob = { [ELEK_ENCODER_A] = &knobs[PITCH] } },
    { .label = "AMOUNT", .knob = { [ELEK_ENCODER_A] = &knobs[AMOUNT],
                                  [ELEK_ENCODER_H] = &knobs[PITCH] } },
};
static const struct elek_machine sdk_hello_world = {
    .id = 122, .name = "SDK Hello", .short_name = "HELO",
    .pages = pages, .page_count = 2,
    .knobs = knobs, .knob_count = KNOB_COUNT,
};
ELEK_REGISTER_MACHINE(sdk_hello_world);
