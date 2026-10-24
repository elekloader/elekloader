/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * settings-ot: the Octatrack's settings companion (core-ot 0.6). Mods declare settings in core's
 * ot_settings table (struct ot_setting, elekloader/sdk/include/octatrack/core.h); this mod
 *
 * - shows them in a fifth MAIN MENU category, MODS, beside PROJECT, SYSTEM, CONTROL and MIDI: a
 *   heading per mod, then its settings with the value in the row. YES steps a value; an encoder
 *   turns it;
 * - keeps them with core's project and card data: card-scope ones in core_carddata ("SETC"),
 *   project-scope ones in core_projdata ("SETP"), each as {tag, key, value} entries, so a value
 *   survives builds that leave the mod out or reorder the settings; an early one also in a CS1
 *   claim ("SETE"), read at ev_boot_done, before the card is mounted.
 *
 * The menu's facts are octabam's (MIT, docs/firmware/MAINMENU.md: the root takes a fifth category
 * through its rows pointer and count, and octalab's ran on an MKI); BRAIN's design (a settings
 * registry, values keyed by store and key, defaults when absent) is the model. The code is ours.
 */
#include <stdint.h>
#include "octatrack/core.h"

#define U32(a)          (*(volatile uint32_t *)(uintptr_t)(a))
#define MENU_WINDOW     U32(0x400cbf4c)     /* the MAIN MENU's window, 0 while it is closed */
#define MENU_FOCUS      U32(0x400cbda8)     /* the list descriptor the cursor is in */
#define STOCK_ROOT_ROWS 0x400cc698u         /* PROJECT, SYSTEM, CONTROL, MIDI: 4 x 0x18 */
#define MENU_REDRAW     ((void (*)(void))0x40064d7c)
#define STOCK_RTS       0x400648f8u         /* the shared no-op action */

#define MAXSET          32
#define MAXROW          (2 * MAXSET)        /* a heading per mod at most, then its settings */
#define NENT            64                  /* entries in a stored block */
#define LABEL           24
#define VALUE           8

extern const struct ot_setting *const ot_settings[];

struct row { const char *label; const void *window; void (*action)(int); void *getter; void *child; uint32_t page; };
struct desc { uint32_t count, scroll, cursor, sel, visible, count2; struct row *rows; };

/* The root's five rows (the stock four, copied at boot, then MODS), and our list. */
struct row ots_root_rows[5];
static struct row rows[MAXROW];
struct desc ots_list = { 0, 0, 0, 0, 7, 0, rows };
static char labels[MAXROW][LABEL];
static char vals[MAXROW][VALUE];
static const struct ot_setting *row_set[MAXROW];    /* the setting a row edits, or 0: a heading */

/* The menu draws a row's value from its getter, called with nothing, at the pane's right edge:
 * one getter per row, each returning its row's string (as an integer: the firmware reads d0). */
#define G(n)            static uint32_t g##n(void) { return (uint32_t)(uintptr_t)vals[n]; }
#define G8(n)           G(n##0) G(n##1) G(n##2) G(n##3) G(n##4) G(n##5) G(n##6) G(n##7)
G8(0) G8(1) G8(2) G8(3) G8(4) G8(5) G8(6) G8(7)
#define P8(n)           g##n##0, g##n##1, g##n##2, g##n##3, g##n##4, g##n##5, g##n##6, g##n##7
static uint32_t (*const getters[MAXROW])(void) = { P8(0), P8(1), P8(2), P8(3), P8(4), P8(5), P8(6), P8(7) };

/* MODS' icon, 19 x 9: a chip, the same top to bottom (which bit is the top row is not settled). */
static const uint32_t icon[2][19] = {
    { 0, 0, 0x7f000000, 0x41000000, 0xc1800000, 0x41000000, 0x41000000, 0xc1800000, 0x41000000,
      0x5d000000, 0x41000000, 0xc1800000, 0x41000000, 0x41000000, 0xc1800000, 0x41000000,
      0x7f000000, 0, 0 },
    { 0xff800000, 0xff800000, 0xff800000, 0xff800000, 0xff800000, 0xff800000, 0xff800000,
      0xff800000, 0xff800000, 0xff800000, 0xff800000, 0xff800000, 0xff800000, 0xff800000,
      0xff800000, 0xff800000, 0xff800000, 0xff800000, 0xff800000 },
};
static const uint32_t window[5] = { 0x13, 0x09, 0x01, (uint32_t)(uintptr_t)icon[0], (uint32_t)(uintptr_t)icon[1] };

/* ---- the stored values: {tag, key | used << 16, value} --------------------------------------- */
struct ent { uint32_t tag; uint32_t key; int32_t value; };
static struct ent card_ents[NENT], proj_ents[NENT];
static struct ent *early_ents;              /* CS1, 16 entries */
#define NEARLY          16
#define USED            0x10000u

static void card_loaded(int32_t found);
static void proj_loaded(int32_t found);
const struct core_projdata ots_card_block = { OT_TAG('S', 'E', 'T', 'C'), sizeof card_ents, card_ents, card_loaded };
const struct core_projdata ots_proj_block = { OT_TAG('S', 'E', 'T', 'P'), sizeof proj_ents, proj_ents, proj_loaded };
const struct ot_sram ots_early_claim = { OT_TAG('S', 'E', 'T', 'E'), NEARLY * sizeof(struct ent), (void **)&early_ents };

static int valid(const struct ot_setting *s)
{
    return s->abi == OT_SETTING_ABI && s->value && s->name;
}

static int32_t lo(const struct ot_setting *s) { return s->kind == OT_SET_NUMBER ? s->min : 0; }
static int32_t hi(const struct ot_setting *s) { return s->kind == OT_SET_BINARY ? 1 : s->max; }

static struct ent *find(struct ent *e, unsigned n, const struct ot_setting *s, int make)
{
    for (unsigned k = 0; k < n; ++k)
        if ((e[k].key & USED) && e[k].tag == s->tag && (e[k].key & 0xffff) == s->key)
            return &e[k];
    if (make)
        for (unsigned k = 0; k < n; ++k)
            if (!(e[k].key & USED)) {
                e[k].tag = s->tag;
                e[k].key = s->key | USED;
                return &e[k];
            }
    return 0;
}

/* ---- the menu rows ---------------------------------------------------------------------------- */
static void put_num(char *d, int32_t v)
{
    char t[12];
    int n = 0;
    uint32_t u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    do t[n++] = '0' + u % 10; while (u /= 10);
    if (v < 0)
        t[n++] = '-';
    while (n)
        *d++ = t[--n];
    *d = 0;
}

/* A row's label (its setting's name) and value (OFF/ON, an option's label, a number). */
static void label_for(unsigned r)
{
    const struct ot_setting *s = row_set[r];
    char *v = vals[r];
    unsigned j;
    int32_t x = *s->value;
    for (j = 0; s->name[j] && j < LABEL - 1; ++j)
        labels[r][j] = s->name[j];
    labels[r][j] = 0;
    if (s->kind == OT_SET_BINARY) {
        const char *t = x ? "ON" : "OFF";
        for (j = 0; t[j]; ++j) v[j] = t[j];
        v[j] = 0;
    } else if (s->kind == OT_SET_OPTION && s->labels && x >= 0 && x <= s->max) {
        const char *t = s->labels[x];
        for (j = 0; t[j] && j < VALUE - 1; ++j) v[j] = t[j];
        v[j] = 0;
    } else {
        put_num(v, x);
    }
}

static void set_value(const struct ot_setting *s, int32_t v, int store)
{
    if (v < lo(s)) v = lo(s);
    if (v > hi(s)) v = hi(s);
    *s->value = v;
    if (store) {
        struct ent *e = find((s->scope & 1) ? proj_ents : card_ents, NENT, s, 1);
        if (e)
            e->value = v;
        if ((s->scope & 1) == OT_SET_PROJECT)
            core_projdata_dirty(ots_proj_block.tag);
        else
            core_carddata_dirty(ots_card_block.tag);
        if ((s->scope & OT_SET_EARLY) && early_ents) {
            struct ent *x = find(early_ents, NEARLY, s, 1);
            if (x)
                x->value = v;
        }
    }
    for (unsigned r = 0; r < ots_list.count; ++r)
        if (row_set[r] == s)
            label_for(r);
    if (s->changed)
        s->changed(s);
}

/* A row's YES: the selected row's value one step on (wrapping). */
static void row_action(int unused)
{
    (void)unused;
    unsigned r = ots_list.sel;
    const struct ot_setting *s = r < ots_list.count ? row_set[r] : 0;
    if (!s)
        return;
    int32_t v = *s->value + 1;
    set_value(s, v > hi(s) ? lo(s) : v, 1);
}

/* ev_enc: an encoder turns the selected row's value while the cursor is in MODS. */
int ots_settings_enc(int encoder, int delta)
{
    (void)encoder;
    if (!MENU_WINDOW || MENU_FOCUS != (uint32_t)(uintptr_t)&ots_list)
        return 0;
    unsigned r = ots_list.sel;
    const struct ot_setting *s = r < ots_list.count ? row_set[r] : 0;
    if (s && delta) {
        int8_t d = (int8_t)delta;
        set_value(s, *s->value + d, 1);
        MENU_REDRAW();
    }
    return 1;
}

/* ---- loading -------------------------------------------------------------------------------- */
static void apply(struct ent *e, unsigned n, unsigned scope, int found)
{
    for (const struct ot_setting *const *p = ot_settings; *p; ++p) {
        const struct ot_setting *s = *p;
        if (!valid(s) || (s->scope & 1) != scope)
            continue;
        struct ent *x = found ? find(e, n, s, 0) : 0;
        int32_t v = x && x->value >= lo(s) && x->value <= hi(s) ? x->value : s->def;
        set_value(s, v, 0);
        if ((s->scope & OT_SET_EARLY) && early_ents) {
            struct ent *y = find(early_ents, NEARLY, s, 1);
            if (y)
                y->value = v;
        }
    }
}

static void card_loaded(int32_t found) { apply(card_ents, NENT, OT_SET_CARD, found); }
static void proj_loaded(int32_t found) { apply(proj_ents, NENT, OT_SET_PROJECT, found); }

/* ev_boot_done: the rows, the root, and the early values (core has given the CS1 claim by now). */
void ots_settings_boot(void)
{
    unsigned n = 0;
    const char *last = 0;
    for (const struct ot_setting *const *p = ot_settings; *p && n + 2 <= MAXROW; ++p) {
        const struct ot_setting *s = *p;
        if (!valid(s))
            continue;
        *s->value = s->def;
        if (s->group && (!last || last != s->group)) {
            char *d = labels[n];
            unsigned i = 0;
            d[i++] = 0x17, d[i++] = 0x17;
            for (unsigned j = 0; s->group[j] && i < LABEL - 3; ++j)
                d[i++] = s->group[j];
            while (i < LABEL - 1)
                d[i++] = 0x17;
            d[i] = 0;
            rows[n] = (struct row){ labels[n], 0, 0, 0, 0, 0 };    /* a heading: no action */
            row_set[n++] = 0;
            last = s->group;
        }
        rows[n] = (struct row){ labels[n], 0, row_action, (void *)getters[n], 0, 0 };
        row_set[n] = s;
        label_for(n++);
    }
    if (early_ents)
        for (const struct ot_setting *const *p = ot_settings; *p; ++p)
            if (valid(*p) && ((*p)->scope & OT_SET_EARLY)) {
                struct ent *x = find(early_ents, NEARLY, *p, 0);
                if (x && x->value >= lo(*p) && x->value <= hi(*p))
                    set_value(*p, x->value, 0);
            }
    if (!n) {                               /* no setting in the build: one row that does nothing */
        rows[0] = (struct row){ "NO SETTINGS", 0, (void (*)(int))STOCK_RTS, 0, 0, 0 };
        row_set[0] = 0;
        n = 1;
    }
    ots_list.count = ots_list.count2 = n;
    ots_list.scroll = ots_list.cursor = ots_list.sel = 0;
    ots_list.visible = 7;
    const struct row *stock = (const struct row *)STOCK_ROOT_ROWS;
    for (unsigned k = 0; k < 4; ++k)
        ots_root_rows[k] = stock[k];
    ots_root_rows[4] = (struct row){ "MODS", window, 0, 0, &ots_list, 0 };
}
