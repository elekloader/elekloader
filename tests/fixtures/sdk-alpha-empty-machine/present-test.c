/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host-only instrumentation, never part of the normal empty fixture. */
#include <assert.h>
#include "elekloader/sdk_alpha_machine.h"

void sdk_alpha_present(const struct elek_machine *machine);
static int calls;

static void empty_present(const struct elek_ui_context *ctx, struct elek_ui_presentation *out)
{
    assert(ctx->page == ELEK_UI_NO_PAGE);
    assert(!ctx->page_count && !ctx->knob_count && !ctx->knob);
    assert(!out->knob_count && !out->knob);
    ++calls;
}

/* Unused target routines: this harness exercises callback dispatch, not the native UI. */
uint32_t core_machine_ui(int32_t id) { (void)id; return 0; }
uint32_t mp_stock_layout(int32_t id) { (void)id; return 0; }

int main(void)
{
    const struct elek_machine without = { .id = 120, .name = "Empty", .short_name = "EMPT" };
    const struct elek_machine with = { .id = 121, .name = "Callback", .short_name = "CALL",
                                      .present = empty_present };
    sdk_alpha_present(&without);
    assert(calls == 0);
    sdk_alpha_present(&with);
    assert(calls == 1);
    sdk_alpha_present(&without);
    assert(calls == 1);
    return 0;
}
