/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The real DN1 adapter with native metadata. */
#undef NDEBUG                   /* the checks below are asserts */
#include <assert.h>
#include <stdint.h>
#include "digitone-mk1/core3.h"
#include "digitone-mk1/machines.h"
#include "elekloader/sdk_alpha_machine.h"

static int32_t params[15 * 160];
#define fw_params ((const uint8_t *)params)
/* The production assertion describes the 32-bit firmware ABI, checked by the cross build. glibc's
 * C99 headers define _Static_assert as a macro already; nothing below the adapter uses it. */
#undef _Static_assert
#define _Static_assert(condition, message)
#include "../mods/machines-dn1/sdk_alpha.c"
#include "../mods/sdk-alpha/machine.c"

uint8_t *volatile fw_kit;
volatile int32_t fw_active_track, fw_voice_track[8];
volatile int16_t fw_voice_params[9 + 79 * 8];
__typeof__(core_views) core_views;
static uint8_t kit[0x2c + 326 * 4];
static int16_t words[79 * 8];
static int32_t blocks[8][ELEK_VOICE_FRAMES];
static struct dnm_voice native[8];
static uint32_t calls[8], fallback_reset;
static struct elek_voice last[8];
static int32_t input[8][3];
static int32_t set_track, set_slot, set_value;

int32_t dnm_sdk_param(int32_t k) { return 100 + k; }
int32_t dnm_sdk_raw(int32_t t, int32_t k)
{ return FW_SOUND_SLOT(fw_kit, t, params[15 * dnm_sdk_param(k) + 1]); }
int32_t dnm_knob(int32_t v, int32_t k)
{ return FW_VOICE_PARAM(v, params[15 * dnm_sdk_param(k) + 1]); }
void core_sound_set(int32_t t, int32_t slot, int32_t value)
{ set_track = t; set_slot = slot; set_value = value; }
int32_t core_page_list(int32_t first, const int32_t *list, int32_t n)
{ (void)first; (void)list; (void)n; return 0; }
int32_t core_page_shown(void *brain, void *page) { (void)brain; (void)page; return 0; }
void fw_fillrect(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t colour)
{ (void)bmp; (void)x0; (void)y0; (void)x1; (void)y1; (void)colour; }
static void stock_reset(void *set, int32_t id) { (void)set; (void)id; ++fallback_reset; }

static int32_t rendering;                   /* the voice render() is rendering */
static void sound(const struct elek_voice *v, elek_sample *out)
{
    assert(v->state && v->knob);
    last[rendering] = *v;
    for (int k = 0; k < 3; ++k) input[rendering][k] = v->knob[k];
    out[0] = ELEK_SAMPLE(1.0);
    out[1] = 16 * ++*ELEK_STATE(uint32_t, v);
    ++calls[rendering];
}
#define render(voice, block, v) (rendering = (voice), render(voice, block, v))
static const struct elek_knob controls[] = {
    { "FINE", .min = -50, .max = 50 },
    { "LEVEL", .max = 127, .def = 100 },
    { "LOW", .min = 10, .max = 20 },
};
static const struct elek_machine test = {
    .id = 100, .name = "Test", .short_name = "TEST", ELEK_KNOBS(controls),
    .pages = { { "FIRST", { &controls[0], &controls[1], &controls[2] } }, { "SHARED", { &controls[0] } } },
    .render = sound,
};
ELEK_REGISTER_MACHINE(test);
const struct sdk_alpha_registration *const sdk_alpha_machines[] = { &sdk_alpha_registration_test, 0 };

int main(void)
{
    fw_kit = kit;
    for (int k = 0; k < DNM_KNOBS_ALL; ++k) params[15 * dnm_sdk_param(k) + 1] = 10 + k;
    const struct dnm_machine *m = dnm_sdk_machine(0);
    assert(m && m->render && !dnm_sdk_machine(1));
    assert(sdk_alpha_entries[0].count == 3);
    assert(sdk_alpha_map(&controls[0], m->defaults[sdk_pos[0]]) == 0);
    assert(sdk_alpha_map(&controls[1], m->defaults[sdk_pos[1]]) == 100);
    assert(m->defaults[sdk_pos[2]] == 0);
    listed = m; reset_parameter = stock_reset;
    sdk_reset(0, dnm_sdk_param(sdk_pos[1]));
    assert(set_track == 0 && set_slot == 10 + sdk_pos[1]);
    assert(sdk_alpha_map(&controls[1], set_value) == 100);
    sdk_reset(0, 1); assert(fallback_reset == 1);
    listed = 0;
    FW_SOUND_SLOT(kit, 0, 0) = FW_SOUND_SLOT(kit, 1, 0) = 100 << 8;
    for (int v = 5; v <= 7; v += 2) {
        FW_VOICE_PARAM(v, 0) = 100 << 8;
        fw_voice_track[v] = v == 5 ? 0 : 1;
        native[v] = (struct dnm_voice){ .pitch = (60 + v) << 16, .velocity = 90,
                                      .fresh = 1, .track = fw_voice_track[v] };
    }
    dnm_sdk_params(words);
    /* The controller stage changes the retained buffer after the params event. */
    words[79 * 5 + 10 + sdk_pos[0]] = SDK_ALPHA_RAW_MAX;
    words[79 * 7 + 10 + sdk_pos[0]] = 0;
    render(5, blocks[5], &native[5]); render(7, blocks[7], &native[7]);
    assert(calls[5] == 1 && calls[7] == 1);
    assert(last[5].track == 0 && last[5].pitch == 65 << 16);
    assert(last[7].track == 1 && last[7].velocity == 90);
    assert(last[5].start);
    assert(last[5].state != last[7].state);
    assert(input[5][0] == 50 && input[7][0] == -50);
    assert(blocks[5][0] == 0x07ffffff && blocks[5][1] == 1);
    native[5].fresh = 0;                   /* the native companion consumes its first-block flag */
    render(5, blocks[5], &native[5]); render(7, blocks[7], &native[7]);
    assert(!last[5].start);
    render(5, blocks[5], &native[5]); render(7, blocks[7], &native[7]);
    assert(blocks[5][1] == 3 && blocks[7][1] == 3);
    FW_VOICE_PARAM(5, 0) = 0; dnm_sdk_params(words);
    FW_VOICE_PARAM(5, 0) = 100 << 8; dnm_sdk_params(words);
    render(5, blocks[5], &native[5]); render(7, blocks[7], &native[7]);
    assert(blocks[5][1] == 1 && blocks[7][1] == 4);
    native[7].fresh = 0;
    render(7, blocks[7], &native[7]);
    assert(!last[7].start);
    return 0;
}
