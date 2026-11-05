/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The shared part (mods/sdk-alpha/machine.c) as the Digitone II builds it, table by table: the value
 * mapping over the whole signed range, the defaults' inverse, the index's checks, label sanitising, the
 * skip of unchanged inputs and the glyph blit. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../mods/sdk-alpha/machine.c"

#define R ELEK_UI_RAW_MAX

static uint8_t lit[64][64];
void dn2_fillrect(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t on)
{
    (void)bmp;
    for (int32_t y = y0; y <= y1; ++y)
        for (int32_t x = x0; x <= x1; ++x)
            lit[y][x] = (uint8_t)on;
}

/* The present callback: one label, visibility and glyph per call, from these. */
static const char *want_label;
static uint32_t want_visible = 1, presents, seen_knob_count;
static int32_t seen_page, seen_value;
static const struct elek_ui_glyph glyph = { .rows = { 0x8000, [15] = 0x0001 } };
static const struct elek_ui_glyph *want_glyph;
static void present(const struct elek_ui_context *ctx, struct elek_ui_presentation *out)
{
    ++presents;
    seen_page = ctx->page;
    seen_knob_count = ctx->knob_count;
    seen_value = ctx->knob[0]->value;
    out->knob[0]->label = want_label;
    out->knob[0]->visible = want_visible;
    if (want_glyph) out->knob[0]->glyph = want_glyph;
}

static const struct elek_ui_knob a = { .id = 0, .label = "A", .min = ELEK_UI_BOUND(-50), .max = ELEK_UI_BOUND(50) };
static const struct elek_ui_knob b = { .id = 1, .label = "B" };
static const struct elek_ui_knob c = { .id = 2, .label = "C" };
static const struct elek_ui_knob a_too = { .id = 0, .label = "A2" };
static const struct elek_ui_page good_pages[] = { { .label = "P1", .knob = { &a, &b } }, { .label = "P2", .knob = { [7] = &a } } };
static const struct elek_ui_page gap_page[] = { { .label = "GAP", .knob = { &a, &c } } };
static const struct elek_ui_page twice_page[] = { { .label = "TWICE", .knob = { &a, &a_too } } };
static const struct elek_machine good = { .id = 100, .name = "Good", .short_name = "GOOD", .pages = good_pages, .page_count = 2, .present = present };
static const struct elek_machine empty = { .id = 101, .name = "Empty", .short_name = "EMPT" };
static const struct elek_machine gap = { .id = 102, .name = "Gap", .short_name = "GAP", .pages = gap_page, .page_count = 1 };
static const struct elek_machine twice = { .id = 103, .name = "Twice", .short_name = "TWIC", .pages = twice_page, .page_count = 1 };
static const struct elek_machine three = { .id = 104, .name = "Three", .short_name = "THRE", .pages = good_pages, .page_count = 3 };
static const struct elek_machine reserved = { .id = 105, .name = "Res", .short_name = "RES", ._reserved = { 1 } };
ELEK_REGISTER_MACHINE(good);
ELEK_REGISTER_MACHINE(empty);
ELEK_REGISTER_MACHINE(gap);
ELEK_REGISTER_MACHINE(twice);
ELEK_REGISTER_MACHINE(three);
ELEK_REGISTER_MACHINE(reserved);
const char sdk_alpha_dt1_ui_v1[] = "sdk alpha 1";
const struct sdk_alpha_registration *const sdk_alpha_machines[] = {
    &sdk_alpha_registration_good, &sdk_alpha_registration_empty, &sdk_alpha_registration_gap,
    &sdk_alpha_registration_twice, &sdk_alpha_registration_three, &sdk_alpha_registration_reserved, 0
};

static int32_t present_label(const struct sdk_alpha_entry *e, const char *label, uint32_t visible)
{
    int32_t raw[2] = { 0, 0 };
    want_label = label;
    want_visible = visible;
    return sdk_alpha_present(e, 0, 0, raw, 1);
}

