/* SPDX-License-Identifier: GPL-2.0-or-later */
/* machines 0.3: the SDK alpha's machines (elekloader/sdk_alpha_machine.h) on the Digitone. A mod built by
 * sdk_alpha_build contributes each machine to the table sdk_alpha_machines. Its pages (two at most)
 * replace SYN2's pages while the track plays the machine (core_page_list): FM's envelope graphs belong to
 * page 4, not to its knobs. Each logical knob keeps its value in one SYN parameter, whichever page shows it.
 * SDK renderers replace FM through the companion's existing voice path. */
#include "dev.h"
#include "digitone-mk1/machines.h"

#define SDK_KNOBS SDK_ALPHA_KNOBS
const char sdk_alpha_abi_v1[] = "sdk alpha 1";
static struct dnm_machine machines[SDK_ALPHA_MACHINES];   /* sdk_alpha_entries' machines, in their order */
static struct dnm_knob knobs[SDK_ALPHA_MACHINES][DNM_KNOBS_ALL];
static int16_t defaults[SDK_ALPHA_MACHINES][DNM_KNOBS_ALL];
static int32_t count = -1;

int32_t dnm_sdk_raw(int32_t track, int32_t encoder);  /* machines.c: the native sound value */
int32_t dnm_sdk_param(int32_t k);                     /* machines.c: knob k's parameter id */

/* Logical knob i's place among machines.c's SYN knobs (DNM_PAGES2's 32): SYN2 A-H (0-127), then the SYN
 * parameters that hold 0-127 on a slot of their own: DTUN, FDBK, MIX, ADEL, BDEL, and two ratio offsets
 * (0-127.99, which the SDK reads as 127). A Digitone sound has no free slot for more. */
static const uint8_t sdk_pos[SDK_KNOBS] = { 8, 9, 10, 11, 12, 13, 14, 15, 5, 6, 7, 24, 28, 18, 19 };

/* The SDK pages: mod pages no stock list holds (after -1), shown by SYN2 in place of its own pages while
 * an SDK machine is the active track's. */
#define SYN2_FIRST 4                /* SYN2's stock list starts with page 4 */
#define V_CUR 144                   /* a view's place in its list (core-dn1's pages.s, every OS) */
/* The OS release's View and parameter-set layouts come from mod.json's port cflags: VIEW_METHODS,
 * VIEW_DRAW, VIEW_DRAW_PARAMETER, VIEW_CLEAR, VIEW_PARAM_SET (and its argument VIEW_PARAM_SET_ARG),
 * VIEW_DIRTY, VIEW_PARENT, SET_METHODS and SET_RESET. */
struct core_page dnm_sdk_pages[2] = {   /* claimed as page:29 and page:28 (core-dn1 5.1 has 30) */
    { 29, -1, "SDK", "SDK", { 0 }, 9, 0, 0 },
    { 28, -1, "SDK", "SDK", { 0 }, 9, 0, 1 },
};
_Static_assert(sizeof(struct core_page) == 60, "mod.json contributes dnm_sdk_pages + 60");
static const int32_t sdk_list[2] = { 29, 28 };
static const struct dnm_machine *listed;    /* the machine SYN2 shows the SDK pages for; 0: stock */
int32_t dnm_sdk_shown[4];                   /* each track's SDK page, kept across track switches */
static int32_t shown_track = -1;            /* the track dnm_sdk_shown follows now */

extern struct { void *view; int32_t first; } core_views[16];
static void (*draw_page)(void *view, void *bmp);
static void sdk_draw(void *view, void *bmp);
static uint8_t (*clear_page)(void *view);
static uint8_t sdk_clear(void *view);

/* Adapt SYN2's view for the plain grid and SDK defaults; FM gets its own method table back. */
static int page_layout(int sdk)
{
    static uint32_t *view;
    static const uint32_t *stock;
    static uint32_t methods[2 + VIEW_METHODS];
    if (!view) {
        uint32_t *syn2 = 0, *plain = 0;
        for (int i = 0; i < 16 && core_views[i].view; ++i) {
            if (core_views[i].first == SYN2_FIRST) syn2 = core_views[i].view;
            if (core_views[i].first == 1) plain = core_views[i].view;
        }
        if (!syn2 || !plain)
            return 0;
        view = syn2;
        stock = (const uint32_t *)view[0];
        const uint32_t *base = (const uint32_t *)plain[0];
        for (int i = -2; i < VIEW_METHODS; ++i)
            methods[i + 2] = stock[i];    /* include the offset-to-top and type information */
        draw_page = (void (*)(void *, void *))base[VIEW_DRAW];
        methods[2 + VIEW_DRAW] = (uint32_t)sdk_draw;
        methods[2 + VIEW_DRAW_PARAMETER] = base[VIEW_DRAW_PARAMETER];
        clear_page = (uint8_t (*)(void *))stock[VIEW_CLEAR];
        methods[2 + VIEW_CLEAR] = (uint32_t)sdk_clear;
        dnm_sdk_pages[0].view = dnm_sdk_pages[1].view = syn2;
    }
    view[0] = (uint32_t)(sdk ? methods + 2 : stock);
    return 1;
}

