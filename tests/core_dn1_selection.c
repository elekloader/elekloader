/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "../mods/core-dn1/selection.c"
static uint8_t ui[512];
static uint32_t object, length = 64, selected[2], lock_trigs[2] = {0, 1u << 31}, writes, trigs, refreshes;
static int grid, step_mode, fail_at = -1;
uint32_t fw_ui_get(void) { return (uint32_t)(uintptr_t)ui; }
uint8_t fw_ui_grid_edit(void *p) { assert(p == ui); return grid; }
uint8_t fw_ui_step_edit(void *p) { assert(p == ui); return step_mode; }
uint32_t fw_track_length(void *p) { assert(p == &object); return length; }
uint8_t fw_track_has_note(void *p, uint32_t step) { assert(p == &object); return step == 0; }
uint8_t fw_track_has_lock(void *p, uint32_t step) { assert(p == &object); return (lock_trigs[step / 32] >> (step & 31)) & 1; }
void fw_track_set_trig(void *p, uint32_t step, uint32_t on)
{ assert(p == &object && step != 0 && step != 63 && on == 1); lock_trigs[step / 32] |= 1u << (step & 31); trigs++; }
uint8_t fw_track_set_lock(void *p, uint32_t step, uint32_t slot, uint32_t value)
{
    assert(p == &object && slot == 31 && value == 2222);
    if ((int)step == fail_at) return 0;
    selected[step / 32] |= 1u << (step & 31); writes++; return 1;
}
void fw_ui_refresh(void *p, uint32_t changed) { assert(p == ui && changed); refreshes++; }
static void masks(uint32_t low, uint32_t high)
{ *(uint32_t *)(ui + 0x15c) = low; *(uint32_t *)(ui + 0x158) = high; }
int main(void)
{
    uint32_t bits[2] = {~0u, ~0u};
    masks(~0u, ~0u);
    assert(!core_step_selection(bits) && !bits[0] && !bits[1]);
    assert(!core_step_selection(0));
    assert(!core_lock_selected(0, -1, -1)); /* No selection never edits the base. */
    grid = 1; masks(1u | (1u << 31), 1u | (1u << 31));
    assert(core_step_selection(bits) && bits[0] == bits[1]);
    assert(core_lock_selected(&object, 31, 2222) == 4);
    assert(selected[0] == bits[0] && selected[1] == bits[1] && writes == 4 && trigs == 2 && refreshes == 1);
    assert(core_lock_selected(0, 31, 2222) == -1);
    assert(core_lock_selected(&object, 79, 2222) == -1);
    assert(core_lock_selected(&object, 31, -1) == -1);
    assert(core_lock_selected(&object, 31, 32768) == -1);
    length = 0; assert(core_lock_selected(&object, 31, 2222) == -1);
    length = 65; assert(core_lock_selected(&object, 31, 2222) == -1);
    length = 16; masks(0, 1u << 31);
    assert(core_lock_selected(&object, 31, 2222) == -1 && writes == 4);
    length = 64; grid = 0; step_mode = 1; masks(1 | (1u << 2), 1u << 31);
    fail_at = 2;
    assert(core_lock_selected(&object, 31, 2222) == -1 && writes == 5 && refreshes == 2);
    assert(trigs == 3 && (lock_trigs[0] & (1u << 2)) && !(selected[0] & (1u << 2)));
    /* Retry the same absolute edit after capacity returns. Existing writes and
     * the failed step's new lock trig remain; no duplicate trig is created. */
    fail_at = -1;
    assert(core_lock_selected(&object, 31, 2222) == 3 && writes == 8 && refreshes == 3);
    assert(trigs == 3 && (selected[0] & (1u << 2)) && bits[0] == bits[1]);
    masks(0, 0); assert(!core_lock_selected(&object, 31, 2222));
    puts("PASS core selection: GRID/STEP halves, bounds, no base fallback, existing trigs, partial allocation failure and retry recovery");
}
