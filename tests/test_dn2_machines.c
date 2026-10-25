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
static const struct mk2_synth low = {5, 0, "CLONE", "CLONE"}, high = {31, 3, "OTHER", "OTHER"};
static const struct mk2_synth bad_id = {32, 0, "BAD", "BAD"}, bad_clone = {6, 4, "BAD", "BAD"};
const struct mk2_synth *const mk2_synths[] = {
#ifndef EMPTY
    &high, &bad_id, &bad_clone, &low,
#endif
    0
};
static const struct mk2_synth sine = {7, 0, "SINE", "SINE"}, plain = {8, 1, "PLAIN", "PLAIN"};
static const struct mk2_synth roomy = {9, 2, "ROOMY", "ROOMY"};
const struct mk2_synth *const mk2_voice_synths[] = {
#ifndef EMPTY
    &sine, &plain, &roomy,
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
static const struct mk2_voice_pages pages_clone = {5, 1, sine_pages};
static const struct mk2_voice_pages pages_missing = {10, 1, sine_pages};
static const struct mk2_voice_pages pages_none = {8, 0, sine_pages};
static const struct mk2_voice_pages pages_default = {8, 1, bad_default};
static const struct mk2_voice_pages pages_label = {8, 1, bad_label};
static const struct mk2_voice_pages pages_blank = {8, 1, empty_label};
static const struct mk2_voice_pages pages_six = {9, 6, roomy_pages};      /* over the ceiling */
static const struct mk2_voice_pages pages_full = {9, 4, roomy_pages};     /* 2 + 4 > 5 pages */
const struct mk2_voice_pages *const mk2_voice_pages[] = {
#ifndef EMPTY
    &pages_clone, &pages_missing, &pages_none, &pages_default, &pages_label, &pages_blank,
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
static const struct mk2_filter filter_low = {6, 0, "CLONE FILTER", "CLONE"};
static const struct mk2_filter filter_high = {31, 5, "comb clone", "comb"};
static const struct mk2_filter filter_unity = {7, 1, "UNITY", "UNITY"};
static const struct mk2_filter filter_duplicate_clone = {6, 2, "DUPLICATE", "DUP"};
static const struct mk2_filter filter_bad = {8, 6, "BAD", "BAD"};
static const struct mk2_filter filter_outside = {32, 0, "BAD", "BAD"};
static const struct mk2_filter filter_stock = {5, 0, "BAD", "BAD"};
const struct mk2_filter *const mk2_filters[] = {
#ifndef EMPTY
    &filter_high, &filter_bad, &filter_outside, &filter_stock, &filter_low,
    &filter_low,
#endif
    0
};
const struct mk2_filter *const mk2_voice_filters[] = {
#ifndef EMPTY
    &filter_bad, &filter_outside, &filter_stock, &filter_unity, &filter_unity,
    &filter_duplicate_clone,
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
    return key == 95 + base ? 10 : 0;
}
uint32_t dn2_stock_cc(uint32_t t, uint32_t s, uint32_t f, uint32_t k) { return fake_lookup(t, s, f, k, 0); }
uint32_t dn2_stock_nrpn(uint32_t t, uint32_t s, uint32_t f, uint32_t k) { return fake_lookup(t, s, f, k, 160); }

static void check_midi(void)
{
#ifndef EMPTY
    /* SINE (7) clones FM TONE: a CC stands for its slot, and a slot is SINE's knob there. */
    assert(dn2_cc_code(0, 7, 0, 40) == ROWS && dn2_cc_code(0, 7, 0, 42) == ROWS + 2);
    assert(dn2_cc_code(0, 7, 0, 53) == ROWS + 15);
    assert(dn2_cc_code(0, 7, 0, 41) == 0);                       /* slot 26 is empty */
    assert(dn2_cc_code(0, 7, 0, 23) == 300 && dn2_cc_code(0, 7, 0, 95) == 10);
    assert(dn2_nrpn_code(0, 7, 0, 200) == ROWS && dn2_nrpn_code(0, 7, 0, 201) == 0);
    assert(dn2_nrpn_code(0, 7, 0, 183) == 300);
    assert(dn2_t1[ROWS].cc == 40 && dn2_t1[ROWS + 2].cc == 42 && dn2_t1[ROWS + 15].cc == 53);
    /* Without pages (8) or not a voice (0, the clone 5, a missing 6): the stock lookup. */
    assert(dn2_cc_code(0, 8, 0, 40) == 0 && dn2_cc_code(0, 8, 0, 23) == 300);
#endif
    /* MIDI's synth type: clones their stock type; voices, missing and stock IDs their own. */
    for (uint32_t t = 0; t < 5; ++t)
        assert(dn2_midi_type(t) == t);
    assert(dn2_midi_type(6) == 6 && dn2_midi_type(32) == 32 && dn2_midi_type(0xffffffff) == 0xffffffff);
#ifndef EMPTY
    assert(dn2_midi_type(5) == 0 && dn2_midi_type(31) == 3 && dn2_midi_type(7) == 7);
#else
    assert(dn2_midi_type(5) == 5 && dn2_midi_type(7) == 7);
#endif
    assert(dn2_cc_code(0, 0, 0, 40) == 200 && dn2_cc_code(0, 5, 0, 40) == 0);
    assert(dn2_cc_code(0, 6, 0, 95) == 10 && dn2_nrpn_code(0, 0, 0, 201) == 201);
}

static void check_pages(void)
{
    assert(dn2_value_code(TEMPLATE) == TEMPLATE && dn2_value_code(ROWS - 1) == ROWS - 1);
    assert(dn2_value_code(ROWS) == TEMPLATE && dn2_value_code(ROWS + ADDED - 1) == TEMPLATE);
    assert(dn2_value_code(ROWS + ADDED) == ROWS + ADDED);
    assert(dn2_voice_page_count(0) == 0 && dn2_voice_page_count(5) == 0);
#ifdef EMPTY
    assert(dn2_voice_page_count(7) == 0 && dn2_voice_code(7, SLOT) == 0);
    (void)pages_sine; (void)pages_again; (void)pages_clone; (void)pages_missing; (void)pages_none;
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
        assert(dn2_filter_clone(i) == i);
        assert(dn2_filter_dsp_type(i) == i);
        assert(dn2_filter_descriptor(i) == (uintptr_t)&dn2_stock_filter_descriptors[i]);
        assert(dn2_filter_name(i) == (uintptr_t)dn2_stock_filter_names[i].name);
        assert(dn2_filter_label(i) == (uintptr_t)dn2_stock_filter_names[i].label);
    }
#ifdef EMPTY
    assert(dn2_filter_list_bytes == 24);
    assert(dn2_filter_clone(31) == 0);
    assert(dn2_filter_dsp_type(7) == 0);
    assert(!strcmp((const char *)dn2_filter_name(6), "MISSING"));
    assert(!strcmp((const char *)dn2_filter_name(7), "MISSING"));
    assert(dn2_filter_descriptor(7) == (uintptr_t)&dn2_stock_filter_descriptors[0]);
    (void)filter_low; (void)filter_high; (void)filter_unity; (void)filter_duplicate_clone;
    (void)filter_bad; (void)filter_outside; (void)filter_stock;
#else
    assert(dn2_filter_list_bytes == 36);
    assert(dn2_filter_list[6] == 6 && dn2_filter_list[7] == 7 && dn2_filter_list[8] == 31);
    assert(dn2_filter_clone(6) == 0 && dn2_filter_clone(7) == 1 && dn2_filter_clone(31) == 5);
    assert(dn2_filter_dsp_type(6) == 0 && dn2_filter_dsp_type(7) == 7
           && dn2_filter_dsp_type(31) == 5);
    assert(dn2_is_voice(7) && dn2_dsp_type(7) == 7);  /* Same ID, separate categories. */
    const struct descriptor *voice = (const struct descriptor *)dn2_filter_descriptor(7);
    assert(!strcmp(voice->name, "UNITY") && !strcmp(voice->label, "UNITY"));
    for (unsigned i = 0; i < 9; ++i)
        assert(voice->params[i] == 0);
    assert(!strcmp((const char *)dn2_filter_name(6), "CLONE FILTER"));
    const struct descriptor *clone = (const struct descriptor *)dn2_filter_descriptor(6);
    assert(!memcmp(clone->params, dn2_stock_filter_descriptors[0].params, sizeof(clone->params)));
    const struct descriptor *d = (const struct descriptor *)dn2_filter_descriptor(31);
    assert(d->params[0] == 318 && !strcmp(d->name, "COMB CLONE"));
    assert(filter_strings[31][0].references == -1);
    assert(!strcmp((const char *)dn2_filter_label(31), "comb"));
#endif
    assert(dn2_filter_clone(8) == 0 && dn2_filter_clone(0xffffffff) == 0xffffffff);
    assert(dn2_filter_dsp_type(8) == 0 && dn2_filter_dsp_type(0xffffffff) == 0xffffffff);
    assert(dn2_filter_name(32) == (uintptr_t)dn2_unknown_name);
    assert(dn2_filter_label(32) == (uintptr_t)dn2_unknown_label);
    assert(dn2_filter_descriptor(8) == (uintptr_t)&dn2_stock_filter_descriptors[0]);
    unsigned size = dn2_filter_list_bytes;
    dn2_filters_ensure();
    assert(size == dn2_filter_list_bytes);
}

int main(void)
{
    dn2_t1[TEMPLATE] = (struct row){1, 36, 0, 32512, 8192, 1, 49, 7, 0xd2, 3, 4,
                                    "A Env Decay", "FM Tone", "DEC", 0x400e3030, 5};
    /* FM TONE rows for slots 25, 26, 27 and 40, with their CCs; a filter row. */
    const int32_t fm[][3] = {{200, 25, 40}, {201, 26, 41}, {202, 27, 42}, {217, 40, 53}};
    for (unsigned i = 0; i < 4; ++i)
        dn2_t1[fm[i][0]] = (struct row){0, fm[i][1], 0, 127, 0, 0, fm[i][2], -1};
    dn2_t1[300] = (struct row){5, 66, 0, 127, 0, 0, 23, -1};
    dn2_ensure();
    unsigned n = dn2_list_bytes / 4;
#ifdef EMPTY
    assert(n == 5);
    assert(dn2_clone_type(5) == 4);
    assert(!strcmp((const char *)dn2_name(5), "MISSING"));
    for (unsigned t = 0; t < 16; ++t)
        assert(dn2_permit(5, t) == dn2_permit(4, t));
    (void)low; (void)high; (void)bad_id; (void)bad_clone;
    (void)sine; (void)plain; (void)roomy;
    assert(!dn2_is_voice(5) && dn2_dsp_type(5) == 4);
#else
    assert(n == 10 && dn2_list[4] == 5 && dn2_list[5] == 7 && dn2_list[8] == 31);
    assert(dn2_is_voice(7) && dn2_dsp_type(7) == 7 && dn2_clone_type(7) == 0);
    assert(!dn2_is_voice(5) && dn2_dsp_type(5) == 0);
    const struct descriptor *page = (const struct descriptor *)dn2_voice_page(8, 0);
    assert(page && !strcmp(page->name, "PLAIN"));
    for (unsigned i = 0; i < 9; ++i)
        assert(page->params[i] == 0);
    assert(dn2_voice_page(8, 1) == (uintptr_t)&dn2_stock_descriptors[4]);
    assert(!dn2_voice_page(5, 0) && !dn2_is_voice(32));
    assert(dn2_clone_type(5) == 0 && dn2_clone_type(31) == 3);
    assert(!strcmp((const char *)dn2_name(5), "CLONE"));
    assert(!strcmp((const char *)dn2_label(31), "OTHER"));
    assert(dn2_hint(31) == (uintptr_t)dn2_stock_names[3].hint);
    const struct descriptor *d = (const struct descriptor *)dn2_descriptor(5);
    assert(d->params[0] == 11 && !strcmp(d->name, "CLONE"));
    assert(strings[5][0].length == 5 && strings[5][0].references == -1);
    for (unsigned t = 0; t < 16; ++t)
        assert(dn2_permit(31, t) == dn2_permit(3, t));
#endif
    assert(dn2_list[0] == 0 && dn2_list[1] == 2 && dn2_list[2] == 1 && dn2_list[3] == 3);
    assert(dn2_list[n-1] == 4);
    assert(dn2_clone_type(6) == 4 && dn2_clone_type(0xffffffff) == 0xffffffff);
    assert(dn2_descriptor(6) == (uintptr_t)&dn2_stock_descriptors[4]);
    for (unsigned t = 0; t < 16; ++t)
        assert(dn2_permit(6, t) == dn2_permit(4, t));
    assert(!dn2_permit(32, 0) && !dn2_permit(5, 16) && !dn2_permit(0xffffffff, 0));
    assert(dn2_name(32) == (uintptr_t)dn2_unknown_name);
    for (unsigned id = 0; id < 5; ++id) {
        assert(dn2_descriptor(id) == (uintptr_t)&dn2_stock_descriptors[id]);
        assert(dn2_name(id) == (uintptr_t)dn2_stock_names[id].name);
        assert(dn2_clone_type(id) == id);
    }
    dn2_ensure();
    assert(dn2_list_bytes == 4*n);
    check_filters();
    check_pages();
    check_midi();
    return 0;
}
