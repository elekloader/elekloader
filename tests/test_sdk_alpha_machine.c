/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The shared part (mods/sdk-alpha/machine.c) as the Digitone II builds it, table by table: the value
 * mapping over the whole signed range, the defaults' inverse, the value text (the number or .present's),
 * the index's checks, label sanitising, the skip of unchanged inputs and the glyph blit. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../mods/sdk-alpha/machine.c"

#define R SDK_ALPHA_RAW_MAX

static uint8_t lit[64][64];
void dn2_fillrect(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t on)
{
    (void)bmp;
    for (int32_t y = y0; y <= y1; ++y)
        for (int32_t x = x0; x <= x1; ++x)
            lit[y][x] = (uint8_t)on;
}

/* The active track's machine, for the value texts. */
static const struct sdk_alpha_entry *active;
const struct sdk_alpha_entry *dn2_sdk_active(void)
{
    return active;
}

/* The present callback: one label (0 keeps the declared one), visibility and glyph per call, from these. */
static const char *want_label, *want_value;  /* want_value "V": "V" and knob 0's value */
static int null_label;                       /* assigns label a null pointer */
static uint32_t want_visible = 1, presents, seen_track_p;
static int32_t seen_page, seen_value, seen_other;
static char seen_label[ELEK_UI_LABEL_MAX + 1];
static const struct elek_glyph glyph = { .rows = { 0x8000, [15] = 0x0001 } };
static const struct elek_glyph *want_glyph;
static void present(const struct elek_ui *ui)
{
    ++presents;
    seen_page = ui->page;
    seen_value = ui->knob[0];
    seen_other = ui->knob[1];
    seen_track_p = ui->track;
    strcpy(seen_label, ui->view[0].label);
    /* each call starts from the declaration */
    assert(!ui->view[0].hidden && ui->view[0].glyph == 0 && !ui->view[0].value && !ui->view[1].value);
    if (want_value && !strcmp(want_value, "V"))
        ui->view[0].value = elek_num(ui, "V", ui->knob[0]);
    else
        ui->view[0].value = want_value;
    if (want_label)
        ui->view[0].label = want_label;
    if (null_label)
        ui->view[0].label = 0;
    ui->view[0].hidden = !want_visible;
    if (want_glyph) ui->view[0].glyph = want_glyph;
}

static const struct elek_knob knobs[] = { { "A", .min = -50, .max = 50 }, { "B", .max = 127 } };
static const struct elek_knob other[] = { { "C", .max = 127 } };
static const struct elek_knob many[16] = { { "M", .max = 127 } };
static const struct elek_machine good = {
    .id = 100, .name = "Good", .short_name = "GOOD", ELEK_KNOBS(knobs),
    .pages = { { "P1", { &knobs[0], &knobs[1] } }, { "P2", { [7] = &knobs[0] } } }, .present = present,
};
static const struct elek_machine empty = { .id = 101, .name = "Empty", .short_name = "EMPT" };
static const struct elek_machine foreign = {        /* a page holds another array's knob */
    .id = 102, .name = "Foreign", .short_name = "FORN", ELEK_KNOBS(knobs), .pages = { { "F", { &other[0] } } },
};
static const struct elek_machine beyond = {         /* a knob past knob_count */
    .id = 103, .name = "Beyond", .short_name = "BYND", .knobs = knobs, .knob_count = 1,
    .pages = { { "B", { &knobs[1] } } },
};
static const struct elek_machine unlabelled = {     /* knobs on a page that is absent */
    .id = 104, .name = "Unlabelled", .short_name = "UNLB", ELEK_KNOBS(knobs), .pages = { { 0, { &knobs[0] } } },
};
static const struct elek_machine too_many = { .id = 105, .name = "Many", .short_name = "MANY", ELEK_KNOBS(many) };
ELEK_REGISTER_MACHINE(good);
ELEK_REGISTER_MACHINE(empty);
ELEK_REGISTER_MACHINE(foreign);
ELEK_REGISTER_MACHINE(beyond);
ELEK_REGISTER_MACHINE(unlabelled);
ELEK_REGISTER_MACHINE(too_many);
const char sdk_alpha_abi_v1[] = "sdk alpha 1";
const struct elek_machine *const sdk_alpha_machines[] = { &good, &empty, &foreign, &beyond, &unlabelled, &too_many, 0 };

