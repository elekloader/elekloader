/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "elekloader/sdk_alpha_machine.h"

enum { PITCH, AMOUNT };
static const struct elek_knob knobs[] = {
    [PITCH] = { "PITCH", .min = 10, .max = 20 },
    [AMOUNT] = { "AMNT", .max = 127 },
};
/* AMOUNT renames PITCH, "A 37" say, and at 127 hides it. */
static void hello_present(const struct elek_ui *ui)
{
    int32_t amount = ui->knob[AMOUNT];
    if (amount)
        ui->view[PITCH].label = elek_num(ui, "A ", amount);
    ui->view[PITCH].hidden = amount == 127;
}
static const struct elek_machine sdk_hello_world = {
    .id = 122, .name = "SDK Hello", .short_name = "HELO", ELEK_KNOBS(knobs),
    .pages = {
        { "MAIN", { [ELEK_A] = &knobs[PITCH] } },
        { "AMOUNT", { [ELEK_A] = &knobs[AMOUNT], [ELEK_H] = &knobs[PITCH] } },
    },
    .present = hello_present,
};
ELEK_REGISTER_MACHINE(sdk_hello_world);
