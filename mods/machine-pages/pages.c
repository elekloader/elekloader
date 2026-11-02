/* SPDX-License-Identifier: GPL-2.0-or-later */
/* machine-pages (Digitakt mk1): answers pages.s's sites from the page each added machine describes (core
 * 3.0: its descriptor's tail, struct cm_ui in elekloader/sdk/include/digitakt-mk1/core3.h). A machine with
 * no page, and every stock one, gets the stock code.
 *
 * Whose machine a site asks about, by site:
 * - the SRC page's (layout, labels, values, graphics, UI records, the pop-up, knob D): the machine whose
 *   layout the page asked for last, as the page asks for it with its track's machine before it draws or
 *   turns a knob;
 * - a range: the machine of the sound the parameter object belongs to;
 * - the LFO page's names: the active track's machine.
 * A parameter id is a knob of a machine's page by the stock machine it copies (page_from: its eight ids,
 * 0x6c + 8 page_from on, A-H), or else by its params machine (the ids MIDI, the LFOs and Randomize use). */
#include "src.h"

#ifdef SDK_ALPHA_DT1
extern uint32_t sdk_alpha_layout(int32_t machine, uint32_t track);
extern int32_t sdk_alpha_empty_selected(void);
#endif

#define KNOBS       8
#define ID_BASE(p)  (0x6cu + 8u * (uint32_t)(p))
#define PAGES       32                         /* added machines with a page; more get the stock page */
#define LAY_LONGS   11

/* The markers a page's abi names: a machine whose page reads this layout links only beside this mod. 3.1
 * is 3 with knobs that draw their own graphic (CM_DRAW), which machine-pages 1.0 does not read; 3.2
 * (src.c) is 3.1 with up to seven extra values on a second page (struct cm_ui32), from machine-pages 1.2;
 * 3.3 is 3.2 whose extra values may name a MIDI CC (+18), from 1.4. */
const char cm_ui_v3[] = "machine-pages 3";
const char cm_ui_v31[] = "machine-pages 3.1";

struct page {
    int32_t id;
    const struct cm_ui *ui;
    const struct cm_ui32 *extended;            /* its extra values (cm_ui_v32), or 0 */
    uint32_t params;                           /* its params machine */
    uint32_t ready;                            /* lay is copied */
    uint32_t lay[2][LAY_LONGS];
};

static struct page pages[PAGES];
static int npages, found;
static struct page *page_now;                  /* the SRC page's machine, if it has a page */
static char pop[16];                           /* the pop-up's text */
static uint8_t selected[8];
extern uint8_t core_samp_tab[];               /* core's: the sample list's SAMP id, by machine (CM_SAMPLE) */

extern const struct cm_machine *const core_machines[];   /* core 2.1's table, 0 at its end */
extern uint32_t mp_stock_layout(int32_t machine);        /* pages.s: the stock layout of machine 0-3 */
extern uint32_t mp_view_knob(void *view, int32_t knob);
extern int32_t mp_shown_value(void *view, uint32_t id, uint8_t *locked);
void *mp_shown_view;                                     /* pages.s: the view drawing a knob's graphic, or 0 */
extern uint32_t mp_view_track(void *view);
extern void mp_page_changed(void *view, void *event);

/* The machines with a page, read from their descriptors once (the table is the linker's: it never
 * changes), the first time a site or the tick asks. */
static void find(void)
{
    int i;
    found = 1;
    for (i = 0; core_machines[i] && npages < PAGES; i++) {
        const struct cm_machine *d = core_machines[i];
        const struct cm_ui *ui = (const struct cm_ui *)core_machine_ui(d->id);
#ifdef SDK_ALPHA_DT1
        const struct cm_ui32 *alpha = sdk_alpha_ui(d->id);
        if (alpha)
            ui = &alpha->base;
#endif
        if (!ui || (ui->abi != cm_ui_v3 && ui->abi != cm_ui_v31 &&
                    ui->abi != cm_ui_v32 && ui->abi != cm_ui_v33) ||
            ui->page_from > 3 || d->id <= 3)
            continue;                          /* no page, or not one this mod reads */
        const struct cm_ui32 *extended = mp_src_for(d->id);
        if ((ui->abi == cm_ui_v32 || ui->abi == cm_ui_v33) && !extended)
            continue;
        pages[npages].id = d->id;
        pages[npages].ui = ui;
        pages[npages].extended = extended;
        pages[npages].params = (uint32_t)d->params;
        npages++;
    }
}