static int32_t present_label(const struct sdk_alpha_entry *e, const char *label, uint32_t visible)
{
    int32_t raw[2] = { 0, 0 };
    want_label = label;
    want_visible = visible;
    return sdk_alpha_present(e, 0, 0, raw, 1);
}

int main(void)
{
    /* Mapping, raw clamped: up to 128 values, an equal share of the 128 positions each (raw's nearest
     * position * values / 128); more, the split product equals 64-bit nearest rounding, halves toward max. */
    static const struct { int32_t min, max; } bounds[] = {
        { 0, 127 }, { -50, 50 }, { 0, 1 }, { 0, 3 }, { 0, 2 }, { -64, 63 }, { 0, 128 }, { -1000000, 1000000 },
        { INT32_MIN, INT32_MAX }, { INT32_MIN, INT32_MIN + 1 }, { INT32_MAX - 1, INT32_MAX },
    };
    static const int32_t raws[] = { INT32_MIN, -1, 0, 1, 127, 128, R / 2, R / 2 + 1, R - 1, R, R + 1, INT32_MAX };
    for (unsigned i = 0; i < sizeof bounds / sizeof *bounds; ++i) {
        struct elek_knob k = { .label = "K", .min = bounds[i].min, .max = bounds[i].max };
        int64_t span = (int64_t)bounds[i].max - bounds[i].min;
        int32_t last = INT32_MIN;
        for (unsigned j = 0; j < sizeof raws / sizeof *raws; ++j) {
            int64_t r = raws[j] < 0 ? 0 : raws[j] > R ? R : raws[j];
            int64_t want = span < 128 ? bounds[i].min + ((r + 128) >> 8) * (span + 1) / 128
                                      : bounds[i].min + (span * r + R / 2) / R;
            int32_t got = sdk_alpha_map(&k, raws[j]);
            assert(got == want && got >= last);
            last = got;
        }
        assert(sdk_alpha_map(&k, 0) == bounds[i].min && sdk_alpha_map(&k, R) == bounds[i].max);
        if (span < 128)                 /* each value spans 128 / values positions, as near as whole ones go */
            for (int64_t v = 0; v <= span; ++v) {
                int32_t first = 0;
                while (sdk_alpha_map(&k, first << 8) < bounds[i].min + v) ++first;
                assert(first == (v * 128 + span) / (span + 1));
            }

        /* Defaults: the first native step that reaches the default, clamped into min..max (0 when
         * omitted). */
        int32_t defs[] = { bounds[i].min, bounds[i].max, (int32_t)(bounds[i].min + span / 2),
                           (int32_t)(bounds[i].min + span / 3), 0, INT32_MIN, INT32_MAX };
        for (unsigned j = 0; j < sizeof defs / sizeof *defs; ++j) {
            k.def = defs[j];
            int32_t d = sdk_alpha_default(&k), want = defs[j] < k.min ? k.min : defs[j] > k.max ? k.max : defs[j];
            assert(d >= 0 && d <= R && sdk_alpha_map(&k, d) >= want);
            assert(d == 0 || sdk_alpha_map(&k, d - 1) < want);
        }
    }

    /* Clicks: turning one click at a time from either end, each value holds exactly `clicks` clicks and the
     * encoder stops at the ends; a faster turn's delta moves that many clicks; a knob without clicks keeps
     * the stock's delta. */
    static const struct { int32_t min, max, clicks; } stepped[] = {
        { 0, 1, 1 }, { 0, 1, 64 }, { 0, 3, 1 }, { 0, 3, 2 }, { 0, 3, 10 }, { 0, 3, 32 }, { 0, 2, 42 },
        { -32, 31, 2 }, { 0, 127, 1 }, { 5, 9, 25 },
    };
    for (unsigned i = 0; i < sizeof stepped / sizeof *stepped; ++i) {
        struct elek_knob k = { .label = "K", .min = stepped[i].min, .max = stepped[i].max, .clicks = stepped[i].clicks };
        int32_t values = k.max - k.min + 1, points = values * k.clicks;
        for (int dir = 1; dir >= -1; dir -= 2) {
            int32_t raw = dir > 0 ? 0 : R, held[128] = { 0 }, turns = 0;
            held[sdk_alpha_map(&k, raw) - k.min]++;
            for (;;) {
                int32_t next = raw + sdk_alpha_step(&k, raw, 256 * dir);
                assert(next >= 0 && next <= R && next % 256 == 0);
                if (next == raw)
                    break;                          /* the end: it stops */
                raw = next;
                held[sdk_alpha_map(&k, raw) - k.min]++;
                assert(++turns < points);
            }
            assert(turns == points - 1 && sdk_alpha_map(&k, raw) == (dir > 0 ? k.max : k.min));
            for (int32_t v = 0; v < values; ++v)
                assert(held[v] == k.clicks);
        }
        int32_t fast = sdk_alpha_step(&k, 0, 3 * 256 + 40);        /* three clicks at once */
        int32_t three = 0;
        for (int n = 0; n < 3; ++n)
            three += sdk_alpha_step(&k, three, 256);
        assert(fast == three);
        assert(sdk_alpha_step(&k, 0, 1) == sdk_alpha_step(&k, 0, 256));   /* a sliver is one click */
        /* From a value set between points (a CC, a load): it counts as its cell's point, so a click down and
         * one back up land on that point, with the value it had. */
        int32_t between = (R / 4 * 3) | 77, below = between + sdk_alpha_step(&k, between, -256);
        int32_t back = below + sdk_alpha_step(&k, below, 256);
        assert(below < between && back <= between && sdk_alpha_map(&k, back) == sdk_alpha_map(&k, between));
    }
    struct elek_knob plain = { .label = "K", .min = 0, .max = 3 };
    assert(sdk_alpha_step(&plain, 1000, 256) == 256 && sdk_alpha_step(&plain, 1000, -3) == -3);

    /* The value text: the number, "#" past five characters unless wide. */
    static const struct { int32_t value; const char *text, *wide; } numbers[] = {
        { 0, "0", "0" }, { 7, "7", "7" }, { -50, "-50", "-50" }, { 99999, "99999", "99999" },
        { -9999, "-9999", "-9999" }, { 100000, "#", "100000" }, { -10000, "#", "-10000" },
        { INT32_MAX, "#", "2147483647" }, { INT32_MIN, "#", "-2147483648" },
    };
    for (unsigned i = 0; i < sizeof numbers / sizeof *numbers; ++i) {
        char buf[12];
        sdk_alpha_number(numbers[i].value, buf, 0);
        assert(!strcmp(buf, numbers[i].text));
        sdk_alpha_number(numbers[i].value, buf, 1);
        assert(!strcmp(buf, numbers[i].wide));
    }

    /* The index: refused machines are not SDK machines; each encoder's knob id, by page. */
    const struct sdk_alpha_entry *e = sdk_alpha_find(100);
    assert(sdk_alpha_count == 2 && e && e->count == 2 && e->pages == 2);
    assert(e->encoder[0][0] == 0 && e->encoder[0][1] == 1 && e->encoder[0][2] == -1);
    assert(e->encoder[1][7] == 0 && e->encoder[1][0] == -1);
    /* A machine that declares no page shows one blank page with its name. */
    const struct sdk_alpha_entry *empty_entry = sdk_alpha_find(101);
    assert(empty_entry && empty_entry->count == 0 && empty_entry->pages == 1);
    assert(!strcmp(empty_entry->title[0], "Empty") && empty_entry->encoder[0][0] == -1);
    for (int32_t id = 102; id <= 106; ++id)
        assert(!sdk_alpha_find(id));

    /* A knob's formatter shows the active machine's mapped value; the native number for another. */
    char text[12];
    active = e;
    sdk_alpha_value_texts[0](0, text);
    assert(!strcmp(text, "-50"));
    sdk_alpha_value_texts[0](R / 2, text);
    assert(!strcmp(text, "0"));
    sdk_alpha_value_texts[0](R, text);
    assert(!strcmp(text, "50"));
    sdk_alpha_value_texts[1](64 << 8, text);
    assert(!strcmp(text, "64"));
    sdk_alpha_value_texts[2](0, text);                  /* past the machine's knobs */
    assert(!strcmp(text, "0"));
    active = 0;
    sdk_alpha_value_texts[0](0, text);
    assert(!strcmp(text, "0"));

    /* Labels: at most ELEK_UI_LABEL_MAX characters, nonprintable bytes become ?; untouched, the declared. */
    static const struct { const char *label, *shown; } labels[] = {
        { "ABCDEFGH", "ABCDE" }, { "a\tb\x7f\x80", "a?b??" }, { " ~", " ~" }, { 0, "A" }, { "", "" },
    };
    for (unsigned i = 0; i < sizeof labels / sizeof *labels; ++i) {
        present_label(e, labels[i].label, 1);
        assert(!strcmp(seen_label, "A"));
        assert(!strcmp(sdk_alpha_label(e, 0), labels[i].shown) && !strcmp(sdk_alpha_label(e, 1), "B"));
    }
    null_label = 1;                                     /* null: the declared label */
    present_label(e, "X", 1);
    assert(!strcmp(sdk_alpha_label(e, 0), "A"));
    null_label = 0;
    present_label(e, "", 1);
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
    assert(presents == 1 && seen_page == 1 && seen_value == 50);
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
    assert(sdk_alpha_label(e, 0) == knobs[0].label);           /* not the last presented: as declared */
    sdk_alpha_present(e, 1, ELEK_UI_NO_PAGE, raw, 0);
    assert(presents == 5);

    /* A value text is .present's for the shown value, the others as it last saw them; else the number. */
    int32_t shown[2] = { R / 2, 9 << 8 };
    sdk_alpha_present(e, 3, 1, shown, 0);
    char label[ELEK_UI_LABEL_MAX + 1];
    memcpy(label, sdk_alpha_label(e, 0), sizeof label);
    want_label = "VALUE";
    want_value = "V";
    active = e;
    sdk_alpha_value_texts[0](R, text);
    assert(!strcmp(text, "V50") && seen_value == 50 && seen_other == 9 && seen_page == 1 && seen_track_p == 3);
    sdk_alpha_value(e, 0, 0, text, 1);                  /* the pop-up: the same text */
    assert(!strcmp(text, "V-50"));
    sdk_alpha_value_texts[1](64 << 8, text);            /* knob 1 has no value text: its number */
    assert(!strcmp(text, "64"));
    want_value = "a\x01" "bcdefg";
    sdk_alpha_value_texts[0](R, text);
    assert(!strcmp(text, "a?bcd"));
    want_value = "";                                    /* empty: the number */
    sdk_alpha_value_texts[0](R, text);
    assert(!strcmp(text, "50"));
    assert(!strcmp(sdk_alpha_label(e, 0), label));     /* the presentation is untouched */
    want_label = 0;
    sdk_alpha_present(sdk_alpha_find(101), 1, 0, raw, 0);
    want_value = "V";
    sdk_alpha_value_texts[0](R / 2, text);              /* not the machine last presented: the defaults */
    assert(!strcmp(text, "V0") && seen_other == 0 && seen_page == ELEK_UI_NO_PAGE && seen_track_p == 0);
    want_value = 0;
    active = 0;

    /* The blit: cleared, then the glyph's top row at y + 16 and its bottom row at y. */
    memset(lit, 1, sizeof lit);
    sdk_alpha_blit(0, 10, 20, &glyph);
    assert(lit[36][10] && lit[21][25] && !lit[36][11] && !lit[20][25] && !lit[30][18]);
    assert(lit[19][10] && lit[20][9]);                  /* outside: untouched */
    return 0;
}
