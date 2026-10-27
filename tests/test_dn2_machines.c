/* SPDX-License-Identifier: GPL-2.0-or-later */
/* cc -std=c99 -Wall -Werror -I elekloader/sdk/include tests/test_dn2_machines.c -o /tmp/dn2-machines-test */
#include <assert.h>
#include <string.h>
#include "../mods/machines-dn2/machines.c"

const struct names dn2_stock_names[5] = {
    {"FM TONE", "FM", "tone"}, {"WAVETONE", "WT", "wave"},
    {"FM DRUM", "DRUM", "drum"}, {"SWARMER", "SW", "swarm"}, {"MIDI", "MIDI", "midi"}
};
const uint32_t dn2_stock_permit[5] = {0xffff0000, 0xaaaa0000, 0x55550000, 0xff000000, 0x00ff0000};
const struct descriptor dn2_stock_descriptors[5] = {
    {"FM TONE", "FM", {11}}, {"WAVETONE", "WT", {22}},
    {"FM DRUM", "DRUM", {33}}, {"SWARMER", "SW", {44}}, {"MIDI", "MIDI", {55}}
};
const char dn2_unknown_name[] = "unknown", dn2_unknown_label[] = "?";
static const struct mk2_synth low = {5, "LOW", "LOW"}, high = {31, "OTHER", "OTHER"};
static const struct mk2_synth bad_id = {32, "BAD", "BAD"}, unlabelled = {6, "BAD", 0};
static const struct mk2_synth duplicate = {5, "DUPLICATE", "DUP"};
static const struct mk2_synth sine = {7, "SINE", "SINE"}, plain = {8, "PLAIN", "PLAIN"};
static const struct mk2_synth roomy = {9, "ROOMY", "ROOMY"};
const struct mk2_synth *const mk2_voice_synths[] = {
#ifndef EMPTY
    &high, &bad_id, &unlabelled, &low, &sine, &plain, &roomy, &duplicate,
#endif
    0
};

static const struct mk2_param level = {"lev", 100}, detune = {"DET", 64}, last = {"X", 0};
static const struct mk2_param loud = {"LOUD", 128}, wide = {"TOOWIDE", 1}, blank = {"", 1};
static const struct mk2_page sine_pages[2] = {{{&level, 0, &detune}}, {{0, 0, 0, 0, 0, 0, 0, &last}}};
static const struct mk2_page bad_default[1] = {{{&loud}}}, bad_label[1] = {{{&wide}}};
static const struct mk2_page empty_label[1] = {{{&blank}}}, roomy_pages[6] = {{{0}}};
static const struct mk2_voice_pages pages_sine = {7, 2, sine_pages};
static const struct mk2_voice_pages pages_again = {7, 1, sine_pages};
static const struct mk2_voice_pages pages_missing = {10, 1, sine_pages};
static const struct mk2_voice_pages pages_none = {8, 0, sine_pages};
static const struct mk2_voice_pages pages_default = {8, 1, bad_default};
static const struct mk2_voice_pages pages_label = {8, 1, bad_label};
static const struct mk2_voice_pages pages_blank = {8, 1, empty_label};
static const struct mk2_voice_pages pages_six = {9, 6, roomy_pages};      /* over the ceiling */
static const struct mk2_voice_pages pages_full = {9, 4, roomy_pages};     /* 2 + 4 > 5 pages */
const struct mk2_voice_pages *const mk2_voice_pages[] = {
#ifndef EMPTY
    &pages_missing, &pages_none, &pages_default, &pages_label, &pages_blank,
    &pages_six, &pages_sine, &pages_again, &pages_full,
#endif
    0
};