/* A machine's page; 0 for a stock machine or one without a page. */
static struct page *page_of(uint32_t m)
{
    int i;
    if (m <= 3)
        return 0;
    if (!found)
        find();
    for (i = 0; i < npages; i++)
        if (pages[i].id == (int32_t)m)
            return &pages[i];
    return 0;
}

/* The knob (0-7, or 8-14 an extra value) a parameter id is on a page, or -1. */
static int knob_of(const struct page *p, uint32_t id)
{
    if (p->extended && id >= MP_EXTRA_ID && id < MP_EXTRA_END)
        return 8 + id - MP_EXTRA_ID;
    uint32_t k = id - ID_BASE(p->ui->page_from);
    if (k < KNOBS)
        return (int)k;
    k = id - ID_BASE(p->params);
    return k < KNOBS ? (int)k : -1;
}

static const struct cm_knob *knob(const struct page *p, int k)
{
    return k < 8 ? &p->ui->knob[k] : &p->extended->extra[k - 8];
}

static const struct cm_knob *page_knob(uint32_t id)
{
    int k;
    if (!page_now || (k = knob_of(page_now, id)) < 0)
        return 0;
    return knob(page_now, k);
}

/* ---- the SRC page -------------------------------------------------------------------------------- */

uint32_t mp_layout_track(int32_t m, uint32_t track)
{
    struct page *p = page_of((uint32_t)m);
    int i;
    page_now = p;
#ifdef SDK_ALPHA_DT1
    uint32_t empty = sdk_alpha_layout(m, track);
    if (empty)
        return empty;
#endif
    if (!p)
        return 0;
#ifdef SDK_ALPHA_DT1
    const struct elek_machine *alpha = sdk_alpha_machine(m);
    if (alpha)
        p->ready = 0; /* Presentation may change without changing the machine descriptor. */
#endif
    if (!p->ready) {
        const uint32_t *s = (const uint32_t *)mp_stock_layout(p->ui->page_from);
        int count = p->extended ? p->extended->page_count : 1;
        for (int page = 0; page < count; ++page) {
            for (i = 0; i < LAY_LONGS; i++)
                p->lay[page][i] = s[i];
            for (i = 0; i < KNOBS; i++) {
                const struct cm_src_page *pg = p->extended ? &p->extended->pages[page] : 0;
                int k = pg ? pg->parameter[i] : i;
                if (pg && (pg->stock & CM_SRC_STOCK(i)))
                    p->lay[page][2 + i] = k;   /* a stock sound parameter: the firmware's own knob */
                else if (k == CM_SRC_BLANK || (knob(p, k)->flags & CM_HIDDEN)
#ifdef SDK_ALPHA_DT1
                         || (alpha && !sdk_alpha_visible(alpha, k))
#endif
                         )
                    p->lay[page][2 + i] = 0;
                else if (p->extended)
                    p->lay[page][2 + i] = k < 8 ? ID_BASE(p->params) + k : MP_EXTRA_ID + k - 8;
            }
        }
        p->ready = 1;
    }
    if (track < 8 && p->extended) {
        if (selected[track] >= p->extended->page_count)
            selected[track] = 0;
        return (uint32_t)p->lay[selected[track]];
    }
    return (uint32_t)p->lay[0];
}

#ifdef SDK_ALPHA_DT1
extern void sdk_alpha_redraw(void *view);
uint32_t sdk_alpha_layout_view(int32_t machine, uint32_t track, void *view)
{
    const struct elek_machine *m = sdk_alpha_machine(machine);
    if (m && m->page_count && track < 8 &&
        sdk_alpha_snapshot(m, selected[track] < m->page_count ? selected[track] : 0, view))
        sdk_alpha_redraw(view);
    return mp_layout_track(machine, track);
}
#endif

uint32_t mp_layout(int32_t m)
{
    return mp_layout_track(m, fw_active_track);
}

