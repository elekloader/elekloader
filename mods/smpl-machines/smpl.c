/* SPDX-License-Identifier: GPL-2.0-or-later */
/* smpl-machines: SMPL machines on the Analog Rytm MKII (README.md;
 * analog-rytm-mk2/smpl.h for the mods that add them).
 *
 *   - which machine a sound runs: the field smpl_choice (0 AUTO, 1 stock,
 *     2 + id a machine), the SRC machine's bind, the voice. The render reads
 *     it from the sound the audio side last put on each track (gate.s, at
 *     0x4011f60a: the kit's sound, or a sound lock's pool sound), and the UI
 *     from the current kit's sounds, cached each UI tick (smpl_tick);
 *   - the SMPL page: core_page_fns for stock's pages 4 and 5 (the title),
 *     core_knob_faces for records 39..46 (label, name, look, range), and
 *     core_params' text for them (the value);
 *   - the choice: a SMPL MACHINE row under MACHINE in the sound setup menu
 *     (gate.s, at the menu's build, 0x400ef184);
 *   - the render (gate.s, the call of the sample layer's trig intake): a
 *     voice whose track runs a machine starts no sample and stops any it
 *     plays, and the machine's engine gets the words. */
#include <stdint.h>
#include "analog-rytm-mk2/pages.h"
#include "analog-rytm-mk2/store.h"
#include "analog-rytm-mk2/moments.h"
#include "analog-rytm-mk2/machines.h"
#include "analog-rytm-mk2/smpl.h"
#include "analog-rytm-mk2/lock.h"     /* core_param_value: knob C's own value */