const struct names dn2_stock_filter_names[6] = {
    {"MULTI-MODE", "MM", 0}, {"LOWPASS 4", "LP4", 0}, {"EQUALIZER", "EQ", 0},
    {"COMB-", "C-", 0}, {"LEGACY LP/HP", "LPHP", 0}, {"COMB+", "C+", 0}
};
const struct descriptor dn2_stock_filter_descriptors[6] = {
    {"MM", "MM", {45,46,47,48,36,37,38,49,10}}, {"LP4", "LP4", {39}},
    {"EQ", "EQ", {41}}, {"C-", "C-", {54}}, {"LPHP", "LPHP", {57}}, {"C+", "C+", {318}}
};
static uint32_t curve_args[3];
static void test_curve(uint32_t freq, uint32_t reso, uint32_t g, uint8_t y[51])
{
    curve_args[0] = freq; curve_args[1] = reso; curve_args[2] = g;
    for (unsigned x = 0; x < 51; ++x)
        y[x] = x % 3 == 0 ? 0xff : (uint8_t)(x * 2);  /* 0xff skipped, above 16 clamped */
}
/* The page's shown value: only G's row is asked for, on the page the hook passes. */
uint32_t dn2_shown(void *page, uint32_t code, uint8_t *flag)
{
    assert(page == (void *)0x5678 && code == G_CODE && flag);
    return 0x7f00;
}
static int32_t dots[51][2];
static unsigned dot_count;
void dn2_pixel(void *canvas, int32_t x, int32_t y, int32_t on)
{
    assert(canvas == (void *)0x1234 && on == 1 && dot_count < 51);
    dots[dot_count][0] = x; dots[dot_count++][1] = y;
}
static const struct mk2_param mode = {"mode", 5}, loud_g = {"LOUD", 128}, wide_g = {"TOOWIDE", 1};
static const struct mk2_filter filter_low = {6, "LOW FILTER", "LOWF", 0, &loud_g};
static const struct mk2_filter filter_high = {31, "comb high", "comb", test_curve, &mode};   /* a second G */
static const struct mk2_filter filter_unity = {7, "UNITY", "UNITY", test_curve, &mode};
static const struct mk2_filter filter_wide = {9, "WIDE", "WIDE", 0, &wide_g};
static const struct mk2_filter filter_duplicate = {6, "DUPLICATE", "DUP", test_curve};
static const struct mk2_filter filter_unnamed = {8, 0, "BAD"};
static const struct mk2_filter filter_outside = {32, "BAD", "BAD"};
static const struct mk2_filter filter_stock = {5, "BAD", "BAD"};
const struct mk2_filter *const mk2_voice_filters[] = {
#ifndef EMPTY
    &filter_unnamed, &filter_outside, &filter_stock, &filter_low, &filter_wide, &filter_unity,
    &filter_high, &filter_unity, &filter_duplicate,
#endif
    0
};

/* Stock lookups: synth maps for stock types, then the filter's, then a global table. */
static uint32_t fake_lookup(uint32_t track, uint32_t synth, uint32_t filter, uint32_t key, uint32_t base)
{
    static const uint32_t fm[][2] = {{40, 200}, {41, 201}, {42, 202}, {53, 217}};
    (void)track;
    if (synth == 0)
        for (unsigned i = 0; i < 4; ++i)
            if (fm[i][0] + base == key)
                return fm[i][1];
    if (filter == 0 && key == 23 + base)
        return 300;
    if (filter == 0 && key == 18 + base)
        return G_TEMPLATE;
    return key == 95 + base ? 10 : 0;
}
uint32_t dn2_stock_cc(uint32_t t, uint32_t s, uint32_t f, uint32_t k) { return fake_lookup(t, s, f, k, 0); }
uint32_t dn2_stock_nrpn(uint32_t t, uint32_t s, uint32_t f, uint32_t k) { return fake_lookup(t, s, f, k, 160); }

