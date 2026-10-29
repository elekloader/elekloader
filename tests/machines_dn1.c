/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native objects/callbacks are fixtures; the companion logic is compiled unchanged. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mods/machines-dn1/machines.c"
#define dnm_param_name core_param_name
#define dnm_value_text core_param_value_text
#define dnm_lfo_filter core_param_lfo_filter
#define dnm_lfo_step core_param_lfo_step
#define dnm_lfo_name core_param_lfo_name
#include "../mods/core-dn1/sound.c"
#include "../mods/core-dn1/selection.c"
#include "../mods/core-dn1/param_names.c"
#include "../mods/machines-dn1/native.c"
#include "../mods/machines-dn1/pages.c"

static uint8_t kit[4 * 326 + 0x2c];
uint8_t *volatile fw_kit = kit;
volatile int32_t fw_active_track, fw_voice_pitch_processed[8], fw_syn_key;
volatile uint32_t fw_voice_pitch[8], fw_gate_off, fw_slot_ids[79];
volatile int16_t fw_voice_params[9 + 79 * 8];
const uint32_t fw_sound_change_event_vtable = 0;

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define WORD32(at, v) [at] = (v) & 255, [at + 1] = ((v) >> 8) & 255, [at + 2] = ((v) >> 16) & 255, [at + 3] = ((v) >> 24) & 255
#else
#define WORD32(at, v) [at + 3] = (v) & 255, [at + 2] = ((v) >> 8) & 255, [at + 1] = ((v) >> 16) & 255, [at] = ((v) >> 24) & 255
#endif
#define PARAM(id, slot) WORD32(id * 60 + 4, slot), WORD32(id * 60 + 12, 32767), WORD32(id * 60 + 40, 0x1234), WORD32(id * 60 + 48, 0x5678)
const uint8_t fw_params[185 * 60] __attribute__((aligned(4))) = {
    PARAM(107, 17), PARAM(108, 18), PARAM(109, 19), PARAM(110, 20),
    PARAM(111, 21), PARAM(112, 22), PARAM(113, 23), PARAM(114, 24),
    PARAM(119, 27), PARAM(120, 28), PARAM(121, 29), PARAM(122, 30),
    PARAM(123, 31), PARAM(124, 32), PARAM(125, 33), PARAM(126, 34),
    PARAM(115, 35), PARAM(116, 36), PARAM(117, 37), PARAM(118, 38),
    PARAM(127, 39), PARAM(128, 40), PARAM(129, 41), PARAM(133, 42),
    PARAM(130, 43), PARAM(131, 44), PARAM(132, 45),
};