/* Native SRC actions collect the page's declared controls, regardless of alpha presentation. */
uint32_t mp_action_layout(uint32_t page, int32_t machine)
{
    uint32_t track = fw_active_track;
    if ((page != 4 && page != 5) || track >= 8)
        return 0;
    if (machine == -2) {                     /* The ID collector has no source object. */
        const uint8_t *kit = fw_kit;
        if (!kit)
            return 0;
        machine = FW_SOUND(kit, track)[FW_SOUND_MACHINE];
    }
    const struct cm_ui32 *ui = mp_src_for(machine);
    if (!ui)
        return 0;
    uint32_t layout = mp_layout_track(machine, track);
#ifdef SDK_ALPHA_DT1
    if (sdk_alpha_machine(machine)) {
        static uint32_t actions[LAY_LONGS];
        const uint32_t *shown = (const uint32_t *)layout;
        for (int i = 0; i < LAY_LONGS; ++i)
            actions[i] = shown[i];
        const struct cm_src_page *pg = &ui->pages[selected[track]];
        for (int i = 0; i < KNOBS; ++i) {
            int k = pg->parameter[i];
            actions[2 + i] = k == CM_SRC_BLANK ? 0 :
                k < 8 ? ID_BASE(ui->base.page_from) + k : MP_EXTRA_ID + k - 8;
        }
        return (uint32_t)actions;
    }
#endif
    return layout;
}

int32_t mp_src_key(void *view, const uint32_t *event)
{
    if (event[3] != 20 || (event[4] & ~4u) != 16)
        return 0;
    uint32_t track = mp_view_track(view);
    if (track >= 8)
        return 0;
    mp_view_knob(view, 0);                 /* Resolve the view's sound, including sound locks. */
#ifdef SDK_ALPHA_DT1
    if (sdk_alpha_empty_selected()) {
        mp_page_changed(view, (void *)event);
        return 1;
    }
#endif
    if (!page_now)
        return 0;
    if (page_now->extended)
        selected[track] = (selected[track] + 1) % page_now->extended->page_count;
    else if (!(page_now->ui->knob[3].flags & CM_NOT_SAMPLE))
        return 0;                           /* a sample machine: the stock step to its waveform */
    /* else a one-page machine that plays no sample: no waveform to step to, so SRC keeps its page */
    mp_page_changed(view, (void *)event);
    return 1;
}

uint32_t mp_src_title(void *view)
{
    uint32_t track = mp_view_track(view);
    mp_view_knob(view, 0);
    if (track >= 8 || !page_now || !page_now->extended)
        return 0;
#ifdef SDK_ALPHA_DT1
    const struct elek_machine *alpha = sdk_alpha_machine(page_now->id);
    if (alpha && sdk_alpha_snapshot(alpha, selected[track], view))
        sdk_alpha_redraw(view);
#endif
    return (uint32_t)page_now->extended->pages[selected[track]].title;
}

uint32_t mp_label(uint32_t id, int32_t long_name)
{
    const struct cm_knob *k = page_knob(id);
    if (!k)
        return 0;
#ifdef SDK_ALPHA_DT1
    const struct elek_machine *alpha = sdk_alpha_machine(page_now->id);
    if (alpha)
        return (uint32_t)sdk_alpha_label(alpha, knob_of(page_now, id));
#endif
    return (uint32_t)(long_name ? k->lname : k->name);
}

int32_t mp_value_text(uint32_t id, int32_t value, char *buf)
{
    const struct cm_knob *k = page_knob(id);
#ifdef SDK_ALPHA_DT1
    if (k && sdk_alpha_value_text(page_now->id, knob_of(page_now, id), value, buf, 0))
        return 1;
#endif
    return k && k->fmt && k->fmt(buf, value, 0, page_now->id);
}

uint32_t mp_pop_text(uint32_t id, int32_t value)
{
    const struct cm_knob *k = page_knob(id);
#ifdef SDK_ALPHA_DT1
    if (k && sdk_alpha_value_text(page_now->id, knob_of(page_now, id), value, pop, 1))
        return (uint32_t)pop;
#endif
    if (!k || !k->fmt)
        return 0;
    pop[0] = 0;
    if (!k->fmt(pop, value, 1, page_now->id))
        return 0;
    pop[sizeof pop - 1] = 0;
    return (uint32_t)pop;
}

/* The knob's graphic: a = its arguments from the id on, [id, value, flag, -, bmp, x, y]. -> nonzero when
 * the machine drew it (CM_DRAW); else a holds the borrowed look's id and the graphic's value. With CM_DRAW
 * the slot at +36 is draw, never gfx: on a cm_ui_v3 page (which should name cm_ui_v31) it is not called. */