static void check_midi(void)
{
#ifndef EMPTY
    /* SINE (7) on its host FM TONE: a CC stands for its slot, and a slot is SINE's knob there. */
    assert(dn2_cc_code(0, 7, 0, 40) == ROWS && dn2_cc_code(0, 7, 0, 42) == ROWS + 2);
    assert(dn2_cc_code(0, 7, 0, 53) == ROWS + 15);
    assert(dn2_cc_code(0, 7, 0, 41) == 0);                       /* slot 26 is empty */
    assert(dn2_cc_code(0, 7, 0, 23) == 300 && dn2_cc_code(0, 7, 0, 95) == 10);
    assert(dn2_nrpn_code(0, 7, 0, 200) == ROWS && dn2_nrpn_code(0, 7, 0, 201) == 0);
    assert(dn2_nrpn_code(0, 7, 0, 183) == 300);
    assert(dn2_t1[ROWS].cc == 40 && dn2_t1[ROWS + 2].cc == 42 && dn2_t1[ROWS + 15].cc == 53);
#endif
    /* Without pages (8, 5) or not a voice (0, a missing 6): the stock lookup with the synth's own
     * type, so a custom synth's CCs never reach the host's parameters. */
    assert(dn2_cc_code(0, 8, 0, 40) == 0 && dn2_cc_code(0, 8, 0, 23) == 300);
    assert(dn2_cc_code(0, 0, 0, 40) == 200 && dn2_cc_code(0, 5, 0, 40) == 0);
    /* A custom filter asks LOWPASS 4's maps (which have no slot-66 row); G answers MULTI-MODE's
     * number for it. Stock filters and a missing ID (its getter gives MULTI-MODE) are unchanged. */
    assert(dn2_cc_code(0, 0, 0, 18) == G_TEMPLATE && dn2_cc_code(0, 0, 1, 18) == 0);
    assert(dn2_cc_code(0, 0, 8, 18) == 0 && dn2_cc_code(0, 0, 0xffffffff, 18) == 0);
#ifndef EMPTY
    assert(dn2_cc_code(0, 0, 7, 18) == G_CODE && dn2_nrpn_code(0, 0, 7, 178) == G_CODE);
    assert(dn2_cc_code(0, 0, 6, 18) == 0 && dn2_cc_code(0, 0, 7, 23) == 0);   /* no G; host's map */
    assert(dn2_cc_code(0, 7, 7, 40) == ROWS && dn2_cc_code(0, 7, 7, 18) == G_CODE);
#else
    assert(dn2_cc_code(0, 0, 7, 18) == 0);
#endif
    assert(dn2_cc_code(0, 6, 0, 95) == 10 && dn2_nrpn_code(0, 0, 0, 201) == 201);
}

static void check_pages(void)
{
    assert(dn2_value_code(TEMPLATE) == TEMPLATE && dn2_value_code(ROWS - 1) == ROWS - 1);
    assert(dn2_value_code(ROWS) == TEMPLATE && dn2_value_code(ROWS + ADDED - 1) == TEMPLATE);
    assert(dn2_value_code(G_CODE + 1) == G_CODE + 1);
    assert(dn2_voice_page_count(0) == 0 && dn2_voice_page_count(6) == 0);
#ifdef EMPTY
    assert(dn2_voice_page_count(7) == 0 && dn2_voice_code(7, SLOT) == 0);
    (void)pages_sine; (void)pages_again; (void)pages_missing; (void)pages_none;
    (void)pages_default; (void)pages_label; (void)pages_blank; (void)pages_full; (void)pages_six;
#else
    assert(dn2_voice_page_count(7) == 2);
    const struct descriptor *one = (const struct descriptor *)dn2_voice_page(7, 0);
    const struct descriptor *two = (const struct descriptor *)dn2_voice_page(7, 1);
    assert(!strcmp(one->name, "SYN1") && !strcmp(two->name, "SYN2") && tabs[0].references == -1);
    assert(!strcmp(one->label, "SINE") && !strcmp(two->label, "SINE"));
    const uint32_t first[9] = {ROWS, 0, ROWS + 2, 0, 0, 0, 0, 0, PAGE_END};
    const uint32_t second[9] = {0, 0, 0, 0, 0, 0, 0, ROWS + 15, PAGE_END};
    assert(!memcmp(one->params, first, sizeof(first)) && !memcmp(two->params, second, sizeof(second)));
    assert(dn2_voice_page(7, 2) == (uintptr_t)&dn2_stock_descriptors[4]);
    const struct row *r = &dn2_t1[ROWS], *t = &dn2_t1[TEMPLATE];
    assert(r->owner == 0 && r->slot == SLOT && r->min == 0 && r->max == 127 << 8);
    assert(r->def == 100 << 8 && r->cc == 40 && !strcmp(r->label, "LEV") && r->name == r->label);
    assert(!strcmp(r->owner_name, "SINE"));
    assert(r->flag == t->flag && r->cc_low == t->cc_low && r->kind == t->kind && r->mask == t->mask
           && r->format == t->format && r->end == t->end && r->unknown == t->unknown);
    assert(dn2_t1[ROWS + 2].slot == SLOT + 2 && dn2_t1[ROWS + 2].def == 64 << 8);
    assert(dn2_t1[ROWS + 15].slot == SLOT + 15 && dn2_t1[ROWS + 15].def == 0);
    assert(dn2_t1[ROWS + 1].slot == 0);                       /* empty knobs get no row */
    assert(dn2_voice_code(7, SLOT) == ROWS && dn2_voice_code(7, SLOT + 1) == 0);
    assert(dn2_voice_code(7, SLOT + 15) == ROWS + 15 && dn2_voice_code(7, SLOT + 16) == 0);
    assert(dn2_voice_code(7, SLOT - 1) == 0 && dn2_voice_code(7, 0xffffffff) == 0);
    /* Invalid entries leave their synth with one empty page. */
    assert(dn2_voice_page_count(8) == 1 && dn2_voice_code(8, SLOT) == 0);
    assert(dn2_voice_page(8, 0) == dn2_descriptor(8));
    assert(dn2_voice_page_count(9) == 1 && used_pages == 2);
#endif
}