static int32_t pitches[8], render_calls, stock_draws, custom_draws, last_page;
static int32_t draw_handled = 1;
static void render(int32_t voice, int32_t *block, const struct dnm_voice *v)
{
    render_calls++;
    pitches[voice] = v->pitch;
    for (int32_t i = 0; i < 32; i++)
        block[i] = (voice + 1) * 0x10000;
}
static int32_t custom(void *view, void *bitmap, int32_t syn, int32_t page)
{
    (void)view; (void)bitmap; (void)syn;
    custom_draws++;
    last_page = page;
    return draw_handled;
}
static void fmt(int32_t value, char *buf) { snprintf(buf, 16, "v%d", value); }
static const struct dnm_knob knobs[16] = {
    {"A", fmt, 0}, {"B", 0, 0}, {"C", 0, 0}, {0, 0, 0}, {"E", 0, 0},
};
static const int16_t defaults[16] = {1234, -1, 100, -1};
static const struct dnm_machine first = {6, "FIRST", 0, knobs, render, 0, defaults};
static const struct dnm_machine without_pages = {23, "NO PAGES", 0, knobs, render, 0, 0};
static const struct dnm_machine second = {24, "SECOND", 0, knobs, render, 0, defaults};
static const struct dnm_machine fallback = {25, "FALLBACK", 0, knobs, render, 0, defaults};
static const struct dnm_knob extended_knobs[DNM_KNOBS_ALL] = {
    [0] = {"A", fmt, 0}, [16] = {"FINE", fmt, 0},
    [20] = {"HOLE", fmt, 0}, [24] = {"DELAY", fmt, 0},
    [27] = {"PHASE", fmt, 0}, [31] = {"HOLE", fmt, 0},
};
static const int16_t extended_defaults[DNM_KNOBS_ALL] = {[0] = 100, [16] = 1234, [24] = 1024, [27] = 256};
static const struct dnm_machine extended = {26, "EXTENDED", 0, extended_knobs, render, DNM_PAGES2, extended_defaults};
/* Same knobs, short names metadata: reads on page 2 must stop at count. */
static const struct dnm_machine short_meta = {27, "SHORT META", 0, extended_knobs, render, DNM_PAGES2, 0};
const struct dnm_machine *const dn_machines[] = {&first, &without_pages, &second, &fallback, &extended, &short_meta, 0};
static const struct dnm_page page1 = {&dnm_ui_v1, 6, {1, 1}, custom};
static const struct dnm_page page2 = {&dnm_ui_v1, 24, {2, 0}, custom};
static const struct dnm_page page3 = {&dnm_ui_v1, 25, {1, 1}, 0};
const struct dnm_page *const dn_machine_pages[] = {&page1, &page2, &page3, 0};
static const char *const full_names[16] = {[0] = "Osc2 Coarse"};
static const struct dnm_names full_meta = {&dnm_names_v1, 24, DNM_KNOBS, full_names};
static const char *const extended_names[DNM_KNOBS_ALL] = {[16] = "Osc2 Fine", [24] = "Osc2 Delay"};
static const struct dnm_names extended_meta = {&dnm_names_v1, 26, DNM_KNOBS_ALL, extended_names};
static const struct dnm_names short_names_meta = {&dnm_names_v1, 27, DNM_KNOBS, full_names};
static const struct dnm_names invalid_meta = {&dnm_names_v1, 25, 15, full_names};
const struct dnm_names *const dn_machine_names[] = {&full_meta, &extended_meta, &short_names_meta, &invalid_meta, 0};

const struct core_param_override *const core_param_override[] = {&dnm_override, 0};
const struct core_param_presenter *const core_param_presenters[] = {&dnm_presenter, 0};
uint32_t core_param_name_call(uint32_t fn, int32_t id, int32_t field)
{ return (uint32_t)(uintptr_t)((const char *(*)(int32_t, int32_t))(uintptr_t)fn)(id, field); }

