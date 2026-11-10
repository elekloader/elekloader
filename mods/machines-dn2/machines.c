/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include "mk2/machines.h"
#include "dn2.h"

#define FILTER_COUNT 32
#define VOICE_COUNT 32              /* SHARC voices: the DSP bridge's table has 32 types */
struct names { const char *name, *label, *hint; };
/* The stock parameter table's 60-byte row; the bound sites in mod.json allow T1_END. */
struct row {
    int32_t owner, slot, min, max, def, flag;
    int16_t cc, cc_low;
    uint32_t kind, unknown, mask;
    const char *name, *owner_name, *label;
    uint32_t format, end;
};
#define ADDED 40                    /* five pages of eight knobs */
#define TEMPLATE 0xd5               /* A Env Decay: a plain 0..127 knob */
#define G_CODE 361                  /* ROWS + ADDED: filter G, where machines-dn2 0.1 put it */
#define T1_END 377                  /* ROWS + SDK_ROW + SDK_KNOBS: the table's rows */
#define G_TEMPLATE 0x26             /* MULTI-MODE's TYPE: the stock rows for slot 66 */
#define G_SLOT 66
#define G_CELL 6                    /* G's place in the FLTR page's descriptor */
#define SYNTH_HOST 0                /* FM TONE: the stock type the OS sees for a custom synth */
#define FILTER_HOST 1               /* LOWPASS 4: the same for a custom filter */

extern const struct mk2_synth *const mk2_voice_synths[];
extern const struct mk2_voice_pages *const mk2_voice_pages[];
extern const struct names dn2_stock_names[5];
extern const uint32_t dn2_stock_permit[5];
extern const struct descriptor dn2_stock_descriptors[5];
extern const char dn2_unknown_name[], dn2_unknown_label[];

uint32_t dn2_list[COUNT], dn2_list_bytes;
static const struct mk2_synth *registered[COUNT];
static struct descriptor descriptors[COUNT];
static struct string_rep strings[COUNT][2];
static uint8_t ui_only[COUNT];      /* an SDK machine (sdk_alpha.c) */
static uint8_t sdk_render[COUNT];   /* an SDK machine with a render: its SHARC kernel plays it, not FM TONE */
static uint32_t ready;
struct row dn2_t1[T1_END];     /* the stock rows are copied in at boot (start.s) */
/* A voice's page p, knob k is sound slot 25 + 8p + k, whose row ROWS + 8p + k every voice shares,
 * labelled for the active track's voice (dn2_rows_follow) and taking its default (dn2_range). */
static struct descriptor pages[VOICE_COUNT][ADDED / 8];
static const struct mk2_voice_pages *voice_pages[VOICE_COUNT];
static uint8_t page_count[COUNT];
static struct string_rep tabs[ADDED / 8];
static struct string_rep labels[T1_END - ROWS];

const char *dn2_representation(struct string_rep *r, const char *s, unsigned max)
{
    unsigned n = 0;
    while (n < max && s[n]) {
        unsigned char c = s[n];
        r->text[n++] = c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
    }
    r->text[n] = 0;
    r->length = r->capacity = n;
    r->references = -1;             /* Stock std::string must not free this representation. */
    return r->text;
}

/* 1..5 printable ASCII characters, or 0. */
static unsigned label_length(const char *s)
{
    unsigned n = 0;
    while (s && n < 6 && s[n] >= ' ' && s[n] <= '~')
        ++n;
    return s && !s[n] && n <= 5 ? n : 0;
}

static int valid_pages(const struct mk2_voice_pages *p)
{
    if (p->id >= VOICE_COUNT || !registered[p->id] || page_count[p->id]
            || !p->page || !p->count || p->count > ADDED / 8)
        return 0;
    for (unsigned i = 0; i < p->count; ++i)
        for (unsigned k = 0; k < 8; ++k) {
            const struct mk2_param *m = p->page[i].knob[k];
            if (m && (m->def > 127 || !label_length(m->label)))
                return 0;
        }
    return 1;
}