#define FIRST_REC 39                    /* the SMPL page's records: sound indices 17..24 */
#define LAST_REC 46
#define MAX_MACHINES 30                 /* the pool's ids: 0..29 (choice 2..31) */
#define CHOICE_AUTO 0
#define CHOICE_STOCK 1
#define CHOICE_FIRST 2
#define SOUND_SIZE 152                  /* a live sound */
#define SOUND_MACHINE 104               /* its SRC machine, a byte */
#define KIT_SOUNDS 42                   /* a live kit's first sound */
#define OWNERS ((const volatile uint8_t *)0x40291138)   /* the track each voice plays, by voice */
#define PLAYING ((const volatile int8_t *)0x8000ea00)   /* the SRC machine the audio side plays, by track */
#define WORDS(t) ((const int16_t *)(0x8000f7a8 + 0x54 * (t)))   /* a track's engine words, by sound index */
#define STOCK_PAGE_SAMPLE 4             /* the SAMPLE view's pages: 4, and 5 (the same ids) */
#define STOCK_PAGE_SAMPLE2 5
#define PLAIN_DIAL 42                   /* BR's record: a plain 0..127 dial, the look of a knob
                                         * with its own range and no look (the record under it
                                         * may draw on another range: TUN's is +-24) */

STORE_FIELD(smpl_choice);

/* knob C: SMP's word (index 19), the sample stock plays, unless the machine
 * asks for a value of its own (SMPL_OWN_C) and the companion smpl-knob-c is
 * in the build: then its knob id and lockable sound field (smpl.h), so
 * p-locks, scenes, performances, the LFO, routes and MIDI come from core
 * and the sample stays put. Without the companion the record reads zeros. */
#define KNOB_C 2
extern const struct smpl_knob_c smpl_knob_c;   /* weak: smpl-knob-c's */
#define KNOB_C_ID (smpl_knob_c.id)

static int own_c(const struct smpl_machine *m)
{
    return m && (m->flags & SMPL_OWN_C) && smpl_knob_c.id && m->knobs[KNOB_C].label;
}

extern const struct smpl_machine *const smpl_machines[];   /* zero-terminated */

/* the voice each track plays on: BD SD RS CP BT LT MT HT CH OH CY CB */
static const uint8_t track_voice[12] = {0, 2, 7, 7, 1, 3, 5, 5, 4, 4, 6, 6};

static volatile uint8_t track_entry[12];         /* the table index + 1 each track runs; 0 stock */
static const unsigned char *volatile applied[12]; /* the sound the audio side last put on each track */
static int n_machines = -1;                       /* -1 until the first tick */
static struct core_knob_face faces[MAX_MACHINES][8];
static struct core_page pages[MAX_MACHINES];

static int count_machines(void)
{
    int n = 0;
    while (n < MAX_MACHINES && smpl_machines[n])
        n++;
    return n;
}

static const struct smpl_machine *bound(int machine, int bind)
{
    int i;
    for (i = 0; i < n_machines; i++)
        if (smpl_machines[i]->bind == bind && smpl_machines[i]->src == machine)
            return smpl_machines[i];
    return 0;
}

static int index_of(const struct smpl_machine *m)
{
    int i;
    for (i = 0; i < n_machines; i++)
        if (smpl_machines[i] == m)
            return i;
    return -1;
}

static int fits(const struct smpl_machine *m, int track)
{
    return m && track >= 0 && track < 12 && (m->voices >> track_voice[track] & 1);
}

static const struct smpl_machine *by_id(int id)
{
    int i;
    for (i = 0; i < n_machines; i++)
        if (smpl_machines[i]->id == id)
            return smpl_machines[i];
    return 0;
}

/* a choice the menu row and the list offer: not a SMPL_REQUIRED machine,
 * which only its SRC machine runs */
static int offered(const struct smpl_machine *m, int track)
{
    return m && m->bind != SMPL_REQUIRED && fits(m, track);
}

/* the machine sound s (a live sound item of track t) runs, or 0 for stock */
static const struct smpl_machine *machine_of(const unsigned char *s, int track)
{
    const struct smpl_machine *m;
    unsigned c;
    int src;
    if (!s || n_machines <= 0)
        return 0;
    src = (signed char)s[SOUND_MACHINE];
    if ((m = bound(src, SMPL_REQUIRED)) != 0)
        return fits(m, track) ? m : 0;
    c = core_store_get(STORE(smpl_choice), (void *)s, 0);
    if (c == CHOICE_AUTO)
        m = bound(src, SMPL_DEFAULT);
    else if (c == CHOICE_STOCK)
        m = 0;
    else
        m = by_id((int)c - CHOICE_FIRST);
    return fits(m, track) ? m : 0;
}

/* the track a live sound item is in the current kit, or -1 */
static int track_of(const unsigned char *s)
{
    const unsigned char *kit = core_current_kit_item();
    unsigned o;
    if (!kit || !s || s < kit + KIT_SOUNDS)
        return -1;
    o = (unsigned)(s - kit - KIT_SOUNDS);
    return o % SOUND_SIZE || o / SOUND_SIZE >= 12 ? -1 : (int)(o / SOUND_SIZE);
}

static const struct smpl_machine *current_machine(void)
{
    return machine_of(core_current_sound_item(), core_current_track());
}

/* a knob whose own range differs from the record's under it: the record's
 * look draws on the record's range (TUN's +-24 stops short at 127), so
 * it takes a plain dial; one with the record's range keeps the record's look
 * (LEV's bar) */
#define RECORD_RANGE(id) ((const int32_t *)(0x401bfbc8 + 0x34 * (id) + 8))
static int misdrawn(int32_t id, const struct smpl_knob *n)
{
    const int32_t *r = RECORD_RANGE(id);
    return n->min < n->max && (n->min != r[0] || n->max != r[1]);
}

static void build(void)
{
    int i, k;
    n_machines = count_machines();
    for (i = 0; i < n_machines; i++) {
        const struct smpl_machine *m = smpl_machines[i];
        pages[i].page = STOCK_PAGE_SAMPLE;
        pages[i].name = m->name;
        for (k = 0; k < 8; k++) {
            const struct smpl_knob *n = &m->knobs[k];
            struct core_knob_face *f = &faces[i][k];
            pages[i].ids[k] = !n->label ? 0 : k == KNOB_C && own_c(m) ? KNOB_C_ID : FIRST_REC + k;  /* 0: an empty cell */
            f->label = n->label ? n->label : "";
            f->name = n->name ? n->name : f->label;
            f->look = n->look || (k == KNOB_C && own_c(m)) || !misdrawn(FIRST_REC + k, n) ? n->look : PLAIN_DIAL;
            f->ui = 0;
            f->min = n->min;
            f->max = n->max;
            f->def = n->def;
        }
    }
}

/* ev_tick: the pages and faces once, and the
 * track cache the render reads */
void smpl_tick(void)
{
    const unsigned char *kit;
    int t;
    if (n_machines < 0) {
        build();
    }
    kit = core_current_kit_item();
    for (t = 0; t < 12; t++) {
        const struct smpl_machine *m = kit ? machine_of(kit + KIT_SOUNDS + SOUND_SIZE * t, t) : 0;
        track_entry[t] = (uint8_t)(index_of(m) + 1);
    }
}

/* gate.s, at the entry of the audio side's routine that puts a sound on a
 * track (0x4011f60a: a kit's sound, or a sound lock's pool sound) */
void smpl_apply(const unsigned char *sound, int32_t track)
{
    if ((uint32_t)track < 12)
        applied[track] = sound;
}

/* the machine track t plays now: the sound the audio side put on it, while
 * that sound's SRC machine is the one playing (stock refuses one that doesn't
 * fit the track), else the current kit's */
static int entry_now(unsigned t)
{
    const unsigned char *s = applied[t];
    if (s && n_machines > 0 && (signed char)s[SOUND_MACHINE] == PLAYING[t])
        return index_of(machine_of(s, (int)t)) + 1;
    return track_entry[t];
}

const struct smpl_machine *smpl_track_machine(int32_t track)
{
    int e = (uint32_t)track < 12 ? entry_now((unsigned)track) : 0;
    return e > 0 && e <= n_machines ? smpl_machines[e - 1] : 0;
}

/* gate.s, at the render's call of the sample layer's trig intake: masks[0]
 * the voices that start a sample this block, masks[1] those that stop */
void smpl_gate(uint32_t *masks)
{
    int v;
    for (v = 0; v < 8; v++) {
        unsigned t = OWNERS[v];
        int e = t < 12 ? entry_now(t) : 0;
        const struct smpl_machine *m;
        if (e <= 0 || e > n_machines)
            continue;
        m = smpl_machines[e - 1];
        if (m->engine) {
            int16_t w[8];
            int k, c;
            for (k = 0; k < 8; k++)
                w[k] = WORDS(t)[17 + k];
            if (own_c(m) && (c = core_param_value((int)t, smpl_knob_c.field)) != CORE_PARAM_NONE)
                w[KNOB_C] = (int16_t)c;
            m->engine(v, (int32_t)t, w, (int32_t)(masks[0] >> v & 1));
        }
        masks[0] &= ~(1u << v);
        masks[1] |= 1u << v;
    }
}

/* ---- the SMPL page ------------------------------------------------------ */

static const struct core_page *smpl_info(int32_t page)
{
    const struct smpl_machine *m = current_machine();
    int i = index_of(m);
    (void)page;
    return i < 0 ? 0 : &pages[i];
}

const struct core_page_fn smpl_page_fn4 = {STOCK_PAGE_SAMPLE, smpl_info};
const struct core_page_fn smpl_page_fn5 = {STOCK_PAGE_SAMPLE2, smpl_info};

/* the SAMPLE view's page 2 (core_view_pages, a stock page's shown): stock's,
 * or a machine that keeps stock's waveform (SMPL_WAVEFORM) */
static int32_t smpl_page2_shown(void *view)
{
    const struct smpl_machine *m = current_machine();
    (void)view;
    return !m || (m->flags & SMPL_WAVEFORM);
}

const struct core_view_page smpl_view_page2 = {CORE_VIEW_SAMPLE, STOCK_PAGE_SAMPLE2, smpl_page2_shown};

/* also smpl-knob-c's, for its knob id: the face of the machine's knob C */
const struct core_knob_face *smpl_face(int32_t id, void *sound)
{
    const struct smpl_machine *m;
    int i, own = id && id == KNOB_C_ID;
    if (own)
        id = FIRST_REC + KNOB_C;
    else if (id < FIRST_REC || id > LAST_REC)
        return 0;
    m = sound ? machine_of(sound, track_of(sound)) : current_machine();
    if (own && !own_c(m))
        return 0;
    i = index_of(m);
    return i < 0 ? 0 : &faces[i][id - FIRST_REC];
}

const struct core_knob_faces smpl_faces = {FIRST_REC, LAST_REC, smpl_face};

static int put_num(char *buf, int32_t v)
{
    char tmp[8];
    int n = 0, i = 0;
    if (v < 0) {
        buf[n++] = '-';
        v = -v;
    }
    do {
        tmp[i++] = '0' + v % 10;
        v /= 10;
    } while (v && i < 7);
    while (i)
        buf[n++] = tmp[--i];
    buf[n] = 0;
    return n;
}

/* also smpl-knob-c's, for its knob id */
int smpl_text(struct core_text *t)
{
    const struct smpl_machine *m;
    const struct smpl_knob *k;
    int n = t->id && t->id == KNOB_C_ID ? KNOB_C : t->id - FIRST_REC;
    if (n < 0 || n > 7 || !(m = current_machine()))
        return CORE_TEXT_PASS;
    k = &m->knobs[n];
    if (!k->label)
        t->buf[0] = 0;
    else if (k->text)
        k->text(t->buf, t->value);
    else
        put_num(t->buf, t->value >> 8);
    return CORE_TEXT_DONE;
}

const struct core_param smpl_params = {FIRST_REC, LAST_REC, 0, 0, smpl_text};

/* knob C's own value (smpl-knob-c's core_params entry): the current
 * sound's field, in the machine's range */
static int c_range(int32_t *lo, int32_t *hi)
{
    const struct smpl_machine *m = current_machine();
    const struct smpl_knob *n = m ? &m->knobs[KNOB_C] : 0;
    *lo = 0;
    *hi = 127;
    if (!own_c(m))
        return 0;
    if (n->min < n->max) {
        *lo = n->min >> 8 < 0 ? 0 : n->min >> 8;
        *hi = n->max >> 8 > 127 ? 127 : n->max >> 8;
    }
    return 1;
}

int smpl_c_get(int32_t id, int32_t *value)
{
    unsigned char *s = core_current_sound_item();
    int32_t lo, hi;
    if (!id || id != KNOB_C_ID || !s || !c_range(&lo, &hi))
        return 0;
    *value = (int32_t)core_store_get(smpl_knob_c.field, s, 0) << 8;
    return 1;
}

int smpl_c_delta(void *view, int32_t id, int32_t delta, int32_t func)
{
    unsigned char *s = core_current_sound_item();
    int32_t lo, hi, v;
    (void)view;
    (void)func;
    if (!id || id != KNOB_C_ID || !s || !c_range(&lo, &hi))
        return 0;
    v = (int32_t)core_store_get(smpl_knob_c.field, s, 0) +
        (delta > 0 ? (delta + 255) / 256 : -((255 - delta) / 256));
    core_store_set(smpl_knob_c.field, s, 0, (unsigned)(v < lo ? lo : v > hi ? hi : v));
    return 1;
}

/* ---- the choice: AUTO, STOCK, then the machines the voice may run ------ */

/* the next choice from c by dir (+1 / -1) the track may take, or c */
static unsigned step_choice(unsigned c, int dir, int track)
{
    unsigned n = c;
    for (;;) {
        const struct smpl_machine *m;
        if (dir > 0 ? n >= CHOICE_FIRST + MAX_MACHINES - 1 : n == 0)
            return c;
        n += dir;
        if (n < CHOICE_FIRST)
            return n;
        m = by_id((int)n - CHOICE_FIRST);
        if (offered(m, track))
            return n;
    }
}

/* what the sound setup menu shows for the current sound */
static const char *choice_name(void)
{
    unsigned char *s = core_current_sound_item();
    const struct smpl_machine *m;
    unsigned c;
    if (!s)
        return "";
    if ((m = bound((signed char)s[SOUND_MACHINE], SMPL_REQUIRED)) != 0)
        return m->name;
    c = core_store_get(STORE(smpl_choice), s, 0);
    if (c == CHOICE_AUTO) {
        m = bound((signed char)s[SOUND_MACHINE], SMPL_DEFAULT);
        return m && fits(m, core_current_track()) ? m->name : "AUTO";
    }
    if (c == CHOICE_STOCK)
        return "STOCK";
    m = by_id((int)c - CHOICE_FIRST);
    return m ? m->name : "----";
}

/* a live sound's stored word for sound index i (the parameter model's read,
 * 0x400a8e66) */
#define SOUND_WORD(s, i) (*(const int16_t *)((const unsigned char *)(s) + 0x14 + 2 * (i)))
#define FIRST_INDEX 17                  /* knob A's sound index */

/* sets the current sound's choice to c on track t; when that changes the
 * machine the sound runs to machine m, m's knob defaults are written as a
 * knob turn writes them (a knob with its own range: min < max) */
/* stock's SMPL page records, 39..46: +0x04 the sound index, +0x10 the
 * default (8.8; on 1.74 TUN 64, FIN 64, SMP 0, BR 0, STA 0, END 120, LOP 0,
 * LEV 100) */
#define STOCK_RECORD(k) ((const int32_t *)(0x401bfbc8 + 0x34 * (FIRST_REC + (k))))
#define SMP_INDEX 19                    /* the sample slot: a move keeps it */

/* the default a move to m writes into knob k, or 0 for none: a machine's
 * knob with its own range, or stock's record (m 0) */
static int knob_default(const struct smpl_machine *m, int k, int32_t *def)
{
    if (FIRST_INDEX + k == SMP_INDEX)
        return 0;
    if (m) {
        const struct smpl_knob *n = &m->knobs[k];
        if (!n->label || n->min >= n->max)
            return 0;
        *def = n->def;
        return 1;
    }
    if (STOCK_RECORD(k)[1] != FIRST_INDEX + k)
        return 0;
    *def = STOCK_RECORD(k)[4];
    return 1;
}

/* sound s of track t now runs after: its defaults into the knobs */
static void write_defaults(unsigned char *s, int t, const struct smpl_machine *after)
{
    int32_t def;
    int k;
    for (k = 0; k < 8; k++)
        if (knob_default(after, k, &def))
            core_param_set(t, FIRST_INDEX + k, def, 0);
    if (own_c(after) && after->knobs[KNOB_C].min < after->knobs[KNOB_C].max) {
        def = after->knobs[KNOB_C].def >> 8;
        core_store_set(smpl_knob_c.field, s, 0, (unsigned)(def < 0 ? 0 : def > 127 ? 127 : def));
    }
}

static void choice_set(unsigned char *s, int t, unsigned c)
{
    const struct smpl_machine *before = machine_of(s, t), *after;
    core_store_set(STORE(smpl_choice), s, 0, c);
    after = machine_of(s, t);
    if (after != before)
        write_defaults(s, t, after);
}

/* gate.s, around stock's SRC machine setter 0x400bd1ba (sound, machine,
 * track, flag), whose callers are the SRC machine's stepper, its list, MIDI
 * CC 15 and NRPN, a page's paste and clear, and Overbridge. sound is an
 * object whose vtable +0x28 answers the live sound item (stock writes the
 * machine byte at its +0x68). Before: the SMPL machine the sound runs (its
 * index + 1, 0 stock; -1 none); after: if the SRC machine's change changed
 * it, the new one's defaults, as a choice in the row writes them. */
#define SOUND_ITEM(o) (((uint32_t (*)(void *))(*(void ***)(o))[0x28 / 4])(o))

int32_t smpl_src_before(void *sound, int32_t track)
{
    unsigned char *s;
    if (!sound || (uint32_t)track >= 12 || n_machines <= 0)
        return -1;
    s = (unsigned char *)SOUND_ITEM(sound);
    return s ? index_of(machine_of(s, (int)track)) + 1 : -1;
}

void smpl_src_after(void *sound, int32_t track, int32_t before)
{
    unsigned char *s;
    const struct smpl_machine *after;
    if (before < 0 || !sound || (uint32_t)track >= 12)
        return;
    s = (unsigned char *)SOUND_ITEM(sound);
    if (!s)
        return;
    after = machine_of(s, (int)track);
    if (index_of(after) + 1 != before)
        write_defaults(s, (int)track, after);
}

static void choice_turn(int delta)
{
    unsigned char *s = core_current_sound_item();
    int t = core_current_track();
    unsigned c;
    if (!s || t < 0 || t >= 12 || !delta || n_machines <= 0)
        return;
    if (bound((signed char)s[SOUND_MACHINE], SMPL_REQUIRED))
        return;                                 /* the SRC machine holds it */
    c = core_store_get(STORE(smpl_choice), s, 0);
    choice_set(s, t, step_choice(c, delta > 0 ? 1 : -1, t));
}

/* ---- the sound setup menu's row (gate.s) -------------------------------- */

/* stock's menu row (0x54 bytes, built by 0x40072fbc) takes four functors,
 * each {storage[2], manager, invoker}: its label, a press, its value's
 * drawing and a turn. The managers are stock's own for the MACHINE row's
 * functors, which hold nothing. */
struct functor {
    void *data[2];
    void *manager;
    void *invoker;
};

/* operator new; stock returns its pointer in %d0, where our compiler reads a
 * pointer from %a0, so it is declared as returning an integer */
#define NEW ((uint32_t (*)(uint32_t))0x400827c4)
#define ROW_INIT ((void (*)(void *, struct functor *, struct functor *, struct functor *, \
                            struct functor *, int32_t, int32_t))0x40072fbc)
