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
static const struct mk2_synth sine = {7, 0, "SINE", "SINE"};
const struct mk2_synth *const mk2_voice_synths[] = {
#ifndef EMPTY
    &sine,
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
    dn2_ensure();
    unsigned n = dn2_list_bytes / 4;
#ifdef EMPTY
    assert(n == 5);
    assert(dn2_clone_type(5) == 4);
    assert(!strcmp((const char *)dn2_name(5), "MISSING"));
    for (unsigned t = 0; t < 16; ++t)
        assert(dn2_permit(5, t) == dn2_permit(4, t));
    (void)low; (void)high; (void)bad_id; (void)bad_clone;
    (void)sine;
    assert(!dn2_is_voice(5) && dn2_dsp_type(5) == 4);
#else
    assert(n == 8 && dn2_list[4] == 5 && dn2_list[5] == 7 && dn2_list[6] == 31);
    assert(dn2_is_voice(7) && dn2_dsp_type(7) == 7 && dn2_clone_type(7) == 0);
    assert(!dn2_is_voice(5) && dn2_dsp_type(5) == 0);
    const struct descriptor *page = (const struct descriptor *)dn2_voice_page(7, 0);
    assert(page && !strcmp(page->name, "SINE"));
    for (unsigned i = 0; i < 9; ++i)
        assert(page->params[i] == 0);
    assert(dn2_voice_page(7, 1) == (uintptr_t)&dn2_stock_descriptors[4]);
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
    return 0;
}
