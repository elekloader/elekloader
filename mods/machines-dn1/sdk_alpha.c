/* SPDX-License-Identifier: GPL-2.0-or-later */
/* machines 0.3: the SDK alpha's machines (elekloader/sdk_alpha_machine.h) on the Digitone. A mod built by
 * sdk_alpha_build contributes its registration to the table sdk_alpha_machines. Its pages (two at most)
 * replace SYN2's pages while the track plays the machine (core_page_list): FM's envelope graphs belong to
 * page 4, not to its knobs. Each logical knob keeps its value in one SYN parameter, whichever page shows it.
 * SDK renderers replace FM through the companion's existing voice path. */
#include "digitone-mk1/core3.h"
#include "digitone-mk1/machines.h"
#include "elekloader/sdk_alpha_machine.h"

extern const struct sdk_alpha_registration *const sdk_alpha_machines[];   /* the table, ending in 0 */

#define SDK_MACHINES 16
#define SDK_KNOBS 15                /* logical knobs 0-14, as on the Digitakt */
const char sdk_alpha_dt1_ui_v1[] = "sdk alpha 1";
static struct dnm_machine machines[SDK_MACHINES];
static const struct elek_machine *declared[SDK_MACHINES];
static const struct elek_ui_knob *by_id[SDK_MACHINES][SDK_KNOBS];   /* each machine's knobs, by logical id */
static uint8_t knob_count[SDK_MACHINES];
static struct dnm_knob knobs[SDK_MACHINES][DNM_KNOBS_ALL];
static int16_t defaults[SDK_MACHINES][DNM_KNOBS_ALL];
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
#define V_CUR 144                   /* a view's place in its list (core-dn1's pages.s) */
struct core_page dnm_sdk_pages[2] = {   /* claimed as page:30 and page:29 */
    { 30, -1, "SDK", "SDK", { 0 }, 9, 0, 0 },
    { 29, -1, "SDK", "SDK", { 0 }, 9, 0, 1 },
};
_Static_assert(sizeof(struct core_page) == 60, "mod.json contributes dnm_sdk_pages + 60");
static const int32_t sdk_list[2] = { 30, 29 };
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
    enum { METHODS = 53, DRAW = 4, DRAW_PARAMETER = 38, CLEAR = 48 }; /* OS 1.44 */
    static uint32_t *view;
    static const uint32_t *stock;
    static uint32_t methods[2 + METHODS];
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
        for (int i = -2; i < METHODS; ++i)
            methods[i + 2] = stock[i];    /* include the offset-to-top and type information */
        draw_page = (void (*)(void *, void *))base[DRAW];
        methods[2 + DRAW] = (uint32_t)sdk_draw;
        methods[2 + DRAW_PARAMETER] = base[DRAW_PARAMETER];
        clear_page = (uint8_t (*)(void *))stock[CLEAR];
        methods[2 + CLEAR] = (uint32_t)sdk_clear;
        dnm_sdk_pages[0].view = dnm_sdk_pages[1].view = syn2;
    }
    view[0] = (uint32_t)(sdk ? methods + 2 : stock);
    return 1;
}

/* A knob's value text: the number, 0-127, whichever parameter holds it. */
static void sdk_value(int32_t value, char *buf)
{
    int32_t v = value >> 8, n = 0;
    if (v >= 100) buf[n++] = '0' + v / 100;
    if (v >= 10) buf[n++] = '0' + v / 10 % 10;
    buf[n++] = '0' + v % 10;
    buf[n] = 0;
}

/* Mapped controls use the same rounding and inverse defaults as the Digitakt adapter. */
static int32_t map(const struct elek_ui_knob *knob, int32_t raw)
{
    int32_t min = knob->min.supplied ? knob->min.value : 0;
    int32_t max = knob->max.supplied ? knob->max.value : 127;
    uint32_t span = (uint32_t)max - (uint32_t)min;
    uint32_t offset = (span / ELEK_UI_RAW_MAX) * (uint32_t)raw +
        ((span % ELEK_UI_RAW_MAX) * (uint32_t)raw + ELEK_UI_RAW_MAX / 2) / ELEK_UI_RAW_MAX;
    return (int32_t)((uint32_t)min + offset);
}