static uint32_t views[2][64], ui[128], brain[64], bottom[4], manager[64], event[6];
static uint32_t object[4], ref[1], ref_vtable[5];
static int32_t grid, step_mode, func, changed, writes, trigs, refreshed, sound_writes;
static int32_t latest_slot, latest_value, project_present = 1, ref_present = 1;
static uint32_t track_length = 64;
static int32_t track_present = 1, expected_slot = 17, expected_value = 1234;
static uint32_t write_mask[2];
static uint32_t ptr(const void *p)
{
    assert((uintptr_t)p <= UINT32_MAX);
    return (uint32_t)(uintptr_t)p;
}
static void sound_changed(void *r, void *e)
{
    uint32_t *ev = e;
    assert(r == ref && ev[0] == ptr(&fw_sound_change_event_vtable));
    assert(*((uint8_t *)e + 8) == 1);
    assert((int32_t)ev[1] == latest_slot);
    changed++;
}
void core_param_ui_make(void *record, int32_t id, int32_t look, void (*format)(int32_t, char *))
{
    (void)look; (void)format;
    ((uint32_t *)record)[0x14 / 4] = id;
}
void core_sound_set(int32_t track, int32_t slot, int32_t value)
{
    if (!fw_kit || track < 0 || track >= 4 || slot < 0 || slot >= 79) return;
    sound_writes++;
    latest_slot = slot;
    latest_value = value;
    FW_SOUND_SLOT(kit, track, slot) = value;
    core_sound_apply(value, track, slot);
}
void fw_sound_live(int32_t value, int32_t track, int32_t slot)
{ assert(value == latest_value && slot == latest_slot && track >= 0 && track < 4); }
uint32_t fw_project_get(void) { return project_present ? 0x100000 : 0; }
uint32_t fw_sound_ref(uint32_t owner, int32_t track)
{
    assert(owner == 0x100250 && track >= 0 && track < 4);
    return ref_present ? ptr(ref) : 0;
}
static int32_t shown_value, shown_id = 107, shown_calls;
static uint8_t shown_locked;
int32_t fw_view_param_value(void *view, int32_t id, uint8_t *locked)
{
    assert(view == views[(knob_of(id) % DNM_KNOBS) / 8]);
    assert(id == shown_id && locked);
    *locked = shown_locked;
    shown_calls++;
    return shown_value;
}
uint32_t fw_ui_get(void) { return ptr(ui); }
uint8_t fw_ui_grid_edit(void *p) { assert(p == ui); return grid; }
uint8_t fw_ui_step_edit(void *p) { assert(p == ui); return step_mode; }
uint8_t fw_key_held(uint32_t key) { assert(key == 1); return func; }
uint32_t fw_track_get(void *owner) { assert(owner == object); return track_present ? ptr(object) : 0; }
uint32_t fw_track_length(void *p) { assert(p == object); return track_length; }
uint8_t fw_track_has_note(void *p, uint32_t step) { assert(p == object); return step == 48; }
uint8_t fw_track_has_lock(void *p, uint32_t step) { assert(p == object); return step == 63; }
void fw_track_set_trig(void *p, uint32_t step, uint32_t enabled)
{
    assert(p == object && enabled && step != 48 && step != 63);
    trigs++;
}
uint8_t fw_track_set_lock(void *p, uint32_t step, uint32_t slot, uint32_t value)
{
    assert(p == object && step < 64 && slot == (uint32_t)expected_slot && value == (uint32_t)expected_value);
    write_mask[step >= 32] |= 1u << (step & 31);
    writes++;
    return 1;
}
void fw_ui_refresh(void *p, uint32_t value) { assert(p == ui && value == 1); refreshed++; }
void fw_syn1_draw(void *v, void *b) { (void)v; (void)b; stock_draws++; }
void fw_syn2_draw(void *v, void *b) { (void)v; (void)b; stock_draws++; }

static void select_machine(int32_t id) { FW_SOUND_SLOT(kit, fw_active_track, 0) = id << 8; }
static void held(int32_t step)
{
    ui[0x158 / 4] = step >= 32 ? 1u << (step & 31) : 0;
    ui[0x15c / 4] = step >= 0 && step < 32 ? 1u << step : 0;
}
static void setup(void)
{
    for (int32_t i = 0; i < 2; i++) {
        views[i][124 / 4] = 0x200000 + i * 16;
        views[i][128 / 4] = views[i][124 / 4] + 8;
        views[i][132 / 4] = views[i][128 / 4];
        views[i][144 / 4] = 1;
        views[i][0x68 / 4] = ptr(object);
    }
    brain[0x54 / 4] = brain[0x5c / 4] = ptr(bottom);
    bottom[2] = ptr(manager);
    manager[0xc0 / 4] = ptr(views[0]);
    ref[0] = ptr(ref_vtable);
    ref_vtable[4] = ptr(sound_changed);
    for (int32_t k = 0; k < DNM_KNOBS_ALL; k++)
        if (knob_id[k])
            fw_slot_ids[param_rec(knob_id[k])[1]] = knob_id[k];
    fw_syn_key = 1;
}