/* The MIDI CC the host's stock row for `slot` has, or -1: a knob takes its position's CC. */
static int16_t host_cc(int32_t slot)
{
    for (unsigned code = 1; code < ROWS; ++code)
        if (dn2_t1[code].owner == SYNTH_HOST && dn2_t1[code].slot == slot)
            return dn2_t1[code].cc;
    return -1;
}

/* A row copied from a stock one; the caller sets what differs. Field by field: a struct copy
 * is a call to memcpy. */
static void copy_row(struct row *r, const struct row *t)
{
    r->flag = t->flag;
    r->cc = t->cc;
    r->cc_low = t->cc_low;
    r->kind = t->kind;
    r->unknown = t->unknown;
    r->mask = t->mask;
    r->format = t->format;
    r->end = t->end;
    r->min = 0;
    r->max = 127 << 8;
}

/* An added row: TEMPLATE's, owned by the host, for one sound slot. */
void dn2_add_row(uint32_t code, int32_t slot, const char *label, uint32_t def, const char *owner_name)
{
    struct row *r = &dn2_t1[code];
    copy_row(r, &dn2_t1[TEMPLATE]);
    r->owner = SYNTH_HOST;
    r->slot = slot;
    r->def = def << 8;
    r->cc = host_cc(slot);
    r->name = r->label = dn2_representation(&labels[code - ROWS], label, 5);
    r->owner_name = owner_name;
}

/* An added row's label and owner, in place: stock code may hold the pointers. */
void dn2_row_label(uint32_t code, const char *label, const char *owner_name)
{
    dn2_representation(&labels[code - ROWS], label, 5);
    dn2_t1[code].owner_name = owner_name;
}

/* An added row's value text: format(value 8.8, buf), which the stock rows' formatter field holds. */
void dn2_row_format(uint32_t code, void (*format)(int32_t value, char *buf))
{
    dn2_t1[code].format = (uint32_t)(uintptr_t)format;
}

/* Each declared knob's slot gets a parameter row after the stock ones, copied from TEMPLATE. */
static void build_pages(void)
{
    const struct mk2_voice_pages *const *e;
    for (unsigned i = 0; i < ADDED / 8; ++i) {
        const char tab[] = {'S', 'Y', 'N', '1' + i, 0};
        dn2_representation(&tabs[i], tab, 4);
    }
    for (e = mk2_voice_pages; *e; ++e) {
        const struct mk2_voice_pages *p = *e;
        if (!valid_pages(p))
            continue;
        voice_pages[p->id] = p;
        page_count[p->id] = p->count;
        for (unsigned i = 0; i < p->count; ++i) {
            struct descriptor *d = &pages[p->id][i];
            d->name = tabs[i].text;
            d->label = strings[p->id][0].text;
            for (unsigned k = 0; k < 8; ++k) {
                const struct mk2_param *m = p->page[i].knob[k];
                unsigned code = ROWS + 8 * i + k;
                d->params[k] = m ? code : 0;
                if (m)
                    dn2_add_row(code, SLOT + 8 * i + k, m->label, m->def, strings[p->id][0].text);
            }
            d->params[8] = PAGE_END;
        }
    }
}

