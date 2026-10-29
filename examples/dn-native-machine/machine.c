/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A sine with native SYN drawing and independent short/full labels. */
#include "digitone-mk1/machines.h"

static uint32_t phase[8];
static void render(int32_t voice, int32_t *block, const struct dnm_voice *v)
{
    uint32_t p = v->fresh ? 0 : phase[voice];
    /* Second-page offset: 64 is the middle, one semitone each way. */
    int32_t fine = (dnm_knob(voice, 16) - 64 * 256) * 4;
    uint32_t inc = dnm_phase_inc(v->pitch + fine);
    for (int32_t i = 0; i < 32; i++) {
        block[i] = dnm_sin(p) * 4096;
        p += inc;
    }
    phase[voice] = p;
}
static const struct dnm_knob knobs[DNM_KNOBS_ALL] = {
    [16] = {"FINE", 0, 0},
};
static const int16_t defaults[DNM_KNOBS_ALL] = {
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    64 * 256, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
};
const struct dnm_machine native_machine = {
    .id = 3, .name = "NATIVE SINE", .knobs = knobs, .render = render,
    .flags = DNM_PAGES2, .defaults = defaults,
};
static const char *const names[DNM_KNOBS_ALL] = {[16] = "Osc2 Fine"};
const struct dnm_names native_names = {
    .abi = &dnm_names_v1, .id = 3, .count = DNM_KNOBS_ALL, .long_names = names,
};
static int32_t draw(void *view, void *bitmap, int32_t syn, int32_t page)
{
    int32_t locked, value = dnm_knob_value(16, &locked);
    (void)view;
    fw_fillrect(bitmap, 15, 0, 127, 52, 0);
    fw_textf(bitmap, fw_font5, 20, 40, 22, "NATIVE SYN%d %d/2", syn, page + 1);
    fw_textf(bitmap, fw_font5, 20, 25, 22, "FINE %d LOCK %d", value >> 8, locked);
    return 1;
}
const struct dnm_page native_pages = {
    .abi = &dnm_ui_v1, .id = 3, .pages = {2, 1}, .draw = draw,
};