int32_t mp_knob(int32_t *a)
{
    const struct cm_knob *k = page_knob((uint32_t)a[0]);
    if (!k)
        return 0;
#ifdef SDK_ALPHA_DT1
    const struct elek_machine *alpha = sdk_alpha_machine(page_now->id);
    if (alpha && mp_shown_view) {
        uint32_t track = mp_view_track(mp_shown_view);
        if (track < 8 && sdk_alpha_snapshot(alpha, selected[track], mp_shown_view))
            sdk_alpha_redraw(mp_shown_view);
    }
    const struct elek_ui_glyph *glyph = alpha ? sdk_alpha_glyph(alpha, knob_of(page_now, a[0])) : 0;
    if (glyph) {
        void *bmp = (void *)a[4];
        int32_t x = a[5], y = a[6];
        fw_fillrect(bmp, x + 1, y, x + 17, y + 16, 0);
        for (int row = 0; row < 16; ++row)
            for (int col = 0; col < 16; ++col)
                if (glyph->rows[row] & (0x8000u >> col))
                    fw_fillrect(bmp, x + 1 + col, y + 16 - row,
                                x + 1 + col, y + 16 - row, 1);
        return 1;
    }
#endif
    if (k->flags & CM_DRAW) {
        if (page_now->ui->abi != cm_ui_v3 && k->draw && k->draw((void *)a[4], a[5], a[6], a[1], a[2]))
            return 1;
    } else if (k->gfx)
        a[1] = k->gfx(a[1]);
    if (k->look)
        a[0] = k->look;
    return 0;
}

/* Value k of the sound the page shows, as it shows it, while a page view draws one of its knobs. */
int32_t cm_src_shown(int32_t k)
{
    const struct page *p = page_now;
    uint8_t locked;
    if (!mp_shown_view || !p || k < 0 || k >= (p->extended ? 8 + CM_EXTRAS : 8))
        return CM_NO_VALUE;
    uint32_t id = k >= 8 ? MP_EXTRA_ID + (uint32_t)k - 8 : ID_BASE(p->extended ? p->params : p->ui->page_from) + (uint32_t)k;
    return mp_shown_value(mp_shown_view, id, &locked);
}

uint32_t mp_look(uint32_t id)
{
    const struct cm_knob *k = page_knob(id);
    return k && k->look ? k->look : id;
}

int32_t mp_not_sample(void)
{
#ifdef SDK_ALPHA_DT1
    if (sdk_alpha_empty_selected())
        return 1;
#endif
    return page_now && (page_now->ui->knob[3].flags & CM_NOT_SAMPLE) != 0;
}

/* ---- the sample list for a knob but D (CM_SAMPLE) -------------------------------------------------- */

/* The sample list finds the parameter it shows and sets by the machine: core points its six lookups at
 * core_samp_tab, an added machine's entry its params machine's SAMP (knob D). While a CM_SAMPLE knob has
 * the list, its machine's entry is that knob's id instead: the list shows its slot, a pick (and a lock, on
 * a held trig) writes it, the pop-up names its sample. The next compare puts knob D back. Only an added
 * machine's entry changes, so a stock page never sees another. */
static int32_t samp_moved = -1;                /* the machine whose entry is a CM_SAMPLE knob's, or -1 */

static void samp_reset(void)
{
    const struct page *p;
    if (samp_moved < 0)
        return;
    p = page_of((uint32_t)samp_moved);
    if (p)
        core_samp_tab[samp_moved] = (uint8_t)(ID_BASE(p->params) + 3);
    samp_moved = -1;
}

/* The SRC page's two compares (a knob turned, a knob pressed): id the knob's parameter. -> 0 when the
 * sample list is for it (the compare says "SAMP"): knob D unless CM_NOT_SAMPLE, or a CM_SAMPLE knob. */
int32_t mp_samp_not(uint32_t id)
{
    const struct page *p = page_now;
    int k;
    samp_reset();
    if (p && p->id < 128 && (k = knob_of(p, id)) >= 0 && k != 3 && (p->ui->knob[k].flags & CM_SAMPLE)) {
        core_samp_tab[p->id] = (uint8_t)(ID_BASE(p->ui->page_from) + (uint32_t)k);
        samp_moved = p->id;
        return 0;
    }
    if (id != 0x87)
        return 1;
    return mp_not_sample();
}