void dn2_ensure(void)
{
    if (ready)
        return;
    const struct mk2_synth *const *e, *s;
    for (e = mk2_voice_synths; *e; ++e) {
        s = *e;
        if (s->id < FIRST || s->id >= VOICE_COUNT || !s->name || !s->label || registered[s->id])
            continue;
        registered[s->id] = s;
        descriptors[s->id].name = dn2_representation(&strings[s->id][0], s->name, 31);
        descriptors[s->id].label = dn2_representation(&strings[s->id][1], s->label, 7);
    }
    /* SDK machines after the SHARC voices: a voice keeps its ID. */
    for (unsigned i = 0; (s = dn2_sdk_synth(i)); ++i) {
        if (s->id < FIRST || s->id >= COUNT || registered[s->id])
            continue;
        registered[s->id] = s;
        ui_only[s->id] = 1;
        sdk_render[s->id] = dn2_sdk_renders(s->id);
        descriptors[s->id].name = dn2_representation(&strings[s->id][0], s->name, 31);
        descriptors[s->id].label = dn2_representation(&strings[s->id][1], s->label, 7);
    }
    build_pages();
    dn2_sdk_build();
    /* Stock ordering puts FM DRUM before WAVETONE and MIDI last. */
    dn2_list[0] = 0; dn2_list[1] = 2; dn2_list[2] = 1; dn2_list[3] = 3;
    unsigned n = 4;
    for (unsigned id = FIRST; id < COUNT; ++id)
        if (registered[id])
            dn2_list[n++] = id;
    dn2_list[n++] = 4;
    dn2_list_bytes = n * 4;
    ready = 1;
}

uint32_t dn2_host_type(uint32_t type)
{
    if (type < FIRST || type >= COUNT)
        return type;
    dn2_ensure();
    /* A missing mod keeps its saved identity, but sends MIDI's silent type to the DSP. */
    return registered[type] ? SYNTH_HOST : 4;
}

uint32_t dn2_is_voice(uint32_t type)
{
    dn2_ensure();
    return type < COUNT && registered[type];
}

/* The type the frame sends the DSP: a voice's own, whose kernel plays it; FM TONE for an SDK machine
 * without a render. */
uint32_t dn2_dsp_type(uint32_t type)
{
    return dn2_is_voice(type) && (!ui_only[type] || sdk_render[type]) ? type : dn2_host_type(type);
}

uint32_t dn2_voice_page_count(uint32_t type)
{
    if (!dn2_is_voice(type))
        return 0;
    if (ui_only[type])
        return dn2_sdk_page_count(type);
    return page_count[type] ? page_count[type] : 1;
}

uintptr_t dn2_voice_page(uint32_t type, uint32_t page)
{
    if (!dn2_is_voice(type))
        return 0;
    if (page >= dn2_voice_page_count(type))
        return (uintptr_t)&dn2_stock_descriptors[4];
    if (ui_only[type])
        return (uintptr_t)dn2_sdk_page(type, page);
    return (uintptr_t)(page_count[type] ? &pages[type][page] : &descriptors[type]);
}

/* The parameter code of a voice's sound slot 25..64, 0 for an empty one. */
uint32_t dn2_voice_code(uint32_t type, uint32_t slot)
{
    uint32_t page = (slot - SLOT) / 8;
    if (dn2_is_voice(type) && ui_only[type])
        return dn2_sdk_code(type, slot);
    if (!dn2_is_voice(type) || slot < SLOT || page >= page_count[type])
        return 0;
    return pages[type][page].params[(slot - SLOT) % 8];
}

/* MIDI CC and NRPN lookups (track, synth, filter, number) -> parameter code. The stock maps
 * know no voice type, so a paged voice asks its host's: a host synth row there stands for its
 * slot, which is the voice's knob in that position (0 when the slot is empty). A voice or a
 * missing ID is otherwise looked up as itself, which no stock synth map has, so its synth CCs
 * never reach a MIDI track's parameters stored in the same slots. */
typedef uint32_t lookup(uint32_t track, uint32_t synth, uint32_t filter, uint32_t key);
extern lookup dn2_stock_cc, dn2_stock_nrpn;
static uint32_t filter_lookup(lookup *stock, uint32_t track, uint32_t synth, uint32_t *filter,
                              uint32_t key);