static void check_filters(void)
{
    dn2_filters_ensure();
    const uint32_t stock[] = {0, 1, 4, 3, 5, 2};
    assert(!memcmp(dn2_filter_list, stock, sizeof(stock)));
    for (unsigned i = 0; i < 6; ++i) {
        assert(dn2_filter_host_type(i) == i);
        assert(dn2_filter_dsp_type(i) == i);
        assert(dn2_filter_descriptor(i) == (uintptr_t)&dn2_stock_filter_descriptors[i]);
        assert(dn2_filter_name(i) == (uintptr_t)dn2_stock_filter_names[i].name);
        assert(dn2_filter_label(i) == (uintptr_t)dn2_stock_filter_names[i].label);
    }
#ifdef EMPTY
    assert(dn2_filter_list_bytes == 24);
    assert(dn2_filter_host_type(31) == 0);
    assert(dn2_filter_dsp_type(7) == 0);
    assert(!strcmp((const char *)dn2_filter_name(6), "MISSING"));
    assert(!strcmp((const char *)dn2_filter_name(7), "MISSING"));
    assert(dn2_filter_descriptor(7) == (uintptr_t)&dn2_stock_filter_descriptors[0]);
    (void)filter_low; (void)filter_high; (void)filter_unity; (void)filter_duplicate; (void)filter_wide;
    assert(!dn2_filter_code(G_SLOT, 7) && dn2_t1[G_CODE].slot == 0);
    (void)filter_unnamed; (void)filter_outside; (void)filter_stock;
#else
    assert(dn2_filter_list_bytes == 40);
    assert(dn2_filter_list[6] == 6 && dn2_filter_list[7] == 7 && dn2_filter_list[8] == 9
           && dn2_filter_list[9] == 31);
    /* G: UNITY (7) has the row, a copy of MULTI-MODE's slot-66 row owned by LOWPASS 4 (6), in
     * its page's G cell. LOW's default and WIDE's label are invalid; COMB's is a second G. */
    const struct row *g = &dn2_t1[G_CODE], *gt = &dn2_t1[G_TEMPLATE];
    assert(g->owner == 6 && g->slot == G_SLOT && g->def == 5 << 8 && g->min == 0 && g->max == 127 << 8);
    assert(!strcmp(g->label, "MODE") && g->name == g->label && g->owner_name == gt->owner_name);
    assert(g->cc == 18 && g->kind == gt->kind && g->mask == gt->mask && g->format == gt->format);
    assert(dn2_value_code(G_CODE) == G_TEMPLATE && dn2_value_code(G_CODE - 1) == TEMPLATE);
    assert(((const struct descriptor *)dn2_filter_descriptor(7))->params[G_CELL] == G_CODE);
    for (uint32_t id = 6; id < 32; ++id)
        if (id != 7) {
            assert(!dn2_filter_code(G_SLOT, id));
            if (filters[id])
                assert(((const struct descriptor *)dn2_filter_descriptor(id))->params[G_CELL] == 0);
        }
    assert(dn2_filter_code(G_SLOT, 7) == G_CODE && !dn2_filter_code(67, 7) && !dn2_filter_code(G_SLOT, 1));
    /* Every custom filter reads as LOWPASS 4 (1) and keeps its ID for the DSP. */
    assert(dn2_filter_host_type(6) == 1 && dn2_filter_host_type(7) == 1 && dn2_filter_host_type(31) == 1);
    assert(dn2_filter_dsp_type(6) == 6 && dn2_filter_dsp_type(7) == 7
           && dn2_filter_dsp_type(31) == 31);
    assert(dn2_is_voice(7) && dn2_dsp_type(7) == 7);  /* Same ID, separate categories. */
    const struct descriptor *voice = (const struct descriptor *)dn2_filter_descriptor(7);
    assert(!strcmp(voice->name, "UNITY") && !strcmp(voice->label, "UNITY"));
    for (unsigned i = 0; i < 9; ++i)
        assert(voice->params[i] == (i == G_CELL ? G_CODE : dn2_stock_filter_descriptors[1].params[i]));
    const struct descriptor *low = (const struct descriptor *)dn2_filter_descriptor(6);
    assert(!memcmp(low->params, dn2_stock_filter_descriptors[1].params, sizeof(low->params)));
    assert(!strcmp((const char *)dn2_filter_name(6), "LOW FILTER"));   /* the first entry for 6 */
    const struct descriptor *d = (const struct descriptor *)dn2_filter_descriptor(31);
    assert(d->params[0] == 39 && !strcmp(d->name, "COMB HIGH"));
    assert(filter_strings[31][0].references == -1);
    assert(!strcmp((const char *)dn2_filter_label(31), "comb"));
#endif
    assert(dn2_filter_host_type(8) == 0 && dn2_filter_host_type(0xffffffff) == 0xffffffff);
    assert(dn2_filter_dsp_type(8) == 0 && dn2_filter_dsp_type(0xffffffff) == 0xffffffff);
    assert(dn2_filter_name(32) == (uintptr_t)dn2_unknown_name);
    assert(dn2_filter_label(32) == (uintptr_t)dn2_unknown_label);
    assert(dn2_filter_descriptor(8) == (uintptr_t)&dn2_stock_filter_descriptors[0]);
    unsigned size = dn2_filter_list_bytes;
    dn2_filters_ensure();
    assert(size == dn2_filter_list_bytes);
    /* Only a registered custom filter with a curve draws: not one without, a missing ID or a stray type. */
    for (unsigned type = 0; type < 40; ++type)
        if (type != 7 && type != 31)
            dn2_filter_draw_curve(type, (void *)0x1234, 1, 2, (void *)0x5678);
    assert(dot_count == 0);
#ifndef EMPTY
    dn2_filter_draw_curve(31, (void *)0x1234, 0x4000, 0x2180, (void *)0x5678);
    assert(curve_args[0] == 0x4000 && curve_args[1] == 0x2180 && curve_args[2] == 0);  /* no G: 0 */
    dot_count = 0;
    dn2_filter_draw_curve(7, (void *)0x1234, 0x4000, 0x2180, (void *)0x5678);
    assert(curve_args[0] == 0x4000 && curve_args[1] == 0x2180 && curve_args[2] == 0x7f00);  /* G's shown word */
    unsigned n = 0;
    for (unsigned x = 0; x < 51; ++x) {
        if (x % 3 == 0)
            continue;
        unsigned v = x * 2 > 16 ? 16 : x * 2;
        assert(dots[n][0] == (int32_t)(21 + x) && dots[n][1] == (int32_t)(7 + v));
        ++n;
    }
    assert(dot_count == n);
#endif
}