#define DRAW_TEXT ((void (*)(void *, const void *, int32_t, int32_t, int32_t, int32_t, int32_t, \
                             const char *, ...))0x4007154c)
#define MENU_FONT ((const void *)0x4027aa28)
#define FMT_S ((const char *)0x402413d4)                             /* "%s" */

extern char smpl_row_label_s[];                                     /* gate.s */
const char smpl_row_text[] = "SMPL MACHINE";                       /* the row's label */

static void row_draw(void *f, void *row, void *ctx, int32_t x, int32_t y)
{
    (void)f;
    (void)row;
    DRAW_TEXT(ctx, MENU_FONT, x + 0x13, y + 2, 2, 0, 0, FMT_S, choice_name());
}

/* the menu redraws itself after a cursor move (0x40078580 on the view's
 * widget at +0x38) but not after a turn: stock's own rows are redrawn by the
 * sound's change notice. Ours is redrawn here, while the view that holds it
 * is the one it was added to. */
#define INVALIDATE ((void (*)(void *))0x40078580)
static void *menu_view, *menu_row;

static void row_turn(void *f, void *row, int32_t delta)
{
    (void)f;
    choice_turn(delta);
    if (row && row == menu_row && menu_view)
        INVALIDATE((char *)menu_view + 0x38);
}