int main(void)
{
    /* Mapping: the split product equals 64-bit nearest rounding, halves toward max, raw clamped. */
    static const struct { int32_t min, max; int supplied; } bounds[] = {
        { 0, 127, 0 }, { -50, 50, 1 }, { 0, 1, 1 }, { 7, 7, 1 }, { -1000000, 1000000, 1 },
        { INT32_MIN, INT32_MAX, 1 }, { INT32_MIN, INT32_MIN + 1, 1 }, { INT32_MAX - 1, INT32_MAX, 1 },
    };
    static const int32_t raws[] = { INT32_MIN, -1, 0, 1, 127, 128, R / 2, R / 2 + 1, R - 1, R, R + 1, INT32_MAX };
    for (unsigned i = 0; i < sizeof bounds / sizeof *bounds; ++i) {
        struct elek_ui_knob k = { .label = "K" };
        if (bounds[i].supplied) {
            k.min = (struct elek_ui_bound)ELEK_UI_BOUND(bounds[i].min);
            k.max = (struct elek_ui_bound)ELEK_UI_BOUND(bounds[i].max);
        }
        int64_t span = (int64_t)bounds[i].max - bounds[i].min;
        int32_t last = INT32_MIN;
        for (unsigned j = 0; j < sizeof raws / sizeof *raws; ++j) {
            int64_t r = raws[j] < 0 ? 0 : raws[j] > R ? R : raws[j];
            int64_t want = bounds[i].min + (span * r + R / 2) / R;
            int32_t got = sdk_alpha_map(&k, raws[j]);
            assert(got == want && got >= last);
            last = got;
        }
        assert(sdk_alpha_map(&k, 0) == bounds[i].min && sdk_alpha_map(&k, R) == bounds[i].max);

        /* Defaults: the first native step that reaches the default; none is step 0. */
        int32_t defs[] = { bounds[i].min, bounds[i].max, (int32_t)(bounds[i].min + span / 2),
                           (int32_t)(bounds[i].min + span / 3) };
        assert(sdk_alpha_default(&k) == 0);
        for (unsigned j = 0; j < sizeof defs / sizeof *defs; ++j) {
            k.def = (struct elek_ui_bound)ELEK_UI_BOUND(defs[j]);
            int32_t d = sdk_alpha_default(&k);
            assert(d >= 0 && d <= R && sdk_alpha_map(&k, d) >= defs[j]);
            assert(d == 0 || sdk_alpha_map(&k, d - 1) < defs[j]);
        }
    }

    /* The index: refused machines are not SDK machines; knobs by id across pages. */
    const struct sdk_alpha_entry *e = sdk_alpha_find(100);
    assert(sdk_alpha_count == 2 && e && e->count == 2 && e->knob[0] == &a && e->knob[1] == &b);
    assert(sdk_alpha_find(101) && sdk_alpha_find(101)->count == 0);
    for (int32_t id = 102; id <= 106; ++id)
        assert(!sdk_alpha_find(id));

    /* Labels: at most ELEK_UI_LABEL_MAX characters, nonprintable bytes become ?, null is the declared. */
    static const struct { const char *label, *shown; } labels[] = {
        { "ABCDEFGH", "ABCDE" }, { "a\tb\x7f\x80", "a?b??" }, { " ~", " ~" }, { 0, "A" }, { "", "" },
    };
    for (unsigned i = 0; i < sizeof labels / sizeof *labels; ++i) {
        present_label(e, labels[i].label, 1);
        assert(!strcmp(sdk_alpha_label(e, 0), labels[i].shown) && !strcmp(sdk_alpha_label(e, 1), "B"));
    }
    assert(!present_label(e, "", 1));                   /* the same presentation is no change */
    assert(present_label(e, "", 0) && !sdk_alpha_visible(e, 0) && sdk_alpha_visible(e, 1));
    assert(!sdk_alpha_label(e, 2) && !sdk_alpha_visible(e, 2));
    want_glyph = &glyph;
    assert(present_label(e, "", 1) && sdk_alpha_glyph(e, 0) == &glyph && !sdk_alpha_glyph(e, 1));
    want_glyph = 0;

    /* Present runs when the machine, track, page or a value changed, and not otherwise. */
    int32_t raw[2] = { R, 0 };
    presents = 0;
    sdk_alpha_present(e, 0, 1, raw, 0);
    assert(presents == 1 && seen_page == 1 && seen_value == 50 && seen_knob_count == 2);
    sdk_alpha_present(e, 0, 1, raw, 0);
    assert(presents == 1);
    raw[1] = -7;                                        /* clamps to 0: the same input */
    sdk_alpha_present(e, 0, 1, raw, 0);
    assert(presents == 1);
    raw[1] = 1;
    sdk_alpha_present(e, 0, 1, raw, 0);
    assert(presents == 2);
    sdk_alpha_present(e, 1, 1, raw, 0);
    assert(presents == 3);
    sdk_alpha_present(e, 1, 5, raw, 0);                 /* a page the machine lacks is no page */
    assert(presents == 4 && seen_page == ELEK_UI_NO_PAGE && sdk_alpha_seen_page == ELEK_UI_NO_PAGE);
    sdk_alpha_present(e, 1, ELEK_UI_NO_PAGE, raw, 0);
    assert(presents == 4);
    sdk_alpha_present(sdk_alpha_find(101), 1, 0, raw, 0);   /* another machine, no knobs, no present */
    assert(sdk_alpha_seen == sdk_alpha_find(101) && sdk_alpha_seen_page == ELEK_UI_NO_PAGE);
    assert(sdk_alpha_label(e, 0) == a.label);           /* not the last presented: as declared */
    sdk_alpha_present(e, 1, ELEK_UI_NO_PAGE, raw, 0);
    assert(presents == 5);

    /* The blit: cleared, then the glyph's top row at y + 16 and its bottom row at y. */
    memset(lit, 1, sizeof lit);
    sdk_alpha_blit(0, 10, 20, &glyph);
    assert(lit[36][10] && lit[21][25] && !lit[36][11] && !lit[20][25] && !lit[30][18]);
    assert(lit[19][10] && lit[20][9]);                  /* outside: untouched */
    return 0;
}
