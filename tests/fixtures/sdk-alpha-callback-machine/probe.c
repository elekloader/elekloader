/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test-only readback; these probes are not part of the empty-machine example. */
#include "elekloader/sdk_alpha_machine.h"

volatile uint32_t alpha_probe_calls, alpha_probe_bad_context;

static void probe_present(const struct elek_ui_context *ctx, struct elek_ui_presentation *out)
{
    ++alpha_probe_calls;
    if (ctx->page != ELEK_UI_NO_PAGE || ctx->page_count || ctx->knob_count || ctx->knob ||
        out->knob_count || out->knob)
        ++alpha_probe_bad_context;
}

static const struct elek_machine callback_machine = {
    .id = 121,
    .name = "Alpha Callback",
    .short_name = "CALL",
    .present = probe_present,
};

ELEK_REGISTER_MACHINE(callback_machine);