static uint32_t voice_lookup(lookup *stock, uint32_t track, uint32_t synth, uint32_t filter,
                             uint32_t key)
{
    uint32_t g = filter_lookup(stock, track, synth, &filter, key);
    if (g)
        return g;
    if (dn2_is_voice(synth) && (page_count[synth] || ui_only[synth])) {
        uint32_t code = stock(track, SYNTH_HOST, filter, key);
        if (code < ROWS && dn2_t1[code].owner == SYNTH_HOST && dn2_t1[code].slot >= SLOT
                && dn2_t1[code].slot < SLOT + ADDED)
            return dn2_voice_code(synth, dn2_t1[code].slot);
    }
    return stock(track, synth, filter, key);
}

uint32_t dn2_cc_code(uint32_t track, uint32_t synth, uint32_t filter, uint32_t cc)
{
    return voice_lookup(dn2_stock_cc, track, synth, filter, cc);
}

uint32_t dn2_nrpn_code(uint32_t track, uint32_t synth, uint32_t filter, uint32_t nrpn)
{
    return voice_lookup(dn2_stock_nrpn, track, synth, filter, nrpn);
}

/* An added row's value-class object is its template's. */
uint32_t dn2_value_code(uint32_t code)
{
    return code == G_CODE ? G_TEMPLATE : code >= ROWS && code < T1_END ? TEMPLATE : code;
}

uintptr_t dn2_descriptor(uint32_t type)
{
    if (type < FIRST)
        return (uintptr_t)&dn2_stock_descriptors[type];
    dn2_ensure();
    return (uintptr_t)(type < COUNT && registered[type] ? &descriptors[type]
                                                      : &dn2_stock_descriptors[4]);
}

uintptr_t dn2_name(uint32_t type)
{
    if (type < FIRST)
        return (uintptr_t)dn2_stock_names[type].name;
    dn2_ensure();
    return (uintptr_t)(type < COUNT ? registered[type] ? registered[type]->name : "MISSING"
                                   : dn2_unknown_name);
}

uintptr_t dn2_label(uint32_t type)
{
    if (type < FIRST)
        return (uintptr_t)dn2_stock_names[type].label;
    dn2_ensure();
    return (uintptr_t)(type < COUNT ? registered[type] ? registered[type]->label : "MISS"
                                   : dn2_unknown_label);
}

uintptr_t dn2_hint(uint32_t type)
{
    if (type >= COUNT)
        return (uintptr_t)dn2_unknown_name;
    return (uintptr_t)dn2_stock_names[dn2_host_type(type)].hint;
}

uint32_t dn2_permit(uint32_t type, uint32_t track)
{
    if (type >= COUNT || track >= 16)
        return 0;
    /* The OS also checks this when loading a sound: rejecting it loses the saved ID.
     * Missing IDs stay out of the selector; MIDI permissions let their silent fallback load. */
    type = dn2_host_type(type);
    return ((dn2_stock_permit[type] >> 16) >> track) & 1;
}

extern const struct mk2_filter *const mk2_voice_filters[];
extern const struct names dn2_stock_filter_names[6];
extern const struct descriptor dn2_stock_filter_descriptors[6];
uint32_t dn2_filter_list[FILTER_COUNT], dn2_filter_list_bytes;
static const struct mk2_filter *filters[FILTER_COUNT];
static struct descriptor filter_descriptors[FILTER_COUNT];
static struct string_rep filter_strings[FILTER_COUNT][2];
static uint32_t filters_ready;
static struct string_rep g_label;

/* G's row: MULTI-MODE's slot-66 row, owned by the host. Every filter with a G shares it, labelled for
 * the active track's filter (dn2_rows_follow) and taking its default (dn2_range); an invalid G is
 * ignored. */
static void add_g(const struct mk2_filter *f)
{
    const struct mk2_param *m = f->g;
    if (!m || m->def > 127 || !label_length(m->label))
        return;
    struct row *r = &dn2_t1[G_CODE];
    copy_row(r, &dn2_t1[G_TEMPLATE]);
    r->owner = dn2_t1[G_TEMPLATE].owner + FILTER_HOST;   /* filter rows: 5 + the filter type */
    r->slot = G_SLOT;
    r->def = m->def << 8;
    r->name = r->label = dn2_representation(&g_label, m->label, 5);
    r->owner_name = dn2_t1[G_TEMPLATE].owner_name;
    filter_descriptors[f->id].params[G_CELL] = G_CODE;
}