static int32_t default_raw(const struct elek_ui_knob *knob)
{
    if (!knob->def.supplied) return 0;
    int32_t lo = 0, hi = ELEK_UI_RAW_MAX;
    while (lo < hi) {
        int32_t mid = (lo + hi) / 2;
        if (map(knob, mid) < knob->def.value) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

struct sdk_shared {
    const struct elek_machine *machine;
    uint32_t data[ELEK_SHARED_MAX / 4];
};
static struct sdk_shared shared[4][2];
static volatile uint32_t published[4];
static int32_t state_machine[8];
static uint8_t pending_start[8];
static uint32_t voice_state[8][ELEK_VOICE_STATE_MAX / 4];
static const int16_t *render_params;
static void render(int32_t voice, int32_t *block, const struct dnm_voice *v);

/* Reentrant initialization writes the same data; publish count only after every machine is ready. */
static void init(void)
{
    const struct sdk_alpha_registration *const *p;
    int32_t n = 0;
    if (count >= 0)
        return;
    for (p = sdk_alpha_machines; *p && n < SDK_MACHINES; p++, n++) {
        const struct elek_machine *e = (*p)->ui->machine;
        machines[n].id = (*p)->id;
        machines[n].name = (*p)->name;
        machines[n].knobs = knobs[n];
        machines[n].flags = DNM_PAGES2;
        machines[n].render = e->render ? render : 0;
        declared[n] = e;
        for (int k = 0; k < DNM_KNOBS_ALL; ++k)
            defaults[n][k] = -1;
        for (uint32_t page = 0; page < e->page_count && page < 2; ++page)
            for (int k = 0; k < 8; ++k) {
                const struct elek_ui_knob *knob = e->pages[page].knob[k];
                if (knob && knob->id < SDK_KNOBS)
                    by_id[n][knob->id] = knob;
            }
        uint32_t nk = 0;
        while (nk < SDK_KNOBS && by_id[n][nk]) ++nk;
        knob_count[n] = nk;
        for (int i = 0; i < knob_count[n]; ++i) {
            knobs[n][sdk_pos[i]].name = by_id[n][i]->label;
            knobs[n][sdk_pos[i]].fmt = sdk_value;
            defaults[n][sdk_pos[i]] = default_raw(by_id[n][i]);
        }
        if (e->page_count)
            machines[n].defaults = defaults[n];
    }
    __asm__ volatile ("" ::: "memory");
    count = n;
}

/* The page SYN2 shows of the listed machine's (0 when it shows none of theirs). */
static int32_t shown_page(void)
{
    const uint8_t *view = dnm_sdk_pages[0].view;
    int32_t page = listed && view ? *(const int32_t *)(view + V_CUR) : 0;
    return listed && page > 0 && (uint32_t)page < declared[listed - machines]->page_count ? page : 0;
}

/* SDK machine i, or 0 past the last. */
const struct dnm_machine *dnm_sdk_machine(int32_t i)
{
    init();
    return i >= 0 && i < count ? &machines[i] : 0;
}

/* The last snapshot .present saw: unchanged machine, track, page and values are skipped. */
static const struct dnm_machine *seen;
static int32_t seen_track = -1, seen_page = ELEK_UI_NO_PAGE, seen_raw[SDK_KNOBS];
static char dnm_sdk_labels[SDK_KNOBS][ELEK_UI_LABEL_MAX + 1];
static uint8_t dnm_sdk_visibility[SDK_KNOBS];
static const struct elek_ui_glyph *dnm_sdk_glyphs[SDK_KNOBS];
static int dirty;

static void snapshot_track(const struct dnm_machine *m, int32_t page, int force,
                           uint32_t track, int update_ui)
{
    const struct elek_machine *e;
    struct elek_ui_context ctx = { ELEK_UI_NO_PAGE, 0, 0, 0, track };
    struct elek_ui_presentation out = { 0 };
    struct elek_ui_value values[SDK_KNOBS];
    const struct elek_ui_value *input[SDK_KNOBS];        /* 0..n-1 filled: no memset in the build */
    struct elek_ui_knob_presentation presentation[SDK_KNOBS];
    struct elek_ui_knob_presentation *output[SDK_KNOBS];
    int32_t i, n;
    init();
    if (m < machines || m >= machines + count || track >= 4)
        return;
    i = m - machines;
    e = declared[i];
    n = knob_count[i];
    if (e->page_count) {
        if (!fw_kit || ctx.track >= 4)
            return;
        for (uint32_t id = 0; id < (uint32_t)n; ++id) {
            const struct elek_ui_knob *knob = by_id[i][id];
            int32_t raw = dnm_sdk_raw(ctx.track, sdk_pos[id]);
            if (raw < 0) raw = 0;
            if (raw > ELEK_UI_RAW_MAX) raw = ELEK_UI_RAW_MAX;
            values[id].raw_value = raw;
            values[id].value = map(knob, raw);
            input[id] = &values[id];
            presentation[id].label = knob->label;
            presentation[id].visible = 1;
            presentation[id].glyph = knob->glyph;
            output[id] = &presentation[id];
            ++ctx.knob_count;
        }
        ctx.page = page;
        if (update_ui && !force && m == seen && ctx.track == (uint32_t)seen_track && ctx.page == seen_page) {
            int k = 0;
            while (k < n && input[k]->raw_value == seen_raw[k])
                ++k;
            if (k == n)
                return;
        }
        if (update_ui)
            for (int k = 0; k < n; ++k) seen_raw[k] = input[k]->raw_value;
        ctx.page_count = e->page_count;
        ctx.knob = ctx.knob_count ? input : 0;
        out.knob_count = ctx.knob_count;
        out.knob = ctx.knob_count ? output : 0;
    } else if (update_ui && !force && m == seen && ctx.track == (uint32_t)seen_track) {
        return;                     /* an empty machine's context changes only with its track */
    }
    uint32_t bank = published[track], next = bank ^ 1;
    struct sdk_shared *next_shared = &shared[track][next];
    if (e->shared) {
        for (uint32_t k = 0; k < ELEK_SHARED_MAX / 4; ++k)
            next_shared->data[k] = shared[track][bank].machine == e ? shared[track][bank].data[k] : 0;
        out.shared = next_shared->data;
    }
    if (e->present)
        e->present(&ctx, &out);
    if (e->shared) {
        next_shared->machine = e;
        __asm__ volatile ("" ::: "memory");
        published[track] = next;
    }
    if (!update_ui) return;
    seen = m;
    seen_track = ctx.track;
    seen_page = ctx.page;
    for (uint32_t id = 0; e->page_count && id < (uint32_t)n; ++id) {
        const struct elek_ui_knob *knob = by_id[i][id];
        const char *label = presentation[id].label;
        if (!label) label = knob->label;
        uint32_t n = 0;
        for (; n < ELEK_UI_LABEL_MAX && label[n]; ++n) {
            unsigned char c = label[n];
            char next = c >= 32 && c <= 126 ? c : '?';
            dirty |= dnm_sdk_labels[id][n] != next;
            dnm_sdk_labels[id][n] = next;
        }
        dirty |= dnm_sdk_labels[id][n] != 0;
        dnm_sdk_labels[id][n] = 0;
        uint8_t visible = presentation[id].visible != 0;
        dirty |= dnm_sdk_visibility[id] != visible;
        dnm_sdk_visibility[id] = visible;
        dirty |= dnm_sdk_glyphs[id] != presentation[id].glyph;
        dnm_sdk_glyphs[id] = presentation[id].glyph;
    }
}

static void snapshot(const struct dnm_machine *m, int32_t page, int force)
{
    snapshot_track(m, page, force, fw_active_track, 1);
}

/* The SYN controls are built (force) or redrawn with their page's presentation. */
void dnm_sdk_present(const struct dnm_machine *m, int force)
{
    snapshot(m, shown_page(), force);
}

/* The knob of machine m that SYN knob k (machines.c's 0-31) holds, or 0. */
static const struct elek_ui_knob *sdk_knob(const struct dnm_machine *m, int32_t k)
{
    if (m < machines || m >= machines + count)
        return 0;
    for (int id = 0; id < knob_count[m - machines]; ++id)
        if (sdk_pos[id] == k)
            return by_id[m - machines][id];
    return 0;
}

const char *dnm_sdk_label(const struct dnm_machine *m, int32_t k)
{
    const struct elek_ui_knob *knob = sdk_knob(m, k);
    if (!knob)
        return 0;
    dnm_sdk_present(m, 0);
    return dnm_sdk_visibility[knob->id] ? dnm_sdk_labels[knob->id] : "";
}

const char *dnm_sdk_lfo_name(const struct dnm_machine *m, int32_t k, int32_t field)
{
    const struct elek_ui_knob *knob = sdk_knob(m, k);
    if (!knob)
        return 0;
    snapshot(m, ELEK_UI_NO_PAGE, 0);
    return field == 0x2c ? declared[m - machines]->short_name : dnm_sdk_labels[knob->id];
}

static int32_t dnm_sdk_hidden(const struct dnm_machine *m, int32_t k)
{
    const struct elek_ui_knob *knob = sdk_knob(m, k);
    if (!knob)
        return 0;
    dnm_sdk_present(m, 0);
    return !dnm_sdk_visibility[knob->id];
}

/* The knob at encoder k of the page shown, or 0. */
static const struct elek_ui_knob *shown_knob(int32_t k)
{
    const struct elek_machine *e = listed ? declared[listed - machines] : 0;
    return e && e->page_count ? e->pages[shown_page()].knob[k] : 0;
}

int32_t dnm_sdk_enc(void *brain, const int32_t *event)
{
    int32_t encoder = event[3] - 1;
    const struct elek_ui_knob *knob;
    if (!listed || encoder < 0 || encoder >= 8 || !core_page_shown(brain, &dnm_sdk_pages[shown_page()]) ||
        !(knob = shown_knob(encoder)))
        return 0;
    return dnm_sdk_hidden(listed, sdk_pos[knob->id]);
}

/* Hide only while painting: the native clipboard still sees every declared parameter. */
static void sdk_draw(void *view, void *bmp)
{
    struct core_page *page = &dnm_sdk_pages[shown_page()];
    int32_t ids[8];
    if (listed)
        dnm_sdk_present(listed, 0);
    for (int k = 0; k < 8; ++k) {
        const struct elek_ui_knob *knob = shown_knob(k);
        ids[k] = page->ids[k];
        if (knob && !dnm_sdk_visibility[knob->id])
            page->ids[k] = 0;
    }
    draw_page(view, bmp);
    for (int k = 0; k < 8; ++k) {
        const struct elek_ui_knob *knob = shown_knob(k);
        const struct elek_ui_glyph *glyph = knob ? dnm_sdk_glyphs[knob->id] : 0;
        if (!page->ids[k] || !glyph)
            continue;
        int32_t x = 25 + 26 * (k % 4), y = 34 - 27 * (k / 4);
        fw_fillrect(bmp, x, y, x + 16, y + 16, 0);
        for (int row = 0; row < 16; ++row)
            for (int col = 0; col < 16; ++col)
                if (glyph->rows[row] & (0x8000u >> col))
                    fw_fillrect(bmp, x + col, y + 16 - row, x + col, y + 16 - row, 1);
    }
    for (int k = 0; k < 8; ++k)
        page->ids[k] = ids[k];
}

static void (*reset_parameter)(void *set, int32_t id);

static int32_t sdk_default_of(const struct dnm_machine *m, int32_t id)
{
    for (int i = 0; m && i < knob_count[m - machines]; ++i)
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
    uint32_t *set = (uint32_t *)((uint32_t (*)(void *, int32_t, int32_t))methods[42])(view, 119, -1);
    const uint32_t *stock = (const uint32_t *)set[0];
    uint32_t reset[2 + 30];          /* OS 1.44's sound parameter-set table, including RTTI */
    for (int i = -2; i < 30; ++i)
        reset[i + 2] = stock[i];
    reset_parameter = (void (*)(void *, int32_t))stock[4];
    reset[2 + 4] = (uint32_t)sdk_reset;
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
            const struct elek_machine *e = declared[want - machines];
            pages = e->page_count ? e->page_count : 1;
            for (int p = 0; p < pages; ++p) {
                dnm_sdk_pages[p].short_title = e->short_name;
                dnm_sdk_pages[p].title = e->page_count ? e->pages[p].label : e->name;
                for (int k = 0; k < 8; ++k) {
                    const struct elek_ui_knob *knob = e->page_count ? e->pages[p].knob[k] : 0;
                    dnm_sdk_pages[p].ids[k] = knob ? dnm_sdk_param(sdk_pos[knob->id]) : 0;
                }
            }
        }
        if (page_layout(want != 0) && core_page_list(SYN2_FIRST, want ? sdk_list : 0, pages)) {
            listed = want;
            seen = 0;
            shown_track = -1;
            ((uint8_t *)ctrl)[0x20] = 1;    /* redraw */
        }
    }
    if (listed && dnm_sdk_pages[0].view) {  /* each track keeps its page, as on the Digitakt */
        int32_t t = fw_active_track, *cur = (int32_t *)((uint8_t *)dnm_sdk_pages[0].view + V_CUR);
        if (t >= 0 && t < 4 && t != shown_track) {
            *cur = dnm_sdk_shown[t] < (int32_t)declared[listed - machines]->page_count ? dnm_sdk_shown[t] : 0;
            shown_track = t;
            ((uint8_t *)ctrl)[0x20] = 1;
        } else if (t == shown_track) {
            dnm_sdk_shown[t] = *cur;
        }
    }
    if (fw_kit)
        for (uint32_t t = 0; t < 4; ++t)
            for (int32_t i = 0; i < count; ++i)
                if (machines[i].render && declared[i]->shared &&
                    machines[i].id == (FW_SOUND_SLOT(fw_kit, t, 0) >> 8))
                    snapshot_track(&machines[i], ELEK_UI_NO_PAGE, 1, t, 0);
    if (m)
        snapshot(m, seen_page, 0);
    if (dirty) {
        if (listed && dnm_sdk_pages[0].view) {
            uint8_t *view = dnm_sdk_pages[0].view;
            view[0x14] = 1;                  /* native View::dirty, without resetting value timers */
            uint8_t *parent = *(uint8_t **)(view + 0x2c);
            if (parent) parent[0x20] = 1;
        }
        ((uint8_t *)ctrl)[0x20] = 1;    /* redraw without resetting the temporary value display */
        dirty = 0;
    }
}