static void test_navigation(void)
{
    select_machine(6);
    dnm_syn1_draw(views[0], 0);
    dnm_syn2_draw(views[1], 0);
    assert(custom_draws == 2 && stock_draws == 0 && last_page == 0);
    for (int32_t i = 0; i < 2; i++)
        assert(views[i][128 / 4] == views[i][124 / 4] + 4 && views[i][144 / 4] == 0);
    assert(dnm_syn_page(brain) == 1);
    brain[0x5c / 4] = 0;
    assert(dnm_syn_page(brain) == 0);
    brain[0x5c / 4] = ptr(bottom);
    for (int32_t cycle = 0; cycle < 5; cycle++) {
        select_machine(0);
        dnm_syn1_draw(views[0], 0);
        assert(views[0][128 / 4] == views[0][124 / 4] + 8 && views[0][144 / 4] == 1);
        select_machine(6);
        dnm_syn1_draw(views[0], 0);
        assert(views[0][128 / 4] == views[0][124 / 4] + 4 && views[0][144 / 4] == 0);
    }
    for (int32_t id = 23; id <= 24; id++) {
        select_machine(id);
        int32_t old_stock = stock_draws;
        dnm_syn1_draw(views[0], 0);
        dnm_syn2_draw(views[1], 0);
        for (int32_t i = 0; i < 2; i++)
            assert(views[i][128 / 4] == views[i][124 / 4] + 8 && views[i][144 / 4] == 1);
        assert(stock_draws == old_stock + (id == 23 ? 2 : 0));
        if (id == 24)
            assert(last_page == 1);
    }
    select_machine(24);
    int32_t before = stock_draws;
    draw_handled = 0;
    dnm_syn1_draw(views[0], 0);
    assert(stock_draws == before + 1);
    draw_handled = 1;
    select_machine(25);
    int32_t old_stock = stock_draws;
    dnm_syn1_draw(views[0], 0);
    assert(stock_draws == old_stock + 1 && views[0][144 / 4] == 0);
    select_machine(99);
    assert(dnm_machine_id() == 0);
    dnm_syn1_draw(views[0], 0);
    assert(stock_draws == old_stock + 2 && views[0][144 / 4] == 1);
    puts("PASS native/custom/FM/navigation without page descriptors, two pages and renderer fallback");
}

static void test_locks(void)
{
    int32_t locked;
    select_machine(6);
    dnm_syn1_draw(views[0], 0);
    FW_SOUND_SLOT(kit, 0, 17) = 999;
    grid = 1;
    const int32_t steps[] = {0, 16, 32, 48, 63};
    for (int32_t i = 0; i < 5; i++) {
        int32_t s = steps[i];
        held(s);
        shown_value = 999; shown_locked = 0;
        assert(dnm_knob_value(0, &locked) == 999 && locked == 0);
        shown_value = 2000 + s; shown_locked = 1;
        assert(dnm_knob_value(0, &locked) == 2000 + s && locked == 1);
        func = 1;
        assert(dnm_reset(brain, 1, 0) == 1);
        func = 0;
        assert(dnm_reset(brain, 1, 0) == 0);
    }
    assert(writes == 5 && trigs == 3 && refreshed == 5 && !sound_writes);
    held(48);
    ui[0x158 / 4] |= 1u << 31;
    func = 1;
    writes = 0;
    memset(write_mask, 0, sizeof(write_mask));
    assert(dnm_reset(brain, 1, 0) == 1);
    assert(writes == 2 && write_mask[0] == 0 && write_mask[1] == ((1u << 16) | (1u << 31)));
    held(-1);
    assert(dnm_reset(brain, 1, 0) == 1 && sound_writes == 1 && changed == 1);
    assert(latest_slot == 17 && latest_value == 1234);
    func = 0;
    assert(dnm_reset(brain, 1, 3) == 1);
    assert(dnm_reset(brain, 1, 1) == 0);
    step_mode = 1; grid = 0; held(63);
    shown_value = 4321; shown_locked = 1;
    assert(dnm_knob_value(0, &locked) == 4321 && locked);
    assert(dnm_knob_value(0, 0) == 4321);
    step_mode = 0;
    int32_t calls = shown_calls;
    fw_active_track = 4;
    assert(!dnm_knob_value(0, &locked) && !locked);
    fw_active_track = 0;
    fw_kit = 0;
    assert(!dnm_knob_value(0, &locked) && !locked);
    fw_kit = kit;
    assert(shown_calls == calls);
    assert(dnm_knob_value(-1, &locked) == 0 && locked == 0);
    assert(dnm_knob_value(20, 0) == 0);
    event[3] = 1; event[4] = 1;
    func = 1;
    assert(dnm_pages_enc(brain, event) == 1);
    views[0][144 / 4] = 1;
    select_machine(24);
    assert(dnm_pages_enc(brain, event) == 1);
    puts("PASS held steps 1/17/33/49/64, multi reset, native FUNC release, STEP and base edits");
}