/* gate.s, as the sound setup menu (view) is built, after its MACHINE row;
 * returns the row to add in %d0, or 0 */
uint32_t smpl_menu_row(void *view)
{
    struct functor label = {{0, 0}, (void *)0x400ee470, smpl_row_label_s};
    struct functor press = {{0, 0}, 0, 0};
    struct functor draw = {{0, 0}, (void *)0x400ee4c2, (void *)row_draw};
    struct functor turn = {{0, 0}, (void *)0x400ee514, (void *)row_turn};
    uint32_t row;
    if (count_machines() <= 0 || !(row = NEW(0x54)))      /* the menu is built at boot, before a tick */
        return 0;
    ROW_INIT((void *)row, &label, &press, &draw, &turn, -1, 8);
    menu_view = view;
    menu_row = (void *)row;
    return row;
}

/* ---- the machine list: TRK + SMPL ---------------------------------------
 * Stock's MachineListView (the SRC page's list), built as the SRC page
 * builds it (0x400d3f1c), with its rows replaced by the choices this track
 * offers: AUTO, STOCK and the machines that fit it, each row's id its
 * choice. UP/DOWN step the choice (the view's step callback, as stock's
 * steps the SRC machine), YES keeps it, NO puts the first one back (the
 * view's revert callback), and stock's own listeners close it when the
 * track changes. */