/* ---- ranges -------------------------------------------------------------------------------------- */

/* out = {min, max, default}, the stock range; machine: the sound's, or -1. */
void mp_range(uint32_t id, int32_t *out, int32_t machine)
{
    const struct page *p;
    const struct cm_knob *k;
    int i;
    if (machine < 0 || !(p = page_of((uint32_t)machine)) || (i = knob_of(p, id)) < 0)
        return;
    k = knob(p, i);
    if (k->flags & CM_RANGE) {
        out[0] = k->min;
        out[1] = k->max;
    }
    if (k->flags & CM_DEFAULT)
        out[2] = k->def;
}

void mp_reset_range(uint32_t id, int32_t *out, int32_t machine)
{
    if (mp_ui32(machine))
        mp_range(id, out, machine);
}

/* An audio track's machine in the UI kit, or -1 (a MIDI track, no kit): MIDI's parameter setter's. */
int32_t mp_track_machine(uint32_t track)
{
    const uint8_t *kit = fw_kit;
    if (track > 7 || !kit)
        return -1;
    return FW_SOUND(kit, track)[FW_SOUND_MACHINE];
}

/* ---- the LFO page ---------------------------------------------------------------------------------- */

static const struct page *active_page(void)
{
    uint32_t t = fw_active_track;
    const uint8_t *kit = fw_kit;
    if (t > 7 || !kit)
        return 0;
    return page_of(FW_SOUND(kit, t)[FW_SOUND_MACHINE]);
}

/* A destination's name, for the active track's machine: what 0 the group, 1 the long name, 2 the short
 * one; stock, what the firmware has (0 when it has not read it yet). */
uint32_t mp_lfo(uint32_t id, int32_t what, uint32_t stock)
{
    const struct page *p = active_page();
    const struct cm_knob *k;
    const char *s;
    uint32_t i;
    if (!p)
        return stock;
    i = id - ID_BASE(p->params);
    if (i >= KNOBS) {
        if (!p->extended || id < MP_EXTRA_ID || id >= MP_EXTRA_END)
            return stock;
        i = 8 + id - MP_EXTRA_ID;
    }
    if (what == 0)
        return p->ui->group ? (uint32_t)p->ui->group : stock;
#ifdef SDK_ALPHA_DT1
    const struct elek_machine *alpha = sdk_alpha_machine(p->id);
    if (alpha) {
        sdk_alpha_snapshot(alpha, ELEK_UI_NO_PAGE, 0);
        const char *label = sdk_alpha_label(alpha, i);
        if (label)
            return (uint32_t)label;
    }
#endif
    k = knob(p, i);
    s = what == 1 ? (k->lfo_long ? k->lfo_long : k->lname) : (k->lfo ? k->lfo : k->name);
    if (!s && (k->flags & CM_HIDDEN))
        s = what == 1 ? "Unused" : "-";
    return s ? (uint32_t)s : stock;
}

uint32_t mp_lfo52(uint32_t off, int32_t what, uint32_t stock)
{
    return off % 52 ? stock : mp_lfo(off / 52, what, stock);
}

/* ---- a track that turns to a machine with a page, or away from one (ev_tick, 30 Hz) ----------------- */

/* A machine with a page that takes a stock machine's parameters gives some of knobs A-H other meanings:
 * GRAIN's MODE 4 is a PLAY ONESHOT does not have (ERR), its SIZE a heavy BR. A track that turns back to a
 * stock machine would keep those values read the stock way. So while a track's machine lends nothing its
 * knob values are kept, every tick; when it turns back from one that lends, the knobs lent on the way get
 * the kept values again, written into the kit and through the knobs' own path into the engine
 * (fw_set_param). ONESHOT -> GRAIN -> ONESHOT comes back as it was, through any number of such machines.
 *
 * A knob is lent if the page gives it its own look, flags (range, default, graphic), text or graphic; a
 * stock knob only renamed (FLIP's LEV) is not.
 *
 * Only a switch is undone, not a load: a switch changes the machine and nothing else of the sound in one
 * tick, a sound loaded changes it all. A snapshot of each sound (one track a tick: eight sounds compared
 * and copied every tick starved the OS's first boot, src-views 1.2's lesson) tells them apart: more than 8
 * bytes changed beside the machine and knobs A-H is a load. A track loaded on a lending machine has nothing
 * kept; turned to a stock one, of the knobs lent its PLAY goes back to 0 if past the stock list (no ERR),
 * BR to 0 and LEV to 100. (Was src-views', a separate mod, before 1.7.) */