static void test_reset_context(void)
{
    select_machine(6); func = 1; grid = 1; held(63);
    int32_t before = writes;
    views[0][0x68 / 4] = 0;
    assert(dnm_reset(brain, 1, 0));
    views[0][0x68 / 4] = ptr(object);
    track_present = 0;
    assert(dnm_reset(brain, 1, 0));
    track_present = 1; track_length = 65;
    assert(dnm_reset(brain, 1, 0));
    track_length = 64;
    assert(writes == before);
    grid = 0; func = 0; held(-1);
    puts("PASS reset refuses missing owner/track/kit and invalid length without writes");
}

static void test_sound_and_names(void)
{
    select_machine(6);
    int32_t old = sound_writes, notices = changed;
    dnm_sound_edit(-1, 0, 6 << 8); dnm_sound_edit(4, 0, 6 << 8);
    dnm_sound_edit(0, -1, 0); dnm_sound_edit(0, 79, 0);
    assert(sound_writes == old && changed == notices);
    for (int32_t t = 0; t < 4; t++) {
        fw_active_track = t;
        machines_pick(0, 0, t, 1);
        assert(FW_SOUND_SLOT(kit, t, 0) == 6 << 8);
        assert(!strcmp(dnm_ptr(dnm_param_name(107, 0x28)), "A"));
        assert(!strcmp(dnm_ptr(dnm_param_name(107, 0x30)), "A"));
        assert(!strcmp(dnm_ptr(dnm_value_text(107, 4096)), "v4096"));
        assert(dnm_value_text(108, 4096) == 0);
        select_machine(0);
        assert(dnm_param_name(107, 0x28) == 0x1234);
        assert(dnm_param_name(107, 0x30) == 0x5678);
        assert(dnm_value_text(107, 4096) == 0);
    }
    fw_active_track = 0;
    project_present = 0;
    old = sound_writes; notices = changed;
    dnm_sound_edit(0, 0, 6 << 8);
    assert(sound_writes == old + 1 && changed == notices);
    project_present = 1; ref_present = 0;
    dnm_sound_edit(0, 0, 6 << 8);
    assert(changed == notices);
    ref_present = 1;
    puts("PASS persistent edit notification/bounds and all-track names/value/FM fallback");
}

static int32_t step_previous, step_stuck, step_calls;
static int32_t dest_get(uint32_t owner, int32_t id)
{ (void)owner; assert(id == 52 || id == 61); return step_previous; }
static int32_t dest_step(uint32_t owner, int32_t id, int32_t value, int32_t delta)
{ (void)owner; assert(id == 52 || id == 61); step_calls++; return step_stuck ? value : value + delta; }

