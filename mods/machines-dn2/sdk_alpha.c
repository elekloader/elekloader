/* SPDX-License-Identifier: GPL-2.0-or-later */
/* machines-dn2: the SDK alpha's machines (elekloader/sdk_alpha_machine.h) on the Digitone II. A mod
 * built by sdk_alpha_build contributes each machine to the table sdk_alpha_machines. Each machine is
 * a synth in MACHINE SEL, which its render's SHARC kernel plays (FM TONE without a render), with its
 * pages (two at most) as SYN's pages. Logical knob i keeps its value in sound slot 25 + i, whichever
 * page shows it; every SDK machine shares the parameter rows ROWS + SDK_ROW + i, labelled for the
 * active track's machine and taking its defaults (dn2_sdk_default). */
#include "digitone-mk2/core.h"
#include "mk2/machines.h"
#include "dev.h"
#include "dn2.h"

const char sdk_alpha_abi_v1[] = "sdk alpha 1";
static struct mk2_synth synths[SDK_ALPHA_MACHINES];  /* sdk_alpha_entries' machines, in their order */
static struct descriptor pages[SDK_ALPHA_MACHINES][2];
static struct string_rep titles[SDK_ALPHA_MACHINES][2][2];
static int32_t count = -1;
#define declared(i) (sdk_alpha_entries[i].machine)

static void init(void)
{
    if (count >= 0)
        return;
    sdk_alpha_init();
    for (int32_t n = 0; n < sdk_alpha_count; ++n) {
        synths[n].id = declared(n)->id;
        synths[n].name = declared(n)->name;
        synths[n].label = declared(n)->short_name;
    }
    count = sdk_alpha_count;
}

const struct mk2_synth *dn2_sdk_synth(unsigned i)
{
    init();
    return (int32_t)i < count ? &synths[i] : 0;
}

/* The SDK machine with the type, or -1. */
static int32_t machine_of(uint32_t type)
{
    const struct sdk_alpha_entry *e = sdk_alpha_find((int32_t)type);
    return e ? e - sdk_alpha_entries : -1;
}

/* Called once by dn2_ensure, after the machines are registered: the rows and the pages. */
void dn2_sdk_build(void)
{
    init();
    for (int32_t i = 0; i < count; ++i) {
        const struct elek_machine *e = declared(i);
        for (uint32_t id = 0; id < sdk_alpha_entries[i].count; ++id) {
            dn2_add_row(ROWS + SDK_ROW + id, SLOT + id, e->knobs[id].label, 0, e->short_name);
            dn2_row_format(ROWS + SDK_ROW + id, sdk_alpha_value_texts[id]);
        }
        for (uint32_t page = 0; page < sdk_alpha_entries[i].pages; ++page) {
            struct descriptor *d = &pages[i][page];
            const char tab[] = {'S', 'Y', 'N', '1' + page, 0};
            d->name = dn2_representation(&titles[i][page][0], tab, 4);
            d->label = dn2_representation(&titles[i][page][1], sdk_alpha_entries[i].title[page], 16);
            for (int k = 0; k < 8; ++k) {
                int32_t id = sdk_alpha_entries[i].encoder[page][k];
                d->params[k] = id >= 0 ? ROWS + SDK_ROW + id : 0;
            }
            d->params[8] = PAGE_END;
        }
    }
}

uint32_t dn2_sdk_page_count(uint32_t type)
{
    int32_t i = machine_of(type);
    return i < 0 ? 0 : sdk_alpha_entries[i].pages;
}

/* Page `page` of the machine, or 0. */
const struct descriptor *dn2_sdk_page(uint32_t type, uint32_t page)
{
    int32_t i = machine_of(type);
    return i < 0 ? 0 : &pages[i][page];
}

/* Whether the machine has a render: sdk_alpha_build paired it with its SHARC kernel. */
uint32_t dn2_sdk_renders(uint32_t type)
{
    int32_t i = machine_of(type);
    return i >= 0 && declared(i)->render;
}

/* The parameter code of the machine's sound slot (25 + logical knob), 0 when it has no such knob. */
uint32_t dn2_sdk_code(uint32_t type, uint32_t slot)
{
    int32_t i = machine_of(type);
    return i >= 0 && slot >= SLOT && slot < SLOT + sdk_alpha_entries[i].count ? ROWS + SDK_ROW + slot - SLOT : 0;
}