#define LIST_CTOR ((void (*)(void *, int32_t, int32_t, uint32_t, uint32_t, uint32_t, uint32_t, \
                             int32_t, int32_t, int32_t))0x400def76)
#define LIST_SIZE 0x1e8
#define LIST_CTRL_VT 0x401e38d8         /* its shared_ptr's control block */
#define LIST_SELECT ((void (*)(void *, int32_t))0x400dea5e)
#define LIST_ON_STEP ((void (*)(void *, struct functor *))0x400df428)
#define LIST_ON_REVERT ((void (*)(void *, struct functor *))0x400df3b2)
#define MODEL_CLEAR ((void (*)(void *))0x40072914)
#define MODEL_ADD ((void (*)(void *, uint32_t))0x400727f2)
#define ROW_ID(row) (((const int32_t *)(row))[1])
#define UI_ROOT ((uint32_t (*)(void))0x401822f6)
#define UI_PUSH ((uint32_t (*)(uint32_t, uint32_t *, int32_t))0x400a1ef8)
#define SP_RELEASE ((void (*)(uint32_t *))0x40170bd8)
#define WP_ALIVE ((uint32_t (*)(uint32_t *))0x40181af4)
#define WP_RELEASE ((void (*)(uint32_t))0x401819cc)
#define EMPTY_FN_MANAGER ((void *)0x400ee514)  /* stock's, for a functor that holds nothing */
#define KEY_TRK 0x86
#define KEY_SMPL 0x32
#define KEY_PRESS 0x01
#define KEY_REPEAT 0x08
#define KEY_RELEASE 0x10