static void test_destination_metadata(void)
{
    static int32_t ids[] = {16, 107, 110, 123, 127, 134, 135, 136, 27, 45};
    static uint32_t vector[3], shared[2];
    select_machine(24);
    assert(!strcmp(dnm_ptr(dnm_param_name(107, 0x28)), "Osc2 Coarse"));
    assert(!strcmp(dnm_ptr(dnm_param_name(107, 0x30)), "A"));
    assert(!strcmp(dnm_ptr(dnm_lfo_name(107 * 60, 0)), "Osc2 Coarse"));
    assert(!strcmp(dnm_ptr(dnm_lfo_name(107 * 60, 2)), "A"));
    vector[0] = ptr(ids); vector[1] = ptr(ids + 10); vector[2] = vector[1];
    shared[0] = ptr(vector);
    dnm_lfo_filter(shared);
    const int32_t expected[] = {16, 107, 134, 27, 45};
    assert(vector[1] == ptr(ids + 5) && vector[2] == ptr(ids + 10));
    assert(!memcmp(ids, expected, sizeof(expected)));
    assert(!strcmp(dnm_ptr(dnm_param_name(110, 0x30)), "UNUSED"));
    static uint32_t vt[11], owner[1];
    vt[8] = ptr(dest_step); vt[10] = ptr(dest_get); owner[0] = ptr(vt);
    fw_slot_ids[20] = 110; fw_slot_ids[21] = 127; fw_slot_ids[22] = 111;
    step_previous = 19 * 256;
    for (int32_t id = 52; id <= 61; id += 9) {
        assert(dnm_lfo_step(ptr(owner), id, 20 * 256, 8) == 22 * 256);
        assert(dnm_lfo_step(ptr(owner), id, 21 * 256, -8) == 19 * 256);
        step_stuck = 1;
        assert(dnm_lfo_step(ptr(owner), id, 20 * 256, 8) == step_previous);
        step_stuck = 0;
    }
    int32_t calls = step_calls;
    assert(dnm_lfo_step(0, 107, 20 * 256, 8) == 20 * 256);
    select_machine(0);
    assert(dnm_lfo_step(0, 52, 20 * 256, 8) == 20 * 256 && step_calls == calls);
    assert(dnm_lfo_visible(127) && dnm_lfo_visible(110));
    assert(dnm_lfo_name(107 * 60, 2) == 0x5678);
    puts("PASS separate long/short labels, unused SYN filtering and original IDs/FM fallback");
}


