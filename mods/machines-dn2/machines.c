/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include "mk2/machines.h"

#define FIRST 5
#define COUNT 32
struct names { const char *name, *label, *hint; };
struct descriptor { const char *name, *label; uint32_t params[9]; };
struct string_rep { uint32_t length, capacity; int32_t references; char text[32]; };
/* The stock parameter table's 60-byte row; the bound sites in mod.json allow ROWS + ADDED. */
struct row {
    int32_t owner, slot, min, max, def, flag;
    int16_t cc, cc_low;
    uint32_t kind, unknown, mask;
    const char *name, *owner_name, *label;
    uint32_t format, end;
};
#define ROWS 321
#define ADDED 40                    /* five pages of eight knobs */
#define TEMPLATE 0xd5               /* A Env Decay: a plain 0..127 knob */
#define SLOT 25
#define PAGE_END 10                 /* Track Level ends a stock page */

extern const struct mk2_synth *const mk2_synths[];
extern const struct mk2_synth *const mk2_voice_synths[];
extern const struct mk2_voice_pages *const mk2_voice_pages[];
extern const struct names dn2_stock_names[5];
extern const uint32_t dn2_stock_permit[5];
extern const struct descriptor dn2_stock_descriptors[5];
extern const char dn2_unknown_name[], dn2_unknown_label[];

uint32_t dn2_list[COUNT], dn2_list_bytes;
static const struct mk2_synth *registered[COUNT];
static uint8_t voices[COUNT];
static struct descriptor descriptors[COUNT];
static struct string_rep strings[COUNT][2];
static uint32_t ready;
struct row dn2_t1[ROWS + ADDED];    /* the stock rows are copied in at boot (start.s) */
static struct descriptor pages[ADDED / 8];
static uint8_t first_page[COUNT], page_count[COUNT];
static struct string_rep tabs[ADDED / 8];
static struct string_rep labels[ADDED];
static unsigned used_pages;

static const char *representation(struct string_rep *r, const char *s, unsigned max)
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
    if (p->id >= COUNT || !registered[p->id] || !voices[p->id] || page_count[p->id]
            || !p->page || !p->count || used_pages + p->count > ADDED / 8)
        return 0;
    for (unsigned i = 0; i < p->count; ++i)
        for (unsigned k = 0; k < 8; ++k) {
            const struct mk2_param *m = p->page[i].knob[k];
            if (m && (m->def > 127 || !label_length(m->label)))
                return 0;
        }
    return 1;
}

/* The MIDI CC the clone's stock row for `slot` has, or -1: a knob takes its position's CC. */
static int16_t clone_cc(int32_t clone, int32_t slot)
{
    for (unsigned code = 1; code < ROWS; ++code)
        if (dn2_t1[code].owner == clone && dn2_t1[code].slot == slot)
            return dn2_t1[code].cc;
    return -1;
}

/* Each declared knob gets a parameter row after the stock ones, copied from TEMPLATE. */
static void build_pages(void)
{
    const struct mk2_voice_pages *const *e;
    for (e = mk2_voice_pages; *e; ++e) {
        const struct mk2_voice_pages *p = *e;
        if (!valid_pages(p))
            continue;
        first_page[p->id] = used_pages;
        page_count[p->id] = p->count;
        for (unsigned i = 0; i < p->count; ++i, ++used_pages) {
            const char tab[] = {'S', 'Y', 'N', '1' + i, 0};
            struct descriptor *d = &pages[used_pages];
            d->name = representation(&tabs[used_pages], tab, 4);
            d->label = strings[p->id][0].text;
            for (unsigned k = 0; k < 8; ++k) {
                const struct mk2_param *m = p->page[i].knob[k];
                unsigned code = ROWS + 8 * used_pages + k;
                d->params[k] = m ? code : 0;
                if (!m)
                    continue;
                struct row *r = &dn2_t1[code];
                const struct row *t = &dn2_t1[TEMPLATE];      /* field by field: a struct copy is a call to memcpy */
                r->flag = t->flag;
                r->cc_low = t->cc_low;
                r->kind = t->kind;
                r->unknown = t->unknown;
                r->mask = t->mask;
                r->format = t->format;
                r->end = t->end;
                r->owner = registered[p->id]->clone;
                r->slot = SLOT + 8 * i + k;
                r->min = 0;
                r->max = 127 << 8;
                r->def = m->def << 8;
                r->cc = clone_cc(r->owner, r->slot);
                r->name = r->label = representation(&labels[code - ROWS], m->label, 5);
                r->owner_name = strings[p->id][0].text;
            }
            d->params[8] = PAGE_END;
        }
    }
}