/* The panel: the OS release's addresses (hooks.s, from mod.json's defsym) and layouts (mod.json's cflags:
 * a sound's SOUND_SIZE bytes, its machine at SOUND_TYPE and slot s's word at SOUND_SLOTS + 2s; a View's
 * VIEW_METHODS methods, its page at VIEW_CUR and View::dirty at VIEW_DIRTY; Brain's page stack from
 * BRAIN_STACK_FIRST to BRAIN_STACK_LAST, an entry's manager at STACK_MANAGER and the manager's view at
 * MANAGER_VIEW; Brain's screen state at BRAIN_SCREEN). */
extern volatile uint8_t dn2_active_track[], dn2_shown_page[], dn2_sounds[];
uint8_t *dn2_syn_view;             /* hooks.s records the view passed to the native SYN title accessor */
#define SHOWN_SYN 1                 /* dn2_shown_page while SYN is on the screen */

/* What the tests and the drawing read: the active track's machine, as .present left it. */
int32_t dn2_sdk_shown[16];          /* each track's SDK page, kept across track switches */
uint32_t dn2_sdk_ids[8];            /* the parameter codes the shown page draws (0: empty or hidden) */
static int32_t shown_track = -1;
static int dirty;
extern const uint8_t *volatile dn2_brain;   /* the panel's Brain (hooks.s): set from boot, before any key */
static void (*draw_page)(void *view, void *bmp);
static void sdk_draw(void *view, void *bmp);

extern const uint32_t dn2_syn_methods[];  /* SYN's stock method table (hooks.s) */

/* The view the page manager shows with no menu above it (Brain's page stack, as core-dn1's
 * core_page_shown), or 0. */
static uint8_t *shown_view(void)
{
    const uint8_t *brain = dn2_brain;
    if (!brain)
        return 0;
    const uint8_t *bottom = *(const uint8_t *const *)(brain + BRAIN_STACK_FIRST);
    if (!bottom || bottom != *(const uint8_t *const *)(brain + BRAIN_STACK_LAST))
        return 0;
    const uint8_t *manager = *(const uint8_t *const *)(bottom + STACK_MANAGER);
    /* The manager keeps the owning View handle; its object starts after the 16-byte header. */
    uint8_t *shown = manager ? *(uint8_t *const *)(manager + MANAGER_VIEW) : 0;
    return shown ? shown + 16 : 0;
}

static int syn_shown(void)
{
    return dn2_syn_view && shown_view() == dn2_syn_view;
}

/* SYN's native method table, including RTTI and its VIEW_METHODS primary methods. */
static int page_layout(int sdk)
{
    static uint32_t *view;
    static const uint32_t *stock;
    static uint32_t methods[2 + VIEW_METHODS];
    if (!dn2_syn_view)
        return 0;
    if (view != (uint32_t *)dn2_syn_view) {
        view = (uint32_t *)dn2_syn_view;
        stock = (const uint32_t *)view[0];
        for (int i = -2; i < VIEW_METHODS; ++i)
            methods[i + 2] = stock[i];
        draw_page = (void (*)(void *, void *))stock[4];
        methods[6] = (uint32_t)sdk_draw;
    }
    uint32_t want = (uint32_t)(sdk ? methods + 2 : stock);
    if (view[0] == want)
        return 0;
    view[0] = want;
    return 1;
}

static int32_t track_machine(uint32_t t)
{
    return t < 16 ? machine_of(dn2_sounds[SOUND_SIZE * t + SOUND_TYPE]) : -1;
}

/* ev_midi_cc (core-dn2 0.4): each CC on a track, with the track's machine, to the dispatcher on the DSP,
 * which keeps them for its SDK machines' v->cc (dispatch.s). The stock applies the CC as before. */
int32_t dn2_sdk_midi_cc(int32_t track, int32_t cc, int32_t value, int32_t flags)
{
    (void)flags;
    if ((uint32_t)track < 16 && (uint32_t)cc < 128)
        core_dsp_post((uint32_t)(dn2_sounds[SOUND_SIZE * track + SOUND_TYPE] & 127) << 18 |
                      (uint32_t)track << 14 | (uint32_t)cc << 7 | ((uint32_t)value & 127));
    return 0;
}

