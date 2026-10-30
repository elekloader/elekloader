/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "elekloader/sdk_alpha_machine.h"

static const struct elek_machine empty_machine = {
    .id = 120,
    .name = "Alpha Empty",
    .short_name = "EMPT",
};

ELEK_REGISTER_MACHINE(empty_machine);