static const int16_t *render_params;
static void render(int32_t voice, int32_t *block, const struct dnm_voice *v);

/* Reentrant initialization writes the same data; publish count only after every machine is ready. */
static void init(void)
{
    if (count >= 0)
        return;
    sdk_alpha_init();
    for (int32_t n = 0; n < sdk_alpha_count; ++n) {
        const struct sdk_alpha_entry *e = &sdk_alpha_entries[n];
        machines[n].id = e->machine->id;
        machines[n].name = e->machine->name;
        machines[n].knobs = knobs[n];
        machines[n].flags = DNM_PAGES2;
        machines[n].render = e->machine->render ? render : 0;
        for (int k = 0; k < DNM_KNOBS_ALL; ++k)
            defaults[n][k] = -1;
        for (uint32_t i = 0; i < e->count; ++i) {
            knobs[n][sdk_pos[i]].name = e->machine->knobs[i].label;
            knobs[n][sdk_pos[i]].fmt = sdk_alpha_value_texts[i];
            defaults[n][sdk_pos[i]] = sdk_alpha_default(&e->machine->knobs[i]);
        }
        machines[n].defaults = defaults[n];
    }
    __asm__ volatile ("" ::: "memory");
    count = sdk_alpha_count;
}

/* The SDK machine behind m, one of machines[], or 0. */
static const struct sdk_alpha_entry *entry_of(const struct dnm_machine *m)
{
    return m >= machines && m < machines + count ? &sdk_alpha_entries[m - machines] : 0;
}

/* The page SYN2 shows of the listed machine's (0 when it shows none of theirs). */
static int32_t shown_page(void)
{
    const uint8_t *view = dnm_sdk_pages[0].view;
    int32_t page = listed && view ? *(const int32_t *)(view + V_CUR) : 0;
    return listed && page > 0 && (uint32_t)page < entry_of(listed)->pages ? page : 0;
}

/* The active track's SDK machine, or 0: the one whose knobs' value texts the UI records show. */
const struct sdk_alpha_entry *dnm_sdk_active(void)
{
    uint32_t t = fw_active_track;
    return t < 4 && fw_kit ? sdk_alpha_find(FW_SOUND_SLOT(fw_kit, t, 0) >> 8) : 0;   /* slot 0: the machine */
}

/* ev_voice_on (machines.c), the render: a mono machine's track sounds on one voice, whichever the stock
 * gives each note. The track's voice before, while the track still has it, hands its state over and goes
 * silent: -> that voice, whose envelope machines.c moves, or -1. */
static int8_t mono_voice[4] = { -1, -1, -1, -1 };
int32_t dnm_sdk_mono_on(int32_t voice, int32_t track)
{
    if ((uint32_t)track >= 4)
        return -1;
    const struct sdk_alpha_entry *e = sdk_alpha_find(dev_track_machine(track));
    int32_t from = mono_voice[track];
    mono_voice[track] = e && e->machine->play != ELEK_POLY ? voice : -1;
    if (mono_voice[track] < 0 || from < 0 || from == voice || fw_voice_track[from] != track)
        return -1;
    sdk_alpha_swap(from, voice);
    return from;
}

/* SDK machine i, or 0 past the last. */
const struct dnm_machine *dnm_sdk_machine(int32_t i)
{
    init();
    return i >= 0 && i < count ? &machines[i] : 0;
}

static int dirty;

/* .present with the active track's values (sdk_alpha_present skips unchanged ones unless force). */
static void snapshot(const struct dnm_machine *m, int32_t page, int force)
{
    uint32_t track = fw_active_track;
    const struct sdk_alpha_entry *e;
    int32_t raw[SDK_KNOBS];
    init();
    if (!(e = entry_of(m)) || track >= 4 || (e->count && !fw_kit))
        return;
    for (uint32_t id = 0; id < e->count; ++id)
        raw[id] = dnm_sdk_raw(track, sdk_pos[id]);
    dirty |= sdk_alpha_present(e, track, page, raw, force);
}

