/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercise the real adapter with a simulated audio interrupt inside .present. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "digitakt-mk1/core3.h"
#include "elekloader/sdk_alpha_machine.h"

static uintptr_t test_machine_ui(int32_t id);
#define core_machine_ui test_machine_ui
#include "../mods/machine-pages/sdk_alpha.c"

const char cm_ui_v32[] = "native UI";
volatile uint32_t fw_active_track, fw_voice_start;
uint8_t *volatile fw_kit;
volatile uint8_t fw_render_machine[8];
volatile int32_t fw_voice_note[8], fw_amp_env[24];
volatile int16_t fw_voice_vel[8];
static int32_t effective[8][15], blocks[8 * ELEK_VOICE_FRAMES];
static uint8_t kit[0x20 + 8 * 0xa2];
static uint32_t calls[8], generation, interrupt_present;
static struct elek_voice last[8];
static int32_t value[8], pair[8][2];

int32_t cm_src_value(int32_t track, int32_t knob) { return effective[track][knob]; }
int32_t mp_shown_value(void *view, uint32_t id, uint8_t *locked)
{ (void)view; (void)id; *locked = 0; return 0; }
uint32_t mp_stock_layout(int32_t machine) { (void)machine; return 0; }

static void render(const struct elek_audio_context *ctx, const struct elek_voice *v, elek_sample *out)
{
    assert(v->slot == v->track && v->state);
    assert(ctx->knob_count == 1 && ctx->shared);
    const int32_t *shared = ctx->shared;
    assert(shared[0] == -shared[1]);
    pair[v->track][0] = shared[0]; pair[v->track][1] = shared[1];
    last[v->track] = *v;
    value[v->track] = ctx->knob[0];
    uint32_t *state = v->state;
    out[0] = ++*state;
    ++calls[v->track];
}
static void present(const struct elek_ui_context *ctx, struct elek_ui_presentation *out)
{
    assert(ctx->page == ELEK_UI_NO_PAGE);
    int32_t *shared = out->shared;
    shared[0] = generation + ctx->track;
    if (interrupt_present) {
        interrupt_present = 0;
        sdk_alpha_render(blocks);
    }
    shared[1] = -shared[0];
}
static const struct elek_ui_knob fine = {
    .id = 0, .label = "FINE", .min = ELEK_UI_BOUND(-50), .max = ELEK_UI_BOUND(50),
    .def = ELEK_UI_BOUND(0),
};
static const struct elek_ui_page pages[] = { { .label = "TEST", .knob = { &fine } } };
static const struct elek_machine machine = {
    .id = 100, .name = "Test", .short_name = "TEST", .pages = pages, .page_count = 1,
    .present = present, .render = render, .voice_state = sizeof(uint32_t), .shared = 8,
};
static const struct sdk_alpha_machine_ui ui = { sdk_alpha_dt1_ui_v1, &machine };
static uintptr_t test_machine_ui(int32_t id) { return id == 100 ? (uintptr_t)&ui : 0; }

int main(void)
{
    fw_kit = kit;
    for (int t = 0; t < 2; ++t) {
        FW_SOUND(kit, t)[FW_SOUND_MACHINE] = 100;
        fw_render_machine[t] = 100;
        fw_voice_note[t] = (60 + t) << 16;
        fw_voice_vel[t] = 100 << 8;
        fw_amp_env[3*t] = 1;
        effective[t][0] = t ? ELEK_UI_RAW_MAX : 0;
    }
    fw_voice_start = 1;                         /* track 0 starts before its shared block is ready */
    sdk_alpha_render(blocks);
    assert(!calls[0] && !calls[1]);             /* no uninitialized shared block */
    generation = 10;
    sdk_alpha_tick();                          /* includes inactive track 1 */
    fw_voice_start = 2;
    sdk_alpha_render(blocks);
    assert(calls[0] == 1 && calls[1] == 1);
    assert(value[0] == -50 && value[1] == 50);   /* effective audio values, not UI kit zeros */
    assert(pair[0][0] == 10 && pair[1][0] == 11);
    assert(last[1].pitch == 61 << 16 && last[1].velocity == 100 && last[1].gate == 1);
    assert(last[0].flags == ELEK_VOICE_START && last[1].flags == ELEK_VOICE_START);
    assert(last[0].state != last[1].state && blocks[0] == 1 && blocks[32] == 1);
    generation = 20;
    interrupt_present = 1;
    sdk_alpha_tick();                          /* interrupted between the two stores */
    assert(pair[0][0] == 10 && pair[1][0] == 11);
    sdk_alpha_render(blocks);
    assert(pair[0][0] == 20 && pair[1][0] == 21);
    assert(!last[0].flags);                     /* the kept start went to one call */
    assert(blocks[0] == 3 && blocks[32] == 3);  /* retriggers preserve the voice's state */
    fw_render_machine[0] = 0;
    sdk_alpha_render(blocks);
    fw_render_machine[0] = 100;
    sdk_alpha_render(blocks);
    assert(blocks[0] == 1 && blocks[32] == 5);  /* changing one track resets only its state */
    fw_voice_start = 0; fw_amp_env[3] = 0;
    sdk_alpha_render(blocks);
    assert(!last[1].gate && !last[1].flags);
    assert(sdk_alpha_map(&fine, sdk_alpha_default(&fine)) == 0);
    struct elek_ui_knob omitted = fine; omitted.def.supplied = 0;
    assert(sdk_alpha_default(&omitted) == 0 && sdk_alpha_map(&omitted, 0) == -50);
    struct elek_ui_knob level = { .def = ELEK_UI_BOUND(100) };
    assert(sdk_alpha_map(&level, sdk_alpha_default(&level)) == 100);
    return 0;
}