void dn2_filters_ensure(void)
{
    if (filters_ready)
        return;
    const struct mk2_filter *const *e;
    for (e = mk2_voice_filters; *e; ++e) {
        const struct mk2_filter *f = *e;
        if (f->id < 6 || f->id >= FILTER_COUNT || !f->name || !f->label || filters[f->id])
            continue;
        filters[f->id] = f;
        filter_descriptors[f->id] = dn2_stock_filter_descriptors[FILTER_HOST];
        filter_descriptors[f->id].name = dn2_representation(&filter_strings[f->id][0], f->name, 31);
        filter_descriptors[f->id].label = dn2_representation(&filter_strings[f->id][1], f->label, 7);
        add_g(f);
    }
    const uint32_t stock[] = {0, 1, 4, 3, 5, 2};
    unsigned n = 0;
    for (; n < 6; ++n)
        dn2_filter_list[n] = stock[n];
    for (unsigned id = 6; id < FILTER_COUNT; ++id)
        if (filters[id])
            dn2_filter_list[n++] = id;
    dn2_filter_list_bytes = n * 4;
    filters_ready = 1;
}

/* The filter's G, or 0. */
static const struct mk2_param *g_of(uint32_t type)
{
    dn2_filters_ensure();
    return type < FILTER_COUNT && filters[type] && filter_descriptors[type].params[G_CELL] == G_CODE
           ? filters[type]->g : 0;
}

uint32_t dn2_filter_host_type(uint32_t type)
{
    if (type < 6 || type >= FILTER_COUNT)
        return type;
    dn2_filters_ensure();
    return filters[type] ? FILTER_HOST : 0;
}

uint32_t dn2_filter_dsp_type(uint32_t type)
{
    dn2_filters_ensure();
    return type < FILTER_COUNT && filters[type] ? type : dn2_filter_host_type(type);
}

/* The parameter code of a filter's sound slot that its host has no row for: G's, or 0. */
uint32_t dn2_filter_code(uint32_t slot, uint32_t type)
{
    dn2_filters_ensure();
    return slot == G_SLOT && g_of(type) ? G_CODE : 0;
}

/* The stock maps know no custom filter: it asks its host's, and G's number is the one the
 * stock filters have for slot 66 (MULTI-MODE's). Sets *filter to the type to look up with. */
static uint32_t filter_lookup(lookup *stock, uint32_t track, uint32_t synth, uint32_t *filter,
                              uint32_t key)
{
    if (*filter < 6 || *filter >= FILTER_COUNT)
        return 0;
    dn2_filters_ensure();
    if (!filters[*filter])
        return 0;
    uint32_t code = 0;
    if (g_of(*filter)) {
        code = stock(track, synth, 0, key);
        code = code < ROWS && dn2_t1[code].owner == dn2_t1[G_TEMPLATE].owner
               && dn2_t1[code].slot == G_SLOT ? G_CODE : 0;
    }
    *filter = FILTER_HOST;
    return code;
}

uintptr_t dn2_filter_descriptor(uint32_t type)
{
    if (type < 6)
        return (uintptr_t)&dn2_stock_filter_descriptors[type];
    dn2_filters_ensure();
    return (uintptr_t)(type < FILTER_COUNT && filters[type] ? &filter_descriptors[type]
                                                   : &dn2_stock_filter_descriptors[0]);
}

uintptr_t dn2_filter_name(uint32_t type)
{
    if (type < 6)
        return (uintptr_t)dn2_stock_filter_names[type].name;
    dn2_filters_ensure();
    return (uintptr_t)(type < FILTER_COUNT ? filters[type] ? filters[type]->name : "MISSING"
                                   : dn2_unknown_name);
}