/* The SYN controls are built (force) or redrawn with their page's presentation. */
void dnm_sdk_present(const struct dnm_machine *m, int force)
{
    snapshot(m, shown_page(), force);
}

/* The id of machine m's knob that SYN knob k (machines.c's 0-31) holds, or -1. */
static int32_t sdk_knob(const struct dnm_machine *m, int32_t k)
{
    const struct sdk_alpha_entry *e = entry_of(m);
    for (uint32_t id = 0; e && id < e->count; ++id)
        if (sdk_pos[id] == k)
            return id;
    return -1;
}

const char *dnm_sdk_label(const struct dnm_machine *m, int32_t k)
{
    int32_t id = sdk_knob(m, k);
    if (id < 0)
        return 0;
    dnm_sdk_present(m, 0);
    return sdk_alpha_visibility[id] ? sdk_alpha_labels[id] : "";
}

const char *dnm_sdk_lfo_name(const struct dnm_machine *m, int32_t k, int32_t field)
{
    int32_t id = sdk_knob(m, k);
    if (id < 0)
        return 0;
    snapshot(m, ELEK_UI_NO_PAGE, 0);
    return field == 0x2c ? entry_of(m)->machine->short_name : sdk_alpha_labels[id];
}

static int32_t dnm_sdk_hidden(const struct dnm_machine *m, int32_t k)
{
    int32_t id = sdk_knob(m, k);
    if (id < 0)
        return 0;
    dnm_sdk_present(m, 0);
    return !sdk_alpha_visibility[id];
}

/* The id of the knob at encoder k of the page shown, or -1. */
static int32_t shown_knob(int32_t k)
{
    const struct sdk_alpha_entry *e = listed ? entry_of(listed) : 0;
    return e ? e->encoder[shown_page()][k] : -1;
}

int32_t dnm_sdk_enc(void *brain, const int32_t *event)
{
    int32_t encoder = event[3] - 1, id;
    if (!listed || encoder < 0 || encoder >= 8 || !core_page_shown(brain, &dnm_sdk_pages[shown_page()]) ||
        (id = shown_knob(encoder)) < 0)
        return 0;
    return dnm_sdk_hidden(listed, sdk_pos[id]);
}

/* Hide only while painting: the native clipboard still sees every declared parameter. */
static void sdk_draw(void *view, void *bmp)
{
    struct core_page *page = &dnm_sdk_pages[shown_page()];
    int32_t ids[8];
    if (listed)
        dnm_sdk_present(listed, 0);
    for (int k = 0; k < 8; ++k) {
        int32_t id = shown_knob(k);
        ids[k] = page->ids[k];
        if (id >= 0 && !sdk_alpha_visibility[id])
            page->ids[k] = 0;
    }
    draw_page(view, bmp);
    for (int k = 0; k < 8; ++k) {
        int32_t id = shown_knob(k);
        const struct elek_glyph *glyph = id >= 0 ? sdk_alpha_glyphs[id] : 0;
        if (page->ids[k] && glyph)
            sdk_alpha_blit(bmp, 25 + 26 * (k % 4), 34 - 27 * (k / 4), glyph);
    }
    for (int k = 0; k < 8; ++k)
        page->ids[k] = ids[k];
}

static void (*reset_parameter)(void *set, int32_t id);

static int32_t sdk_default_of(const struct dnm_machine *m, int32_t id)
{
    for (uint32_t i = 0; m && i < entry_of(m)->count; ++i)
        if (dnm_sdk_param(sdk_pos[i]) == id)
            return defaults[m - machines][sdk_pos[i]];
    return -1;
}

static void sdk_reset(void *set, int32_t id)
{
    int32_t value = sdk_default_of(listed, id);
    if (value >= 0) {
        int32_t slot = ((const int32_t *)fw_params)[15 * id + 1];
        core_sound_set(fw_active_track, slot, value);
    } else {
        reset_parameter(set, id);
    }
}

/* Keep native clear/undo and its clipboard; only the SDK knobs' reset values differ from FM. */
static uint8_t sdk_clear(void *view)
{
    const uint32_t *methods = *(const uint32_t **)view;
    uint32_t *set = (uint32_t *)((uint32_t (*)(void *, int32_t, int32_t))methods[VIEW_PARAM_SET])(view, VIEW_PARAM_SET_ARG, -1);
    const uint32_t *stock = (const uint32_t *)set[0];
    uint32_t reset[2 + SET_METHODS]; /* the sound parameter-set's method table, including RTTI */
    for (int i = -2; i < SET_METHODS; ++i)
        reset[i + 2] = stock[i];
    reset_parameter = (void (*)(void *, int32_t))stock[SET_RESET];
    reset[2 + SET_RESET] = (uint32_t)sdk_reset;
    set[0] = (uint32_t)(reset + 2);
    uint8_t result = clear_page(view);
    set[0] = (uint32_t)stock;
    return result;
}

