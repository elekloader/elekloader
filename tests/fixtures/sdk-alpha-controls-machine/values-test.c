/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host-only mapping oracle and instance snapshots. */
#include <assert.h>
#include <limits.h>
#include "elekloader/sdk_alpha_machine.h"
int32_t sdk_alpha_map(const struct elek_ui_knob *, int32_t raw);
void sdk_alpha_snapshot(const struct elek_machine *, int32_t page, void *view);
const char cm_ui_v32[] = "host stub";
uint32_t core_machine_ui(int32_t id) { (void)id; return 0; }
uint32_t mp_stock_layout(int32_t id) { (void)id; return 0; }
int32_t mp_shown_value(void *view, uint32_t id, uint8_t *locked)
{
    (void)locked;
    int k = id >= 164 ? id - 164 + 8 : id - 0x84;
    assert(k >= 0 && k < 15);
    return ((int32_t *)view)[k];
}
static const struct elek_ui_knob knobs[] = {
    { .id = 0, .label = "DEFAULT" },
    { .id = 1, .label = "TEN", .min = ELEK_UI_BOUND(10), .max = ELEK_UI_BOUND(20) },
    { .id = 2, .label = "ZERO", .max = ELEK_UI_BOUND(0) },
    { .id = 3, .label = "MIN", .min = ELEK_UI_BOUND(10) },
    { .id = 4, .label = "MAX", .max = ELEK_UI_BOUND(20) },
    { .id = 5, .label = "EQUAL", .min = ELEK_UI_BOUND(-7), .max = ELEK_UI_BOUND(-7) },
    { .id = 6, .label = "SIGNED", .min = ELEK_UI_BOUND(-10), .max = ELEK_UI_BOUND(10) },
    { .id = 7, .label = "MINZERO", .min = ELEK_UI_BOUND(0) },
    { .id = 8, .label = "FULL", .min = ELEK_UI_BOUND(INT32_MIN), .max = ELEK_UI_BOUND(INT32_MAX) },
};
static const struct elek_ui_page pages[2] = {
    { .label = "FIRST", .knob = { &knobs[2], &knobs[3], &knobs[4], &knobs[0],
                                  &knobs[5], &knobs[6], &knobs[7], &knobs[1] } },
    { .label = "SECOND", .knob = { [ELEK_ENCODER_A] = &knobs[8],
                                   [ELEK_ENCODER_H] = &knobs[0] } },
};
static int32_t *expected;
static int expected_page, calls;
static void present(const struct elek_ui_context *ctx, struct elek_ui_presentation *out)
{
    assert(ctx->page_count == 2 && ctx->knob_count == 9 && ctx->page == expected_page);
    assert(!out->knob_count && !out->knob);
    for (int k = 0; k < 9; ++k) {
        int32_t raw = expected[k] < 0 ? 0 : expected[k] > ELEK_UI_RAW_MAX ? ELEK_UI_RAW_MAX : expected[k];
        assert(ctx->knob[k]->raw_value == raw);
        assert(ctx->knob[k]->value == sdk_alpha_map(&knobs[k], raw));
    }
    ++calls;
}
int main(void)
{
    for (int k = 0; k < 9; ++k) {
        int32_t min = knobs[k].min.supplied ? knobs[k].min.value : 0;
        int32_t max = knobs[k].max.supplied ? knobs[k].max.value : 127;
        int64_t previous = INT64_MIN;
        for (int raw = 0; raw <= ELEK_UI_RAW_MAX; ++raw) {
            int64_t want = min + (((int64_t)max - min) * raw + ELEK_UI_RAW_MAX / 2) / ELEK_UI_RAW_MAX;
            int32_t got = sdk_alpha_map(&knobs[k], raw);
            assert(got == want && got >= previous);
            previous = got;
        }
        assert(sdk_alpha_map(&knobs[k], 0) == min);
        assert(sdk_alpha_map(&knobs[k], ELEK_UI_RAW_MAX) == max);
    }
    assert(sdk_alpha_map(&knobs[0], 127) == 0);
    assert(sdk_alpha_map(&knobs[0], 128) == 1); /* exact half rounds toward maximum */
    assert(sdk_alpha_map(&knobs[1], 16256) == 15);
    int32_t first[15] = {0, 16256, 30000, 512, 32512, 300, 512, -1, 50000};
    int32_t second[15] = {32512, 32512, 1, 0, 256, 32512, 0, 0, 16256};
    struct elek_machine m = { .pages = pages, .page_count = 2, .present = present };
    expected = first; expected_page = 0; sdk_alpha_snapshot(&m, 0, first);
    expected = second; expected_page = 1; sdk_alpha_snapshot(&m, 1, second);
    expected = first; expected_page = 1; sdk_alpha_snapshot(&m, 1, first);
    assert(calls == 3); /* returning to an instance never reads the other instance */
    struct elek_ui_page reordered[] = { pages[1], pages[0] };
    m.pages = reordered;
    expected = first; expected_page = 0; sdk_alpha_snapshot(&m, 0, first);
    assert(calls == 4); /* page order never changes logical identities */
    struct elek_ui_knob bad = { .id = 15, .label = "BAD" };
    reordered[0].knob[0] = &bad;
    sdk_alpha_snapshot(&m, 0, first); assert(calls == 4);
    bad.id = 0; /* a different descriptor cannot reuse logical zero */
    sdk_alpha_snapshot(&m, 0, first); assert(calls == 4);
    reordered[0].knob[0] = 0; /* logical eight is now missing */
    reordered[1].knob[7] = &knobs[8]; /* logical one is now missing, leaving a gap */
    sdk_alpha_snapshot(&m, 0, first); assert(calls == 4);
    m.pages = pages;
    m.present = 0; sdk_alpha_snapshot(&m, 0, first); assert(calls == 4);
    return 0;
}