extern char smpl_list_label_s[];                                    /* gate.s */
static void *list_view;
static int16_t list_words[8];           /* knobs A..H as the list opened */
static unsigned list_c;                  /* knob C's own value as the list opened */
static uint32_t list_ctrl;              /* a weak hold on the open list */
static int trk_down, smpl_taken;
static char list_text[16];

/* the row's label: its choice, padded as stock pads machine names */
uint32_t smpl_list_label(const void *row)
{
    int c = ROW_ID(row), i = 0;
    const struct smpl_machine *m;
    const char *s = c == CHOICE_AUTO ? "AUTO" : c == CHOICE_STOCK ? "STOCK"
                  : (m = by_id(c - CHOICE_FIRST)) != 0 ? m->name : "----";
    while (i < 15 && s[i]) {
        list_text[i] = s[i];
        i++;
    }
    while (i < 15)
        list_text[i++] = ' ';
    list_text[15] = 0;
    return (uint32_t)list_text;
}

static void list_step(void *f, int32_t dir)
{
    unsigned char *s = core_current_sound_item();
    int t = core_current_track();
    unsigned c;
    (void)f;
    if (!s || t < 0 || t >= 12 || !dir || !list_view)
        return;
    c = core_store_get(STORE(smpl_choice), s, 0);
    c = step_choice(c, dir > 0 ? 1 : -1, t);
    choice_set(s, t, c);
    LIST_SELECT(list_view, (int32_t)c);
}

