/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "digitone-mk1/core3.h"
#ifdef CORE_NATIVE_UI

int32_t core_step_selection(uint32_t steps[2])
{
    uint8_t *ui = (void *)(uintptr_t)fw_ui_get();
    if (!steps) return 0;
    steps[0] = steps[1] = 0;
    if (!ui || !(fw_ui_grid_edit(ui) || fw_ui_step_edit(ui))) return 0;
    steps[0] = *(volatile uint32_t *)(ui + 0x15c);
    steps[1] = *(volatile uint32_t *)(ui + 0x158);
    return !!(steps[0] | steps[1]);
}

int32_t core_lock_selected(void *track, int32_t slot, int32_t value)
{
    uint32_t steps[2], length;
    int32_t written = 0;
    if (!core_step_selection(steps)) return 0;
    if (!track || slot < 0 || slot >= 79 || value < 0 || value > 32767) return -1;
    length = fw_track_length(track);
    if (!length || length > 64) return -1;
    for (uint32_t step = 0; step < length; step++) {
        if (!(steps[step / 32] & (1u << (step & 31)))) continue;
        if (!fw_track_has_note(track, step) && !fw_track_has_lock(track, step))
            fw_track_set_trig(track, step, 1);
        if (!fw_track_set_lock(track, step, slot, value)) {
            fw_ui_refresh((void *)(uintptr_t)fw_ui_get(), 1);
            return -1; /* Earlier steps/trig creation may already have changed. */
        }
        written++;
    }
    if (written) fw_ui_refresh((void *)(uintptr_t)fw_ui_get(), 1);
    return written ? written : -1; /* A selection beyond track length is not a base edit. */
}
#endif
