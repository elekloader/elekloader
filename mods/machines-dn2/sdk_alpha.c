/* SPDX-License-Identifier: GPL-2.0-or-later */
/* machines-dn2 0.2: the SDK alpha's machines (elekloader/sdk_alpha_machine.h) on the Digitone II. A mod
 * built by sdk_alpha_build contributes its registration to the table sdk_alpha_machines. Each machine is
 * a synth in MACHINE SEL that FM TONE plays (no DSP code), with its pages (two at most) as SYN's pages.
 * Logical knob i keeps its value in sound slot 25 + i, whichever page shows it; every SDK machine
 * shares the parameter rows ROWS + SDK_ROW + i, labelled for the active track's machine. */
#include "mk2/machines.h"
#include "dev.h"
#include "dn2.h"

const char sdk_alpha_dt1_ui_v1[] = "sdk alpha 1";
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
        for (uint32_t id = 0; id < sdk_alpha_entries[i].count; ++id)
            dn2_add_row(ROWS + SDK_ROW + id, SLOT + id, sdk_alpha_entries[i].knob[id]->label, 0, e->short_name);
        for (uint32_t page = 0; page < e->page_count && page < 2; ++page) {
            struct descriptor *d = &pages[i][page];
            const char tab[] = {'S', 'Y', 'N', '1' + page, 0};
            d->name = dn2_representation(&titles[i][page][0], tab, 4);
            d->label = dn2_representation(&titles[i][page][1], e->pages[page].label, 16);
            for (int k = 0; k < 8; ++k) {
                const struct elek_ui_knob *knob = e->pages[page].knob[k];
                d->params[k] = knob && knob->id < sdk_alpha_entries[i].count ? ROWS + SDK_ROW + knob->id : 0;
            }
            d->params[8] = PAGE_END;
        }
    }
}

uint32_t dn2_sdk_page_count(uint32_t type)
{
    int32_t i = machine_of(type);
    return i < 0 ? 0 : declared(i)->page_count ? declared(i)->page_count : 1;
}

/* Page `page` of the machine; 0 for an empty machine, whose one page is its descriptor's. */
const struct descriptor *dn2_sdk_page(uint32_t type, uint32_t page)
{
    int32_t i = machine_of(type);
    return i < 0 || !declared(i)->page_count ? 0 : &pages[i][page];
}

/* The parameter code of the machine's sound slot (25 + logical knob), 0 when it has no such knob. */
uint32_t dn2_sdk_code(uint32_t type, uint32_t slot)
{
    int32_t i = machine_of(type);
    return i >= 0 && slot >= SLOT && slot < SLOT + sdk_alpha_entries[i].count ? ROWS + SDK_ROW + slot - SLOT : 0;
}

/* The panel: OS 1.12's addresses (hooks.s). */
extern volatile uint8_t dn2_active_track[], dn2_shown_page[], dn2_sounds[];
uint8_t *dn2_syn_view;             /* hooks.s records the view passed to the native SYN title accessor */
#define SOUND_SIZE 0x48b
#define SOUND_TYPE 0xde
#define SOUND_SLOTS 20              /* sound slot s is the word at +20 + 2s */
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
    const uint8_t *bottom = *(const uint8_t *const *)(brain + 0x54);
    if (!bottom || bottom != *(const uint8_t *const *)(brain + 0x5c))
        return 0;
    const uint8_t *manager = *(const uint8_t *const *)(bottom + 8);
    /* The manager keeps the owning View handle; its object starts after the 16-byte header. */
    uint8_t *shown = manager ? *(uint8_t *const *)(manager + 0xc0) : 0;
    return shown ? shown + 16 : 0;
}

static int syn_shown(void)
{
    return dn2_syn_view && shown_view() == dn2_syn_view;
}