int main(void)
{
    dn2_t1[TEMPLATE] = (struct row){1, 36, 0, 32512, 8192, 1, 49, 7, 0xd2, 3, 4,
                                    "A Env Decay", "FM Tone", "DEC", 0x400e3030, 5};
    /* FM TONE rows for slots 25, 26, 27 and 40, with their CCs; a filter row. */
    const int32_t fm[][3] = {{200, 25, 40}, {201, 26, 41}, {202, 27, 42}, {217, 40, 53}};
    for (unsigned i = 0; i < 4; ++i)
        dn2_t1[fm[i][0]] = (struct row){0, fm[i][1], 0, 127, 0, 0, fm[i][2], -1};
    dn2_t1[300] = (struct row){5, 67, 0, 127, 0, 0, 23, -1};
    dn2_t1[G_TEMPLATE] = (struct row){5, 66, 0, 32512, 0, 0, 18, -1, 0x96, 0x29, 0x1e00,
                                      "Filter Type", "Filter", "TYPE", 0x400e3030, 5};
    dn2_ensure();
    unsigned n = dn2_list_bytes / 4;
#ifdef EMPTY
    assert(n == 5);
    assert(dn2_host_type(5) == 4);
    assert(!strcmp((const char *)dn2_name(5), "MISSING"));
    for (unsigned t = 0; t < 16; ++t)
        assert(dn2_permit(5, t) == dn2_permit(4, t));
    (void)low; (void)high; (void)bad_id; (void)unlabelled; (void)duplicate;
    (void)sine; (void)plain; (void)roomy;
    assert(!dn2_is_voice(5) && dn2_dsp_type(5) == 4);
#else
    assert(n == 10 && dn2_list[4] == 5 && dn2_list[5] == 7 && dn2_list[8] == 31);
    /* Every custom synth reads as FM TONE (0) and keeps its ID for the DSP. */
    assert(dn2_is_voice(7) && dn2_dsp_type(7) == 7 && dn2_host_type(7) == 0);
    assert(dn2_is_voice(5) && dn2_dsp_type(5) == 5 && dn2_host_type(31) == 0);
    const struct descriptor *page = (const struct descriptor *)dn2_voice_page(8, 0);
    assert(page && !strcmp(page->name, "PLAIN"));
    for (unsigned i = 0; i < 9; ++i)
        assert(page->params[i] == 0);
    assert(dn2_voice_page(8, 1) == (uintptr_t)&dn2_stock_descriptors[4]);
    assert(!dn2_voice_page(6, 0) && !dn2_voice_page(0, 0) && !dn2_is_voice(32));
    assert(!strcmp((const char *)dn2_name(5), "LOW"));      /* the first entry for 5 */
    assert(!strcmp((const char *)dn2_label(31), "OTHER"));
    assert(dn2_hint(31) == (uintptr_t)dn2_stock_names[0].hint);
    const struct descriptor *d = (const struct descriptor *)dn2_descriptor(5);
    assert(d->params[0] == 0 && !strcmp(d->name, "LOW"));
    assert(strings[5][0].length == 3 && strings[5][0].references == -1);
    for (unsigned t = 0; t < 16; ++t)
        assert(dn2_permit(31, t) == dn2_permit(0, t) && dn2_permit(31, t));
#endif
    assert(dn2_list[0] == 0 && dn2_list[1] == 2 && dn2_list[2] == 1 && dn2_list[3] == 3);
    assert(dn2_list[n-1] == 4);
    assert(dn2_host_type(6) == 4 && dn2_host_type(0xffffffff) == 0xffffffff);
    assert(dn2_descriptor(6) == (uintptr_t)&dn2_stock_descriptors[4]);
    for (unsigned t = 0; t < 16; ++t)
        assert(dn2_permit(6, t) == dn2_permit(4, t));
    assert(!dn2_permit(32, 0) && !dn2_permit(5, 16) && !dn2_permit(0xffffffff, 0));
    assert(dn2_name(32) == (uintptr_t)dn2_unknown_name);
    for (unsigned id = 0; id < 5; ++id) {
        assert(dn2_descriptor(id) == (uintptr_t)&dn2_stock_descriptors[id]);
        assert(dn2_name(id) == (uintptr_t)dn2_stock_names[id].name);
        assert(dn2_host_type(id) == id);
    }
    dn2_ensure();
    assert(dn2_list_bytes == 4*n);
    check_filters();
    check_pages();
    check_midi();
    return 0;
}
