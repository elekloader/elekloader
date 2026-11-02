/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "elekloader/sdk_alpha_machine.h"

static const struct elek_machine sdk_hello_world = {
    .id = 122,
    .name = "SDK Hello",
    .short_name = "HELO",
};

ELEK_REGISTER_MACHINE(sdk_hello_world);