/* The active track's SDK machine, or 0: the one whose knobs' value texts the shared rows show. */
const struct sdk_alpha_entry *dn2_sdk_active(void)
{
    int32_t i = track_machine(*dn2_active_track);
    return i < 0 ? 0 : &sdk_alpha_entries[i];
}

/* The active track's synth type, or with `filter` its filter type. */
uint32_t dn2_active_type(uint32_t filter)
{
    uint32_t t = *dn2_active_track;
    return t < 16 ? dn2_sounds[SOUND_SIZE * t + SOUND_TYPE + (filter ? 1 : 0)] : 0;
}

/* Knob `knob`'s default, raw 8.8, for the active track's machine; -1 when it has no such knob. */
int32_t dn2_sdk_default(uint32_t knob)
{
    const struct sdk_alpha_entry *e = dn2_sdk_active();
    return e && knob < e->count ? sdk_alpha_default(&e->machine->knobs[knob]) : -1;
}

/* Knob `id`'s stored value on track t: slot 25 + id, a big-endian word (the sounds are packed). */
static int32_t raw_of(uint32_t t, uint32_t id)
{
    const volatile uint8_t *at = dn2_sounds + SOUND_SIZE * t + SOUND_SLOTS + 2 * (SLOT + id);
    return (int16_t)(at[0] << 8 | at[1]);
}

/* The page SYN shows of machine i (0 when it has one page). */
static int32_t page_of(int32_t i)
{
    int32_t page = dn2_syn_view ? *(const int32_t *)(dn2_syn_view + VIEW_CUR) : 0;
    return page > 0 && (uint32_t)page < sdk_alpha_entries[i].pages ? page : 0;
}

/* .present with track t's values (sdk_alpha_present skips unchanged ones), and the rows' labels. */
static void snapshot(int32_t i, uint32_t t, int32_t page)
{
    const struct sdk_alpha_entry *e = &sdk_alpha_entries[i];
    int32_t raw[SDK_KNOBS];
    for (uint32_t id = 0; id < e->count; ++id)
        raw[id] = raw_of(t, id);
    const struct sdk_alpha_entry *before = sdk_alpha_seen;
    int32_t changed = sdk_alpha_present(e, t, page, raw, 0);
    if (!changed && before == e)
        return;
    dirty |= changed;
    for (uint32_t id = 0; id < e->count; ++id)
        dn2_row_label(ROWS + SDK_ROW + id, sdk_alpha_labels[id], e->machine->short_name);
}

/* The id of the knob at encoder k (0-7) of the page SYN shows of machine i, or -1. */
static int32_t shown_knob(int32_t i, int32_t k)
{
    return sdk_alpha_entries[i].encoder[page_of(i)][k];
}

/* Mono machines: a track's TRK SETUP PLAY MODE (0 POLY, 1 POLY M.LFO, 2 MONO, 3 MONO LEG.) and MONO NOTE
 * PRIO (0 LAST, 1 LOW, 2 HIGH) follow its SDK machine's .play and .prio. They go through the menu's own
 * setters, which tell the voice allocator, and each tick puts back a change made in TRK SETUP. A track
 * whose machine is not an SDK one keeps what it has. */
extern void *const volatile dn2_tracks_root[];
void *dn2_track_object(void *tracks, int32_t track);
int32_t dn2_play_mode(void *track);
void dn2_set_play_mode(void *track, int32_t mode);
int32_t dn2_mono_prio(void *track);
void dn2_set_mono_prio(void *track, int32_t prio);

static void mono_follow(void)
{
    static const int8_t mode[] = { [ELEK_POLY] = 0, [ELEK_MONO] = 2, [ELEK_MONO_LEGATO] = 3 };
    const uint8_t *root = dn2_tracks_root[0];
    void *tracks = root ? *(void *const *)(root + TRACK_LIST) : 0;
    if (!tracks)
        return;                     /* before the project is up */
    for (uint32_t t = 0; t < 16; ++t) {
        int32_t i = track_machine(t);
        if (i < 0 || declared(i)->play > ELEK_MONO_LEGATO || declared(i)->prio > ELEK_PRIO_HIGH)
            continue;
        const struct elek_machine *m = declared(i);
        void *track = dn2_track_object(tracks, (int32_t)t);
        if (dn2_play_mode(track) != mode[m->play])
            dn2_set_play_mode(track, mode[m->play]);
        if (m->play != ELEK_POLY && dn2_mono_prio(track) != m->prio)
            dn2_set_mono_prio(track, m->prio);
    }
}