static void test_pages2_contract(void)
{
    int32_t locked, lo, hi;
    select_machine(26);
    held(-1); grid = step_mode = 0;
    FW_SOUND_SLOT(kit, 0, 35) = 2000;
    shown_id = 115; shown_value = 2000; shown_locked = 0;
    assert(dnm_knob_value(16, &locked) == 2000 && !locked);
    grid = 1; held(63);
    shown_value = 3456; shown_locked = 1;
    assert(dnm_knob_value(16, &locked) == 3456 && locked);
    grid = 0; held(-1);
    for (int32_t k = 0; k < DNM_KNOBS_ALL; k++) {
        int32_t id = knob_id[k];
        dnm_knob_range(k, &lo, &hi);
        if (!id) {
            assert(dnm_knob_value(k, &locked) == 0 && !locked);
            assert(dnm_knob(0, k) == 0 && !lo && !hi);
            continue;
        }
        FW_VOICE_PARAM(0, param_rec(id)[1]) = 4000 + k;
        assert(dnm_knob(0, k) == 4000 + k && hi == 32767);
        assert(dnm_ui(id));
        assert(dnm_lfo_visible(id) == !!extended_knobs[k].name);
    }
    assert(!strcmp(dnm_ptr(dnm_param_name(115, 0x28)), "Osc2 Fine"));
    assert(!strcmp(dnm_ptr(dnm_param_name(115, 0x30)), "FINE"));
    assert(!strcmp(dnm_ptr(dnm_value_text(115, 1234)), "v1234"));
    assert(!strcmp(dnm_ptr(dnm_param_name(133, 0x28)), "PHASE"));
    assert(!dnm_lfo_visible(116) && !dnm_lfo_visible(135));
    static int32_t ids[] = {107, 115, 116, 127, 133, 135, 27};
    static uint32_t vec[3], shared[1];
    vec[0] = ptr(ids); vec[1] = vec[2] = ptr(ids + 7); shared[0] = ptr(vec);
    dnm_lfo_filter(shared);
    const int32_t want[] = {107, 115, 127, 133, 27};
    assert(vec[1] == ptr(ids + 5) && vec[2] == ptr(ids + 7));
    assert(!memcmp(ids, want, sizeof(want)));
    select_machine(25);
    assert(!strcmp(dnm_ptr(dnm_param_name(107, 0x28)), "A"));
    select_machine(27);
    assert(!strcmp(dnm_ptr(dnm_param_name(115, 0x28)), "FINE"));
    select_machine(23); /* exactly 16 knobs and no defaults */
    assert(!dnm_lfo_visible(115));
    assert(!strcmp(dnm_ptr(dnm_param_name(115, 0x28)), "UNUSED"));
    assert(!dnm_value_text(115, 1234));
    assert(dnm_ui(115) == blank_rec[0]);
    select_machine(26);
    fw_syn_key = 1; manager[0xc0 / 4] = ptr(views[0]);
    views[0][144 / 4] = 1; event[3] = 1; event[4] = 1; func = 1;
    assert(dnm_pages_enc(brain, event) == 1 && latest_slot == 35 && latest_value == 1234);
    event[3] = 5; /* physical padding position 20 */
    int32_t before = sound_writes;
    assert(dnm_pages_enc(brain, event) == 1 && sound_writes == before);
    fw_syn_key = 2; manager[0xc0 / 4] = ptr(views[1]);
    views[1][144 / 4] = 1; event[3] = 1;
    assert(dnm_pages_enc(brain, event) == 1 && latest_slot == 39 && latest_value == 1024);
    func = 0;
    /* Upstream switch semantics: own second pages, restore when leaving,
     * retain second-page values between machines without page descriptors. */
    select_machine(0); machines_pick(0, 0, 0, 5);
    assert(FW_SOUND_SLOT(kit, 0, 35) == 1234);
    machines_pick(0, 0, 0, 1);
    assert(FW_SOUND_SLOT(kit, 0, 35) == param_rec(115)[4]);
    FW_SOUND_SLOT(kit, 0, 35) = 777;
    machines_pick(0, 0, 0, 2);
    assert(FW_SOUND_SLOT(kit, 0, 35) == 777);
    machines_pick(0, 0, 0, 5); machines_pick(0, 0, 0, 0);
    assert(FW_SOUND_SLOT(kit, 0, 35) == param_rec(115)[4]);
    puts("PASS PAGES2 32 positions, padding, count-limited names, both page resets, locks and FM restore");
}


static void test_voices(void)
{
    int32_t pcm[8 * 32], note[18] = {0};
    note[4] = 100 << 8;
    for (int32_t v = 0; v < 8; v++) {
        fw_voice_pitch[v] = (40 + v) << 16;
        fw_voice_pitch_processed[v] = ((50 + v) << 16) + 123;
        FW_VOICE_PARAM(v, 0) = 6 << 8;
        FW_VOICE_PARAM(v, 66) = 0;
        FW_VOICE_PARAM(v, 68) = 127 << 8;
        dnm_voice_on(v, v % 4, note);
    }
    dnm_render(pcm);
    assert(render_calls == 8);
    for (int32_t v = 0; v < 8; v++)
        assert(pitches[v] == fw_voice_pitch_processed[v] && pitches[v] != (int32_t)fw_voice_pitch[v]);
    FW_VOICE_PARAM(2, 0) = 0;
    for (int32_t i = 64; i < 96; i++) pcm[i] = 0x12345678;
    dnm_render(pcm);
    assert(render_calls == 15);
    for (int32_t i = 64; i < 96; i++) assert(pcm[i] == 0x12345678);
    puts("PASS eight processed pitches and intentional FM voice bypass");
}

int main(void)
{
    setup();
    FW_SOUND_SLOT(kit, 0, 17) = 876;
    int32_t locked = 99;
    assert(dnm_knob_value(0, &locked) == 876 && !locked && !shown_calls);
    test_navigation();
    test_locks();
    test_reset_context();
    test_sound_and_names();
    test_destination_metadata();
    test_pages2_contract();
    test_voices();
    return 0;
}