/* Track machine changes even while the native companion skips an inactive/non-SDK voice. */
static void select_voice(int32_t voice, int32_t id)
{
    if (state_machine[voice] == id) return;
    for (uint32_t k = 0; k < ELEK_VOICE_STATE_MAX / 4; ++k) voice_state[voice][k] = 0;
    state_machine[voice] = id;
    pending_start[voice] = 0;
}

/* The native controller/LFO stage edits these words after this event and before render(). */
void dnm_sdk_params(int16_t *words)
{
    render_params = words;
    for (int32_t v = 0; v < 8; ++v)
        select_voice(v, FW_VOICE_PARAM(v, 0) >> 8);
}

static void render(int32_t voice, int32_t *block, const struct dnm_voice *v)
{
    int32_t id = FW_VOICE_PARAM(voice, 0) >> 8, i = 0;
    while (i < count && machines[i].id != id) ++i;
    uint32_t track = fw_voice_track[voice];
    if (i == count || track >= 4) return;
    const struct elek_machine *e = declared[i];
    select_voice(voice, id);
    if (v->fresh) pending_start[voice] = 1;
    const struct sdk_shared *current = &shared[track][published[track]];
    __asm__ volatile ("" ::: "memory");
    if (e->shared && current->machine != e) {
        for (int k = 0; k < ELEK_VOICE_FRAMES; ++k) block[k] = 0;
        return;
    }
    int32_t input[SDK_KNOBS];
    for (int k = 0; k < knob_count[i]; ++k) {
        int32_t slot = ((const int32_t *)fw_params)[15 * dnm_sdk_param(sdk_pos[k]) + 1];
        int32_t raw = render_params ? render_params[79 * voice + slot] : dnm_knob(voice, sdk_pos[k]);
        if (raw < 0) raw = 0;
        if (raw > ELEK_UI_RAW_MAX) raw = ELEK_UI_RAW_MAX;
        input[k] = map(by_id[i][k], raw);
    }
    const struct elek_audio_context ctx = {
        .knob_count = knob_count[i], .knob = knob_count[i] ? input : 0,
        .shared = e->shared ? current->data : 0,
    };
    const struct elek_voice sv = {
        .slot = voice, .track = track, .pitch = v->pitch, .velocity = v->velocity,
        .flags = pending_start[voice] ? ELEK_VOICE_START : 0, .gate = v->gate != 0,
        .state = e->voice_state ? voice_state[voice] : 0,
    };
    e->render(&ctx, &sv, block);
    pending_start[voice] = 0;
    for (int k = 0; k < ELEK_VOICE_FRAMES; ++k)
        block[k] >>= 4;              /* SDK Q31 full scale to DNM_PEAK, before native AMP/filter. */
}