void dn2_ensure(void)
{
    if (ready)
        return;
    const struct mk2_synth *const *e;
    for (unsigned voice = 0; voice < 2; ++voice)
    for (e = voice ? mk2_voice_synths : mk2_synths; *e; ++e) {
        const struct mk2_synth *s = *e;
        if (s->id < FIRST || s->id >= COUNT || s->clone > 3 || !s->name || !s->label
                || registered[s->id])
            continue;
        registered[s->id] = s;
        voices[s->id] = voice;
        descriptors[s->id] = dn2_stock_descriptors[s->clone];
        if (voice)
            for (unsigned i = 0; i < 9; ++i)
                descriptors[s->id].params[i] = 0;
        descriptors[s->id].name = representation(&strings[s->id][0], s->name, 31);
        descriptors[s->id].label = representation(&strings[s->id][1], s->label, 7);
    }
    build_pages();
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

uint32_t dn2_clone_type(uint32_t type)
{
    if (type < FIRST || type >= COUNT)
        return type;
    dn2_ensure();
    /* A missing mod keeps its saved identity, but sends MIDI's silent type to the DSP. */
    return registered[type] ? registered[type]->clone : 4;
}

uint32_t dn2_is_voice(uint32_t type)
{
    dn2_ensure();
    return type < COUNT && voices[type];
}

uint32_t dn2_dsp_type(uint32_t type)
{
    return dn2_is_voice(type) ? type : dn2_clone_type(type);
}

uint32_t dn2_voice_page_count(uint32_t type)
{
    if (!dn2_is_voice(type))
        return 0;
    return page_count[type] ? page_count[type] : 1;
}

/* The synth type MIDI parameter lookups see: a clone's stock type; a voice or a missing ID its
 * own, which no stock synth map has, so their synth CCs reach only a paged voice's knobs and never
 * a MIDI track's parameters stored in the same slots. */
uint32_t dn2_midi_type(uint32_t type)
{
    dn2_ensure();
    return type >= FIRST && type < COUNT && registered[type] && !voices[type]
           ? registered[type]->clone : type;
}

uintptr_t dn2_voice_page(uint32_t type, uint32_t page)
{
    if (!dn2_is_voice(type))
        return 0;
    if (page >= dn2_voice_page_count(type))
        return (uintptr_t)&dn2_stock_descriptors[4];
    return (uintptr_t)(page_count[type] ? &pages[first_page[type] + page] : &descriptors[type]);
}

/* The parameter code of a voice's sound slot 25..64, 0 for an empty one. */
uint32_t dn2_voice_code(uint32_t type, uint32_t slot)
{
    uint32_t page = (slot - SLOT) / 8;
    if (!dn2_is_voice(type) || slot < SLOT || page >= page_count[type])
        return 0;
    return pages[first_page[type] + page].params[(slot - SLOT) % 8];
}

/* MIDI CC and NRPN lookups (track, synth, filter, number) -> parameter code. The stock maps
 * know no voice type, so a paged voice asks its clone's: a clone synth row there stands for its
 * slot, which is the voice's knob in that position (0 when the slot is empty). */
typedef uint32_t lookup(uint32_t track, uint32_t synth, uint32_t filter, uint32_t key);
extern lookup dn2_stock_cc, dn2_stock_nrpn;

static uint32_t voice_lookup(lookup *stock, uint32_t track, uint32_t synth, uint32_t filter,
                             uint32_t key)
{
    if (dn2_is_voice(synth) && page_count[synth]) {
        int32_t clone = registered[synth]->clone;
        uint32_t code = stock(track, clone, filter, key);
        if (code < ROWS && dn2_t1[code].owner == clone && dn2_t1[code].slot >= SLOT
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
    return code >= ROWS && code < ROWS + ADDED ? TEMPLATE : code;
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
    return (uintptr_t)dn2_stock_names[dn2_clone_type(type)].hint;
}

uint32_t dn2_permit(uint32_t type, uint32_t track)
{
    if (type >= COUNT || track >= 16)
        return 0;
    if (type >= FIRST) {
        dn2_ensure();
        /* The OS also checks this when loading a sound: rejecting it loses the saved ID.
         * Missing IDs stay out of the selector; MIDI permissions let their silent fallback load. */
        type = registered[type] ? registered[type]->clone : 4;
    }
    return ((dn2_stock_permit[type] >> 16) >> track) & 1;
}

extern const struct mk2_filter *const mk2_filters[];
extern const struct mk2_filter *const mk2_voice_filters[];
extern const struct names dn2_stock_filter_names[6];
extern const struct descriptor dn2_stock_filter_descriptors[6];
uint32_t dn2_filter_list[COUNT], dn2_filter_list_bytes;
static const struct mk2_filter *filters[COUNT];
static uint8_t filter_voices[COUNT];
static struct descriptor filter_descriptors[COUNT];
static struct string_rep filter_strings[COUNT][2];
static uint32_t filters_ready;

void dn2_filters_ensure(void)
{
    if (filters_ready)
        return;
    const struct mk2_filter *const *e;
    for (unsigned voice = 0; voice < 2; ++voice)
    for (e = voice ? mk2_voice_filters : mk2_filters; *e; ++e) {
        const struct mk2_filter *f = *e;
        if (f->id < 6 || f->id >= COUNT || f->clone >= 6 || !f->name || !f->label
                || filters[f->id])
            continue;
        filters[f->id] = f;
        filter_voices[f->id] = voice;
        filter_descriptors[f->id] = dn2_stock_filter_descriptors[f->clone];
        if (voice)
            for (unsigned i = 0; i < 9; ++i)
                filter_descriptors[f->id].params[i] = 0;
        filter_descriptors[f->id].name = representation(&filter_strings[f->id][0], f->name, 31);
        filter_descriptors[f->id].label = representation(&filter_strings[f->id][1], f->label, 7);
    }
    const uint32_t stock[] = {0, 1, 4, 3, 5, 2};
    unsigned n = 0;
    for (; n < 6; ++n)
        dn2_filter_list[n] = stock[n];
    for (unsigned id = 6; id < COUNT; ++id)
        if (filters[id])
            dn2_filter_list[n++] = id;
    dn2_filter_list_bytes = n * 4;
    filters_ready = 1;
}

uint32_t dn2_filter_clone(uint32_t type)
{
    if (type < 6 || type >= COUNT)
        return type;
    dn2_filters_ensure();
    return filters[type] ? filters[type]->clone : 0;
}

uint32_t dn2_filter_dsp_type(uint32_t type)
{
    dn2_filters_ensure();
    return type < COUNT && filter_voices[type] ? type : dn2_filter_clone(type);
}

uintptr_t dn2_filter_descriptor(uint32_t type)
{
    if (type < 6)
        return (uintptr_t)&dn2_stock_filter_descriptors[type];
    dn2_filters_ensure();
    return (uintptr_t)(type < COUNT && filters[type] ? &filter_descriptors[type]
                                                   : &dn2_stock_filter_descriptors[0]);
}

uintptr_t dn2_filter_name(uint32_t type)
{
    if (type < 6)
        return (uintptr_t)dn2_stock_filter_names[type].name;
    dn2_filters_ensure();
    return (uintptr_t)(type < COUNT ? filters[type] ? filters[type]->name : "MISSING"
                                   : dn2_unknown_name);
}

uintptr_t dn2_filter_label(uint32_t type)
{
    if (type < 6)
        return (uintptr_t)dn2_stock_filter_names[type].label;
    dn2_filters_ensure();
    return (uintptr_t)(type < COUNT ? filters[type] ? filters[type]->label : "MISS"
                                   : dn2_unknown_label);
}
