/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Private empty-machine backend. No cm_ui/cm_knob definitions are fabricated. */
#include "elekloader/sdk_alpha_machine.h"

const char sdk_alpha_dt1_empty_ui[] = "sdk alpha dt1 empty";
static const struct elek_machine *sdk_alpha_current;
static uint32_t sdk_alpha_empty_layout[11];
extern uint32_t mp_stock_layout(int32_t machine);

void sdk_alpha_present(const struct elek_machine *machine)
{
    const struct elek_ui_context ctx = { .page = ELEK_UI_NO_PAGE };
    struct elek_ui_presentation out = {0};
    if (machine->present)
        machine->present(&ctx, &out);
}

uint32_t sdk_alpha_layout(int32_t machine, uint32_t track)
{
    const struct sdk_alpha_machine_ui *ui =
        (const struct sdk_alpha_machine_ui *)core_machine_ui(machine);
    sdk_alpha_current = 0;
    if (!ui || ui->abi != sdk_alpha_dt1_empty_ui)
        return 0;
    const struct elek_machine *m = ui->machine;
    if (!m || m->id != machine || m->pages || m->page_count || m->knobs || m->knob_count)
        return 0;
    sdk_alpha_current = m;
    const uint32_t *stock = (const uint32_t *)mp_stock_layout(3);
    for (int i = 0; i < 11; ++i)
        sdk_alpha_empty_layout[i] = i >= 2 && i < 10 ? 0 : stock[i];
    if (track < 8)
        sdk_alpha_present(m);
    return (uint32_t)sdk_alpha_empty_layout;
}

int32_t sdk_alpha_empty_selected(void)
{
    return sdk_alpha_current != 0;
}