#define SND_SIZE    0xa2
#define SND_SLOT0   17                         /* fw_set_param's slot of knob A */
#define K_PLAY      1
#define K_BR        2
#define K_LEV       7

static const uint8_t *seen_kit;
static uint8_t seen[8], keep_ok[8], lent[8], snap_next;
static int16_t keep[8][KNOBS];
static uint8_t snap[8][SND_SIZE];

/* the knobs machine m lends (bit k: knob k), 0 for a stock machine or one without a page */
static uint8_t lends(uint8_t m)
{
    const struct page *p = page_of(m);
    uint8_t b = 0;
    int k;
    for (k = 0; p && k < KNOBS; k++) {
        const struct cm_knob *n = &p->ui->knob[k];
        if (n->look || n->flags || n->fmt || n->gfx)
            b |= (uint8_t)(1u << k);
    }
    return b;
}

static int16_t *knob_word(uint8_t *snd, int k)
{
    return (int16_t *)(snd + FW_SOUND_PARAM(k));
}

static void snap_take(int t, const uint8_t *snd)
{
    int i;
    for (i = 0; i < SND_SIZE; i++)
        snap[t][i] = snd[i];
}

/* the bytes of the sound that changed since its snapshot, the machine and knobs A-H aside */
static int changed_rest(int t, const uint8_t *snd)
{
    int i, n = 0;
    for (i = 0; i < SND_SIZE; i++) {
        if (i == FW_SOUND_MACHINE || (i >= FW_SOUND_PARAM(0) && i < FW_SOUND_PARAM(KNOBS)))
            continue;
        n += snd[i] != snap[t][i];
    }
    return n;
}

static void put_knob(int t, uint8_t *snd, int k, int16_t v)
{
    *knob_word(snd, k) = v;
    fw_set_param(v, t, SND_SLOT0 + k);
}

/* track t's machine turned to m: back from lending, the knobs as they were */
static void turned(int t, uint8_t *snd, uint8_t m)
{
    uint8_t l = lends(m);
    int k;
    if (changed_rest(t, snd) > 8) {            /* a sound loaded: taken as it is */
        keep_ok[t] = 0;
        lent[t] = l;
        return;
    }
    if (lent[t] && !l) {
        for (k = 0; k < KNOBS; k++) {
            if (!(lent[t] & (1u << k)))
                continue;
            if (keep_ok[t])
                put_knob(t, snd, k, keep[t][k]);
            else if (k == K_PLAY && *knob_word(snd, k) > (3 << 8))
                put_knob(t, snd, k, 0);
            else if (k == K_BR)
                put_knob(t, snd, k, 0);         /* no bit reduction */
            else if (k == K_LEV)
                put_knob(t, snd, k, 100 << 8);
        }
        lent[t] = 0;
    }
    lent[t] |= l;
}

void mp_tick(void *ctrl)
{
    uint8_t *kit = fw_kit;
    int t, k;
    (void)ctrl;
    if (!kit)
        return;
    if (kit != seen_kit) {                     /* another kit: taken as it is */
        for (t = 0; t < 8; t++) {
            uint8_t *snd = FW_SOUND(kit, t);
            seen[t] = snd[FW_SOUND_MACHINE];
            selected[t] = 0;
            keep_ok[t] = 0;
            lent[t] = lends(seen[t]);
            snap_take(t, snd);
        }
        seen_kit = kit;
        return;
    }
    for (t = 0; t < 8; t++) {
        uint8_t *snd = FW_SOUND(kit, t);
        uint8_t m = snd[FW_SOUND_MACHINE], from = seen[t];
        const struct page *p;
        if (m == from) {
            if (!lent[t]) {                    /* lending nothing: kept as they are now */
                for (k = 0; k < KNOBS; k++)
                    keep[t][k] = *knob_word(snd, k);
                keep_ok[t] = 1;
            }
            continue;
        }
        seen[t] = m;
        selected[t] = 0;
        turned(t, snd, m);
        p = page_of(m);
        if (p && p->ui->on_switch)
            p->ui->on_switch(t, from, snd);
        snap_take(t, snd);
    }
    t = snap_next++ & 7;
    snap_take(t, FW_SOUND(kit, t));
}