/* NO: the first choice back, and the knobs' words as they were */
static void list_revert(void *f, int32_t first)
{
    unsigned char *s = core_current_sound_item();
    int t = core_current_track(), k;
    (void)f;
    if (!s || t < 0 || t >= 12 || first < 0 || first >= CHOICE_FIRST + MAX_MACHINES)
        return;
    core_store_set(STORE(smpl_choice), s, 0, (unsigned)first);
    if (smpl_knob_c.id)
        core_store_set(smpl_knob_c.field, s, 0, list_c);
    for (k = 0; k < 8; k++)
        if (SOUND_WORD(s, FIRST_INDEX + k) != list_words[k])
            core_param_set(t, FIRST_INDEX + k, (uint16_t)list_words[k], 0);
}

static void list_add(void *model, int32_t c)
{
    struct functor label = {{0, 0}, (void *)0x400ee470, smpl_list_label_s};
    struct functor none = {{0, 0}, 0, 0};
    uint32_t row = NEW(0x54);
    if (!row)
        return;
    ROW_INIT((void *)row, &label, &none, &none, &none, c, 8);
    MODEL_ADD(model, row);
}

static void list_open(void)
{
    unsigned char *s = core_current_sound_item();
    int t = core_current_track(), k;
    unsigned c;
    uint32_t view, ctrl, sp[2];
    struct functor step = {{0, 0}, EMPTY_FN_MANAGER, (void *)list_step};
    struct functor revert = {{0, 0}, EMPTY_FN_MANAGER, (void *)list_revert};
    if (!s || t < 0 || t >= 12 || n_machines <= 0)
        return;
    if (list_ctrl && WP_ALIVE(&list_ctrl))
        return;                                     /* open already */
    c = core_store_get(STORE(smpl_choice), s, 0);
    for (k = 0; k < 8; k++)
        list_words[k] = SOUND_WORD(s, FIRST_INDEX + k);
    list_c = smpl_knob_c.id ? core_store_get(smpl_knob_c.field, s, 0) : 0;
    if (!(view = NEW(LIST_SIZE)))
        return;
    LIST_CTOR((void *)view, t, (int32_t)c, 0, 0, 0, 0, 0x46, 0x3a, 1);
    if (!(ctrl = NEW(16))) {
        /* the view stays: freeing it needs its own destructor; it is not
         * pushed, so nothing draws it */
        return;
    }
    ((uint32_t *)ctrl)[0] = LIST_CTRL_VT;
    ((uint32_t *)ctrl)[1] = 1;
    ((uint32_t *)ctrl)[2] = 2;                      /* ours: a weak hold */
    ((uint32_t *)ctrl)[3] = view;
    if (list_ctrl)
        WP_RELEASE(list_ctrl);
    list_ctrl = ctrl;
    list_view = (void *)view;
    MODEL_CLEAR((char *)view + 0x4c);
    list_add((char *)view + 0x4c, CHOICE_AUTO);
    list_add((char *)view + 0x4c, CHOICE_STOCK);
    for (k = 0; k < MAX_MACHINES; k++) {
        if (offered(by_id(k), t))
            list_add((char *)view + 0x4c, CHOICE_FIRST + k);
    }
    LIST_SELECT((void *)view, (int32_t)c);
    LIST_ON_STEP((void *)view, &step);
    LIST_ON_REVERT((void *)view, &revert);
    sp[0] = view;
    sp[1] = ctrl;
    UI_PUSH(UI_ROOT(), sp, 0);
    SP_RELEASE(&sp[1]);
}