/* ev_tick (machines.c), the UI task, with the active track's machine (or 0): SYN2's list follows it, and
 * .present learns of a changed value (the firmware keeps the SYN records it was given). */
void dnm_sdk_tick(const struct dnm_machine *m, void *ctrl)
{
    const struct dnm_machine *want = 0;
    init();
    if (m >= machines && m < machines + count)
        want = m;
    if (want != listed) {
        int32_t pages = 0;
        if (want) {
            const struct sdk_alpha_entry *e = entry_of(want);
            pages = e->pages;
            for (int p = 0; p < pages; ++p) {
                dnm_sdk_pages[p].short_title = e->machine->short_name;
                dnm_sdk_pages[p].title = e->title[p];
                for (int k = 0; k < 8; ++k) {
                    int32_t id = e->encoder[p][k];
                    dnm_sdk_pages[p].ids[k] = id >= 0 ? dnm_sdk_param(sdk_pos[id]) : 0;
                }
            }
        }
        if (page_layout(want != 0) && core_page_list(SYN2_FIRST, want ? sdk_list : 0, pages)) {
            listed = want;
            sdk_alpha_seen = 0;
            shown_track = -1;
            ((uint8_t *)ctrl)[0x20] = 1;    /* redraw */
        }
    }
    if (listed && dnm_sdk_pages[0].view) {  /* each track keeps its page, as on the Digitakt */
        int32_t t = fw_active_track, *cur = (int32_t *)((uint8_t *)dnm_sdk_pages[0].view + V_CUR);
        if (t >= 0 && t < 4 && t != shown_track) {
            *cur = dnm_sdk_shown[t] < (int32_t)entry_of(listed)->pages ? dnm_sdk_shown[t] : 0;
            shown_track = t;
            ((uint8_t *)ctrl)[0x20] = 1;
        } else if (t == shown_track) {
            dnm_sdk_shown[t] = *cur;
        }
    }
    if (m)
        snapshot(m, sdk_alpha_seen_page, 0);
    if (dirty) {
        if (listed && dnm_sdk_pages[0].view) {
            uint8_t *view = dnm_sdk_pages[0].view;
            view[VIEW_DIRTY] = 1;            /* native View::dirty, without resetting value timers */
            uint8_t *parent = *(uint8_t **)(view + VIEW_PARENT);
            if (parent) parent[0x20] = 1;
        }
        ((uint8_t *)ctrl)[0x20] = 1;    /* redraw without resetting the temporary value display */
        dirty = 0;
    }
}

/* The native controller/LFO stage edits these words after this event and before render(); voices are
 * followed even while the native companion skips an inactive or non-SDK one. */
void dnm_sdk_params(int16_t *words)
{
    render_params = words;
    for (int32_t v = 0; v < 8; ++v)
        sdk_alpha_select(v, FW_VOICE_PARAM(v, 0) >> 8);
}

static void render(int32_t voice, int32_t *block, const struct dnm_voice *v)
{
    int32_t id = FW_VOICE_PARAM(voice, 0) >> 8;
    const struct sdk_alpha_entry *e = sdk_alpha_find(id);
    uint32_t track = fw_voice_track[voice];
    if (!e || track >= 4) return;
    sdk_alpha_select(voice, id);
    int32_t raw[SDK_KNOBS];
    for (uint32_t k = 0; k < e->count; ++k) {
        int32_t slot = ((const int32_t *)fw_params)[15 * dnm_sdk_param(sdk_pos[k]) + 1];
        raw[k] = render_params ? render_params[79 * voice + slot] : dnm_knob(voice, sdk_pos[k]);
    }
    struct elek_voice sv = { .track = track, .pitch = sdk_alpha_pitch(track, id, v->pitch), .velocity = v->velocity };
    sdk_alpha_voice(e, voice, &sv, raw, v->fresh, v->idle, block);
    for (int k = 0; k < ELEK_VOICE_FRAMES; ++k)
        block[k] >>= 4;              /* SDK Q31 full scale to DNM_PEAK, before native AMP/filter. */
}
