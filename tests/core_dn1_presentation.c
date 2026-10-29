/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mods/core-dn1/param_names.c"
const char native_long[] = "Native long", native_group[] = "SYN", native_short[] = "NAT";
/* Native 60-byte records, with real 32-bit name pointers in the fixture. */
__asm__(".pushsection .rodata\n.globl fw_params\nfw_params:\n.rept 185\n"
        ".zero 40\n.long native_long,native_group,native_short\n.zero 8\n.endr\n.popsection");
volatile uint32_t fw_slot_ids[79];
uint32_t core_param_name_call(uint32_t fn, int32_t id, int32_t field)
{ return (uint32_t)(uintptr_t)((const char *(*)(int32_t, int32_t))(uintptr_t)fn)(id, field); }
static int32_t active = 1, names = 1, prefer_first_visible, previous, stuck, calls, old_calls;
/* Valid original callback: the parameter is checked, field was always SHORT. */
static const char *old_name(int32_t id, int32_t field)
{ assert(field == CORE_PARAM_SHORT); old_calls++; return id == 16 ? "OLD" : 0; }
static const char *first_name(int32_t id, int32_t field)
{
    if (!names || id != 16) return 0;
    if (field == CORE_PARAM_LONG) return "A name too wide for the LFO list";
    return field == CORE_PARAM_GROUP ? "OSC" : 0;
}
static const char *second_name(int32_t id, int32_t field)
{
    if (!names || id != 16) return 0;
    return field == CORE_PARAM_SHORT ? "ATK" : "Later name must lose";
}
static int32_t abstain_visible(int32_t id)
{ return prefer_first_visible && id == 108 ? 1 : -1; }
static int32_t visible(int32_t id) { return active && id == 108 ? 0 : -1; }
static int32_t abstain_format(int32_t id, int32_t value, char out[CORE_PARAM_TEXT])
{ (void)id; (void)value; out[0] = '!'; return 0; }
static int32_t text(int32_t id, int32_t value, char out[CORE_PARAM_TEXT])
{
    (void)value;
    if (id != 16) return 0;
    memset(out, 'x', CORE_PARAM_TEXT); return 1; /* Core must terminate even this formatter. */
}
static const char *should_not_name(int32_t id, int32_t field)
{ (void)id; (void)field; assert(0); return 0; }
static int32_t should_not_format(int32_t id, int32_t v, char out[CORE_PARAM_TEXT])
{ (void)id; (void)v; (void)out; assert(0); return 0; }
static const uint32_t other_marker = 1;
/* Old 12-byte layout: wrong marker must be rejected before reading callbacks. */
static const struct { const uint32_t *abi; uint32_t format, visible; } old_layout = {&other_marker, 1, 1};
static const struct core_param_override old = {0, old_name};
static const struct core_param_presenter unknown = {0, should_not_name, should_not_format, 0};
static const struct core_param_presenter first = {&core_param_present_v2, first_name, abstain_format, abstain_visible};
static const struct core_param_presenter second = {&core_param_present_v2, second_name, text, visible};
const struct core_param_override *const core_param_override[] = {&old, 0};
const struct core_param_presenter *const core_param_presenters[] = {
    (const struct core_param_presenter *)&old_layout, &unknown, &first, &second, 0
};
static uint32_t ptr(const void *p) { assert((uintptr_t)p <= UINT32_MAX); return (uintptr_t)p; }
static const char *name(int32_t field) { return (void *)(uintptr_t)core_param_name(16, field); }
static int32_t get(uint32_t owner, int32_t id) { (void)owner; assert(id == 52 || id == 61); return previous; }
static int32_t step(uint32_t owner, int32_t id, int32_t value, int32_t delta)
{ (void)owner; (void)id; calls++; return stuck ? value : value + delta; }
int main(void)
{
    assert(!strcmp(name(CORE_PARAM_LONG), "A name too wide for the LFO list"));
    assert(!strcmp(name(CORE_PARAM_GROUP), "OSC") && !strcmp(name(CORE_PARAM_SHORT), "ATK"));
    assert(!old_calls); /* New name provider wins even for SHORT. */
    assert(strlen(name(CORE_PARAM_LONG)) > 20);
    assert(!strcmp((void *)(uintptr_t)core_param_lfo_name(16 * 60, 0), name(CORE_PARAM_LONG)));
    assert(!strcmp((void *)(uintptr_t)core_param_lfo_name(16 * 60, 1), "ATK"));
    assert(!strcmp((void *)(uintptr_t)core_param_lfo_name(16 * 60, 2), "ATK"));
    names = 0;
    assert(!strcmp(name(CORE_PARAM_LONG), native_long) && !strcmp(name(CORE_PARAM_GROUP), native_group));
    assert(!old_calls); /* LONG/GROUP cannot reach an old callback ignoring field. */
    assert(!strcmp(name(CORE_PARAM_SHORT), "OLD") && old_calls == 1);
    assert(!strcmp((void *)(uintptr_t)core_param_lfo_name(16 * 60, 2), "OLD"));
    assert(!strcmp((void *)(uintptr_t)core_param_lfo_name(17 * 60, 2), native_short));
    assert(!core_param_name(16, -1) && !core_param_value_text(17, 0));
    const char *out = (void *)(uintptr_t)core_param_value_text(16, 1);
    assert(strlen(out) == 15 && out[0] == 'x'); /* Name and format may come from different providers. */
    assert(core_param_dest_visible(107) && !core_param_dest_visible(108));
    prefer_first_visible = 1; assert(core_param_dest_visible(108)); prefer_first_visible = 0;
    static int32_t ids[] = {16, 108, 107};
    static uint32_t vec[3], shared;
    vec[0] = ptr(ids); vec[1] = vec[2] = ptr(ids + 3); shared = ptr(vec);
    core_param_lfo_filter(&shared);
    assert(ids[0] == 16 && ids[1] == 107 && vec[1] == ptr(ids + 2) && vec[2] == ptr(ids + 3));
    static uint32_t vt[11], owner;
    owner = ptr(vt); vt[10] = ptr(get); vt[8] = ptr(step);
    fw_slot_ids[17] = 107; fw_slot_ids[18] = 108; fw_slot_ids[19] = 109;
    assert(core_param_lfo_step(ptr(&owner), 52, 18 * 256, 256) == 19 * 256 && calls == 1);
    previous = 17 * 256; stuck = 1;
    assert(core_param_lfo_step(ptr(&owner), 61, 18 * 256, -256) == previous);
    active = 0; int32_t before = calls;
    assert(core_param_lfo_step(ptr(&owner), 52, 18 * 256, 256) == 18 * 256 && calls == before);
    puts("PASS original SHORT-only callback, versioned names, ordered independent providers, width fallback, marker refusal, termination and LFO IDs/relative edits");
}