/* ev_key: SMPL while TRK is held opens the list; the SMPL key's events are
 * taken until it is let go, so stock's SMPL page doesn't open under it */
int smpl_key(void *brain, void *ev)
{
    const uint8_t *e = (const uint8_t *)ev;
    uint32_t code = *(const uint32_t *)(e + 12), flags = *(const uint32_t *)(e + 16);
    (void)brain;
    /* a key's release comes as 0x10, or as 0 when it was used with another
     * key (TRK + a pad): anything without the press bit lets go */
    if (code == KEY_TRK) {
        trk_down = (flags & KEY_PRESS) && !(flags & KEY_RELEASE);
        return 0;
    }
    if (code != KEY_SMPL)
        return 0;
    if (smpl_taken) {
        if (!(flags & KEY_PRESS) || (flags & KEY_RELEASE))
            smpl_taken = 0;
        return 1;
    }
    if (trk_down && (flags & KEY_PRESS) && !(flags & (KEY_REPEAT | KEY_RELEASE))) {
        unsigned char *s = core_current_sound_item();
        if (n_machines <= 0 || !s || bound((signed char)s[SOUND_MACHINE], SMPL_REQUIRED))
            return 0;                               /* nothing to choose: stock's SMPL */
        smpl_taken = 1;
        list_open();
        return 1;
    }
    return 0;
}