/* ev_tick, the UI task: each track keeps its page and .present sees the active track's values. */
void dn2_sdk_tick(uint8_t *ctrl)
{
    init();
    dn2_rows_follow();              /* the mk2 voices' and filters' shared rows */
    if (!count)
        return;                     /* no SDK machine: leave Brain and SYN's methods alone */
    mono_follow();
    uint32_t t = *dn2_active_track;
    /* At power-on SYN shows under the project's name: its title accessor (hooks.s) has not run yet. */
    if (!dn2_syn_view) {
        uint8_t *view = shown_view();
        if (view && *(const uint32_t **)view == dn2_syn_methods)
            dn2_syn_view = view;
    }
    int32_t i = track_machine(t);
    if (page_layout(i >= 0))
        ctrl[0x20] = 1;
    if (i < 0) {
        sdk_alpha_seen = 0;
        shown_track = -1;
        for (int k = 0; k < 8; ++k)
            dn2_sdk_ids[k] = 0;
        return;
    }
    if (dn2_syn_view) {
        if ((int32_t)t != shown_track) {
            *(int32_t *)(dn2_syn_view + VIEW_CUR) = dn2_sdk_shown[t] < (int32_t)sdk_alpha_entries[i].pages ? dn2_sdk_shown[t] : 0;
            shown_track = t;
            ctrl[0x20] = 1;
        } else {
            dn2_sdk_shown[t] = page_of(i);
        }
    }
    /* Off SYN no SDK page is on the screen: .present names knobs for the LFO lists, say. */
    snapshot(i, t, *dn2_shown_page == SHOWN_SYN ? page_of(i) : ELEK_UI_NO_PAGE);
    if (dirty) {
        if (dn2_syn_view)
            dn2_syn_view[VIEW_DIRTY] = 1;
        ctrl[0x20] = 1;
        dirty = 0;
    }
    for (int k = 0; k < 8; ++k) {
        int32_t id = shown_knob(i, k);
        dn2_sdk_ids[k] = id >= 0 && sdk_alpha_visibility[id] ? ROWS + SDK_ROW + id : 0;
    }
}

/* Hide only while SYN paints, so the native clipboard, clear and MIDI retain the declared controls. */
static void sdk_draw(void *view, void *bmp)
{
    int32_t i = track_machine(*dn2_active_track);
    if (i < 0) {
        draw_page(view, bmp);
        return;
    }
    snapshot(i, *dn2_active_track, page_of(i));
    struct descriptor *d = &pages[i][page_of(i)];
    uint32_t ids[8];
    for (int k = 0; k < 8; ++k) {
        int32_t id = shown_knob(i, k);
        ids[k] = d->params[k];
        if (id >= 0 && !sdk_alpha_visibility[id])
            d->params[k] = 0;
    }
    draw_page(view, bmp);
    for (int k = 0; k < 8; ++k)
        d->params[k] = ids[k];
}

/* ev_draw supplies the composed bitmap. The view's own bitmap can be clipped to a partial redraw. */
void dn2_sdk_draw(void *bmp, void *ctrl)
{
    (void)ctrl;
    int32_t i = track_machine(*dn2_active_track);
    /* BRAIN_SCREEN is 2 while the screen saver's view covers the frame; SYN stays the shown page. */
    if (i < 0 || !syn_shown() || *(const volatile int32_t *)(dn2_brain + BRAIN_SCREEN) == 2)
        return;
    for (int k = 0; k < 8; ++k) {
        int32_t id = shown_knob(i, k);
        const struct elek_glyph *glyph = id >= 0 ? sdk_alpha_glyphs[id] : 0;
        if (glyph && sdk_alpha_visibility[id])
            sdk_alpha_blit(bmp, 25 + 26 * (k % 4), 34 - 27 * (k / 4), glyph);
    }
}

/* ev_enc: a hidden knob's encoder does nothing while SYN shows it. */
int dn2_sdk_enc(void *brain, const int32_t *event)
{
    (void)brain;
    int32_t encoder = event[3] - 1;
    int32_t i = track_machine(*dn2_active_track);
    int32_t id;
    if (i < 0 || !syn_shown() || encoder < 0 || encoder >= 8 || (id = shown_knob(i, encoder)) < 0)
        return 0;
    return !sdk_alpha_visibility[id];
}