uintptr_t dn2_filter_label(uint32_t type)
{
    if (type < 6)
        return (uintptr_t)dn2_stock_filter_names[type].label;
    dn2_filters_ensure();
    return (uintptr_t)(type < FILTER_COUNT ? filters[type] ? filters[type]->label : "MISS"
                                   : dn2_unknown_label);
}

/* The stock pixel writer the page's line drawing uses: bounds-checked, on its canvas; and the
 * page's shown value of a parameter, a lock being edited included, as it reads FREQ and RESO. */
extern void dn2_pixel(void *canvas, int32_t x, int32_t y, int32_t on);
extern uint32_t dn2_shown(void *page, uint32_t code, uint8_t *flag);

/* A custom filter's response graph: canvas x 21..71, y 7..23 from the bottom (screen y 56..40). */
void dn2_filter_draw_curve(uint32_t type, void *canvas, uint32_t freq, uint32_t reso, void *page)
{
    if (type >= FILTER_COUNT)
        return;
    dn2_filters_ensure();
    const struct mk2_filter *f = filters[type];
    if (!f || !f->curve)
        return;
    uint8_t y[51], flag;
    for (unsigned x = 0; x < 51; ++x)
        y[x] = 0xff;
    f->curve(freq, reso, g_of(type) ? dn2_shown(page, G_CODE, &flag) : 0, y);
    for (unsigned x = 0; x < 51; ++x)
        if (y[x] != 0xff)
            dn2_pixel(canvas, 21 + x, 7 + (y[x] > 16 ? 16 : y[x]), 1);
}

/* The active track's voice's knob in sound slot `slot`, or 0. */
static const struct mk2_param *active_knob(uint32_t slot)
{
    uint32_t type = dn2_active_type(0), i = slot - SLOT;
    const struct mk2_voice_pages *p = dn2_is_voice(type) && type < VOICE_COUNT ? voice_pages[type] : 0;
    return p && i / 8 < p->count ? p->page[i / 8].knob[i % 8] : 0;
}

/* ev_tick: the shared rows' labels and owner for the active track's voice and filter. */
void dn2_rows_follow(void)
{
    static int32_t voice = -1, filter = -1;
    uint32_t type = dn2_active_type(0), f = dn2_active_type(1);
    if ((int32_t)type != voice && dn2_is_voice(type) && page_count[type]) {
        for (uint32_t code = ROWS; code < ROWS + ADDED; ++code) {
            const struct mk2_param *m = active_knob(SLOT + code - ROWS);
            if (m)
                dn2_row_label(code, m->label, strings[type][0].text);
        }
        voice = type;
    }
    const struct mk2_param *g = g_of(f);
    if ((int32_t)f != filter && g) {
        dn2_representation(&g_label, g->label, 5);
        filter = f;
    }
}

/* The stock range accessor's {min, max, default} of a parameter code (hooks.s), the row's own, but
 * the shared rows take the default of the active track's machine: stock reads it after a selection
 * has written the track's new machine, and when a page or a sound is cleared. */
void dn2_range(int32_t *out, uint32_t code)
{
    const struct row *r = &dn2_t1[code < T1_END ? code : 0];
    int32_t def = r->def;
    if (code >= ROWS && code < ROWS + ADDED) {
        const struct mk2_param *m = active_knob(SLOT + code - ROWS);
        def = m ? (int32_t)m->def << 8 : def;
    } else if (code == G_CODE) {
        const struct mk2_param *g = g_of(dn2_active_type(1));
        def = g ? (int32_t)g->def << 8 : def;
    } else if (code >= ROWS + SDK_ROW && code < T1_END) {
        int32_t d = dn2_sdk_default(code - ROWS - SDK_ROW);
        def = d >= 0 ? d : def;
    }
    out[0] = r->min;
    out[1] = r->max;
    out[2] = def;
}
