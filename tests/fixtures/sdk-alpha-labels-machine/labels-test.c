/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <string.h>
#include "elekloader/sdk_alpha_machine.h"
int32_t sdk_alpha_snapshot(const struct elek_machine *, int32_t, void *);
int32_t sdk_alpha_visible(const struct elek_machine *, uint32_t);
const char *sdk_alpha_label(const struct elek_machine *, uint32_t);
const char cm_ui_v32[] = "host stub";
uint32_t core_machine_ui(int32_t id) { (void)id; return 0; }
volatile uint32_t fw_active_track;
uint8_t *volatile fw_kit;
uint32_t mp_stock_layout(int32_t id) { (void)id; return 0; }
int32_t mp_shown_value(void *view, uint32_t id, uint8_t *locked)
{ (void)locked; return ((int32_t *)view)[id - 0x84]; }
static const struct elek_ui_knob knobs[] = { { .id = 0, .label = "SOURCE" }, { .id = 1, .label = "TARGET" } };
static const struct elek_ui_page pages[2] = {
    { .knob = { [ELEK_ENCODER_A] = &knobs[0], [ELEK_ENCODER_H] = &knobs[1] } },
    { .knob = { [ELEK_ENCODER_A] = &knobs[1], [ELEK_ENCODER_H] = &knobs[0] } },
};
static char buffer[16] = "COPIED";
static int mode;
static void present(const struct elek_ui_context *ctx, struct elek_ui_presentation *out)
{
    assert(out->knob_count == 2 && out->knob);
    assert(out->knob[1]->label == knobs[1].label && out->knob[1]->visible == 1);
    if (ctx->knob[0]->value == 127) out->knob[1]->visible = 0;
    if (ctx->knob[0]->value) {
        if (mode == 0) out->knob[1]->label = buffer;
        if (mode == 1) out->knob[1]->label = "LITERAL";
        if (mode == 2) out->knob[1]->label = "123456789long";
        if (mode == 3) out->knob[1]->label = "";
        if (mode == 4) out->knob[1]->label = 0;
        if (mode == 5) out->knob[1]->label = "A\n\377Z";
    }
}
int main(void)
{
    const struct elek_machine m = { .pages = pages, .page_count = 2,
        .present = present };
    int32_t first[] = {256, 512}, second[] = {0, 768};
    assert(sdk_alpha_snapshot(&m, 0, first));
    assert(!sdk_alpha_snapshot(&m, 0, first)); /* stable output needs no redraw */
    assert(!strcmp(sdk_alpha_label(&m, 1), "COPIED"));
    buffer[0] = 'X';
    assert(!strcmp(sdk_alpha_label(&m, 1), "COPIED")); /* SDK owns the copy. */
    assert(!strcmp(buffer, "XOPIED")); /* Never writes into caller memory. */
    sdk_alpha_snapshot(&m, 1, second);
    assert(!strcmp(sdk_alpha_label(&m, 1), "TARGET")); /* other instance/default */
    sdk_alpha_snapshot(&m, 1, first);
    assert(!strcmp(sdk_alpha_label(&m, 1), "XOPIED")); /* off-page source */
    const char *want[] = {"XOPIED", "LITERAL", "12345678", "", "TARGET", "A??Z"};
    for (mode = 0; mode < 6; ++mode) {
        sdk_alpha_snapshot(&m, 1, first);
        assert(!strcmp(sdk_alpha_label(&m, 1), want[mode]));
        assert(first[0] == 256 && first[1] == 512);
    }
    first[0] = ELEK_UI_RAW_MAX;
    sdk_alpha_snapshot(&m, 0, first);
    assert(!sdk_alpha_visible(&m, 1) && sdk_alpha_visible(&m, 0));
    sdk_alpha_snapshot(&m, 1, first);
    assert(!sdk_alpha_visible(&m, 1)); /* same logical knob, other page */
    sdk_alpha_snapshot(&m, 1, second);
    assert(sdk_alpha_visible(&m, 1)); /* another instance restores visibility */
    first[0] = 256;
    sdk_alpha_snapshot(&m, 0, first);
    assert(sdk_alpha_visible(&m, 1) && first[1] == 512);
    struct elek_machine without = m; without.present = 0;
    sdk_alpha_snapshot(&without, 0, first);
    assert(!strcmp(sdk_alpha_label(&without, 1), "TARGET"));
    assert(!sdk_alpha_label(&without, 2));
    return 0;
}
