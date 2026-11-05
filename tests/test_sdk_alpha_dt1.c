/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercise the real adapter's render path. */
#undef NDEBUG                   /* the checks below are asserts */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "digitakt-mk1/core3.h"
#include "elekloader/sdk_alpha_machine.h"

static uintptr_t test_machine_ui(int32_t id);
#define core_machine_ui test_machine_ui
#include "../mods/machine-pages/sdk_alpha.c"
#include "../mods/sdk-alpha/machine.c"

const char cm_ui_v32[] = "native UI";
volatile uint32_t fw_active_track, fw_voice_start;
uint8_t *volatile fw_kit;
volatile uint8_t fw_render_machine[8];
volatile int32_t fw_voice_note[8];
volatile int16_t fw_voice_vel[8];
static int32_t effective[8][15], blocks[8 * ELEK_VOICE_FRAMES];
static uint8_t kit[0x20 + 8 * 0xa2];
static uint32_t calls[8];
static struct elek_voice last[8];
static int32_t value[8];

int32_t cm_src_value(int32_t track, int32_t knob) { return effective[track][knob]; }
int32_t mp_shown_value(void *view, uint32_t id, uint8_t *locked)
{ (void)view; (void)id; *locked = 0; return 0; }
uint32_t mp_stock_layout(int32_t machine) { (void)machine; return 0; }

static void render(const struct elek_voice *v, elek_sample *out)
{
    assert(v->state && v->knob);
    last[v->track] = *v;
    value[v->track] = v->knob[0];
    uint32_t *state = ELEK_STATE(uint32_t, v);
    out[0] = ++*state;
    ++calls[v->track];
}
static const struct elek_knob knobs[] = { { "FINE", .min = -50, .max = 50 } };
static const struct elek_machine machine = {
    .id = 100, .name = "Test", .short_name = "TEST", ELEK_KNOBS(knobs),
    .pages = { { "TEST", { &knobs[0] } } }, .render = render,
};
static const struct sdk_alpha_machine_ui ui = { sdk_alpha_dt1_ui_v1, &machine };
static uintptr_t test_machine_ui(int32_t id) { return id == 100 ? (uintptr_t)&ui : 0; }
static const struct cm_machine native = { .id = 100 };
const struct cm_machine *const core_machines[] = { &native, 0 };
void fw_fillrect(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t colour)
{ (void)bmp; (void)x0; (void)y0; (void)x1; (void)y1; (void)colour; }

int main(void)
{
    fw_kit = kit;
    for (int t = 0; t < 2; ++t) {
        FW_SOUND(kit, t)[FW_SOUND_MACHINE] = 100;
        fw_render_machine[t] = 100;
        fw_voice_note[t] = (60 + t) << 16;
        fw_voice_vel[t] = 100 << 8;
        effective[t][0] = t ? SDK_ALPHA_RAW_MAX : 0;
    }
    fw_voice_start = 3;
    sdk_alpha_render(blocks);
    assert(calls[0] == 1 && calls[1] == 1);
    assert(value[0] == -50 && value[1] == 50);   /* effective audio values, not UI kit zeros */
    assert(last[1].pitch == 61 << 16 && last[1].velocity == 100 && last[1].track == 1);
    assert(last[0].start && last[1].start);
    assert(last[0].state != last[1].state && blocks[0] == 1 && blocks[32] == 1);
    fw_voice_start = 2;
    sdk_alpha_render(blocks);
    sdk_alpha_render(blocks);
    assert(!last[0].start);                     /* the start went to one call */
    assert(blocks[0] == 3 && blocks[32] == 3);  /* retriggers preserve the voice's state */
    fw_render_machine[0] = 0;
    sdk_alpha_render(blocks);
    fw_render_machine[0] = 100;
    sdk_alpha_render(blocks);
    assert(blocks[0] == 1 && blocks[32] == 5);  /* changing one track resets only its state */
    fw_voice_start = 0;
    sdk_alpha_render(blocks);
    assert(!last[1].start);
    assert(sdk_alpha_map(&knobs[0], sdk_alpha_default(&knobs[0])) == 0);   /* omitted: 0 */
    struct elek_knob level = { "LEVEL", .max = 127, .def = 100 };
    assert(sdk_alpha_map(&level, sdk_alpha_default(&level)) == 100);
    struct elek_knob above = { "ABOVE", .min = 10, .max = 20 };
    assert(sdk_alpha_default(&above) == 0);                                /* 0, clamped to min */
    return 0;
}