/* SYN's native method table, including RTTI and its 60 primary methods (OS 1.12). */
static int page_layout(int sdk)
{
    static uint32_t *view;
    static const uint32_t *stock;
    static uint32_t methods[62];
    if (!dn2_syn_view)
        return 0;
    if (view != (uint32_t *)dn2_syn_view) {
        view = (uint32_t *)dn2_syn_view;
        stock = (const uint32_t *)view[0];
        for (int i = -2; i < 60; ++i)
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

/* Knob `id`'s stored value on track t: slot 25 + id, a big-endian word (the sounds are packed). */
static int32_t raw_of(uint32_t t, uint32_t id)
{
    const volatile uint8_t *at = dn2_sounds + SOUND_SIZE * t + SOUND_SLOTS + 2 * (SLOT + id);
    return (int16_t)(at[0] << 8 | at[1]);
}

/* The page SYN shows of machine i (0 when it has one page or none). */
static int32_t page_of(int32_t i)
{
    int32_t page = dn2_syn_view ? *(const int32_t *)(dn2_syn_view + 144) : 0;
    return page > 0 && (uint32_t)page < declared(i)->page_count ? page : 0;
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

/* The knob at encoder k (0-7) of the page SYN shows of machine i, or 0. */
static const struct elek_ui_knob *shown_knob(int32_t i, int32_t k)
{
    const struct elek_machine *e = declared(i);
    return e->page_count ? e->pages[page_of(i)].knob[k] : 0;
}

/* ev_tick, the UI task: each track keeps its page and .present sees the active track's values. */
void dn2_sdk_tick(uint8_t *ctrl)
{
    init();
    if (!count)
        return;                     /* no SDK machine: leave Brain and SYN's methods alone */
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
    if (declared(i)->page_count && dn2_syn_view) {
        if ((int32_t)t != shown_track) {
            *(int32_t *)(dn2_syn_view + 144) = dn2_sdk_shown[t] < (int32_t)declared(i)->page_count ? dn2_sdk_shown[t] : 0;
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
            dn2_syn_view[0x14] = 1;
        ctrl[0x20] = 1;
        dirty = 0;
    }
    for (int k = 0; k < 8; ++k) {
        const struct elek_ui_knob *knob = shown_knob(i, k);
        dn2_sdk_ids[k] = knob && sdk_alpha_visibility[knob->id] ? ROWS + SDK_ROW + knob->id : 0;
    }
}

/* Hide only while SYN paints, so the native clipboard, clear and MIDI retain the declared controls. */
static void sdk_draw(void *view, void *bmp)
{
    int32_t i = track_machine(*dn2_active_track);
    if (i < 0 || !declared(i)->page_count) {
        draw_page(view, bmp);
        return;
    }
    snapshot(i, *dn2_active_track, page_of(i));
    struct descriptor *d = &pages[i][page_of(i)];
    uint32_t ids[8];
    for (int k = 0; k < 8; ++k) {
        const struct elek_ui_knob *knob = shown_knob(i, k);
        ids[k] = d->params[k];
        if (knob && !sdk_alpha_visibility[knob->id])
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
    /* Brain +0xe8 is 2 while the screen saver's view covers the frame; SYN stays the shown page. */
    if (i < 0 || !declared(i)->page_count || !syn_shown() || *(const volatile int32_t *)(dn2_brain + 0xe8) == 2)
        return;
    for (int k = 0; k < 8; ++k) {
        const struct elek_ui_knob *knob = shown_knob(i, k);
        const struct elek_ui_glyph *glyph = knob ? sdk_alpha_glyphs[knob->id] : 0;
        if (glyph && sdk_alpha_visibility[knob->id])
            sdk_alpha_blit(bmp, 25 + 26 * (k % 4), 34 - 27 * (k / 4), glyph);
    }
}

/* ev_enc: a hidden knob's encoder does nothing while SYN shows it. */
int dn2_sdk_enc(void *brain, const int32_t *event)
{
    (void)brain;
    int32_t encoder = event[3] - 1;
    int32_t i = track_machine(*dn2_active_track);
    const struct elek_ui_knob *knob;
    if (i < 0 || !syn_shown() || encoder < 0 || encoder >= 8 || !(knob = shown_knob(i, encoder)))
        return 0;
    return !sdk_alpha_visibility[knob->id];
}
