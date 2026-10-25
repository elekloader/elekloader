/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * machines-ot: the Octatrack's machines companion (OS 1.40C). The machine
 * chooser, SRC SETUP, the page resolver, the Part validator and the FLEX
 * renderer ask the machines in the `ot_machines` table (struct ot_machine,
 * elekloader/sdk/include/octatrack/machines.h), in row order: rows 5 on.
 * seams.s holds the 32 places in the firmware that come here; this file is
 * what they ask.
 *
 * A machine's track is a FLEX track with the machine's signature in the Part
 * (Part + 60 + 30 * track: two letters and a version), the form octabam's
 * machines write, so a Part saved by one of them reads the same here. The
 * record has the layout and meaning of version 1 of Modwerk's record of the
 * same name; this code is elekloader's own.
 */
#include <stdint.h>
#include "octatrack/machines.h"

#define U8(a)       (*(volatile uint8_t *)(uintptr_t)(a))
#define U32(a)      (*(volatile uint32_t *)(uintptr_t)(a))

#define BANK        0x46c82456u     /* the long holding the bank's address */
#define PART_OFF    0x8ed80u        /* Part 0 in the bank (Part' = bank + this + part * 6322) */
#define PART_SIZE   6322u
#define CUR_PART    0x100b14cfu     /* the current Part (a byte) */
#define CUR_TRACK   0x100b14ccu     /* the current track (a byte) */
#define MIRROR      0x100a4eceu     /* the Parts' battery-backed copies, the same layout */
#define SIG         60u             /* + 30 * track: the signature (NEIGHBOR's PLAYBACK 0..2) */
#define TYPES       0x22u           /* + track: the machine byte (0 STATIC, 1 FLEX, ... 4 PICKUP) */
#define FLEX        1u
#define FLEX_PAGE   0x400d31aeu     /* stock's FLEX PLAYBACK page */
#define NEIGHBOR_PAGE 0x400d34d2u   /* stock's spare rows 5 and 6 hold it */
#define FLEX_DEFAULTS 0x400d320cu   /* stock's 12 FLEX defaults */
#define NO_NAME     0x400b94f6u     /* what stock's name(row) gives past its five */
#define ROWS        8u              /* machines listed at most: rows 5..12 */

typedef volatile uint8_t Part;

extern const struct ot_machine *const ot_machines[];   /* the table: pointers, then 0 */
extern void (*const ev_machine_set[])(volatile uint8_t *, unsigned, unsigned, unsigned);
int otm_stock_validate(uint8_t *part);                  /* seams.s */

/* The grown tables the firmware's readers are pointed at (seams.s): stock's own until
 * otm_init adds the machines, so a reader that comes first sees stock. */
const char *otm_names[5 + ROWS] = {
    (const char *)0x400b3eacu, (const char *)0x400b3e98u, (const char *)0x400b7c67u,
    (const char *)0x400b5413u, (const char *)0x400b7a63u,   /* STATIC FLEX THRU NEIGHBOR PICKUP */
};
uint32_t otm_pages[5 + ROWS + 1] = {
    0x400d301cu, FLEX_PAGE, 0x400d3340u, NEIGHBOR_PAGE, 0x400d3664u, NEIGHBOR_PAGE, NEIGHBOR_PAGE, 0,
};
uint32_t otm_last = 4;              /* the last chooser row */
uint32_t otm_total = 5;             /* the rows */
uint32_t otm_clamp = 5;             /* the list's absolute row is kept in 0..this (stock: 5) */
uint8_t otm_ready;

static const struct ot_machine *rows[ROWS];
static unsigned n_rows;

/* The machines octabam and Modwerk sign tracks with. A track that still carries one
 * whose machine is not in the build keeps its 12 bytes from stock's validator (they
 * are that machine's, not FLEX's: a Part with one out of FLEX's ranges would make
 * the boot's battery restore refuse the whole Part). It plays as stock plays it. */
static const uint8_t known[][4] = {
    {'A', 'B', 1, 1}, {'T', 'B', 1, 1}, {'C', 'X', 1, 1}, {'F', 'M', 1, 1}, {'P', 'L', 1, 1}, {'S', '2', 1, 2},
    {'O', 'T', 1, 1},                   /* BLACKPYRAMIDS-303 (0.2) */
};

/* Which records are listed: version 1, and nothing 0.1 cannot serve (a DSP voice). */
static unsigned servable(const struct ot_machine *m)
{
    return m->abi == OT_MACHINE_ABI && !m->source_kind && m->label;
}

void otm_init(void)
{
    unsigned n = 0, r;
    for (const struct ot_machine *const *p = ot_machines; *p && n < ROWS; ++p)
        if (servable(*p))
            rows[n++] = *p;
    for (r = 0; r < n; ++r) {
        otm_names[5 + r] = rows[r]->label;
        otm_pages[5 + r] = rows[r]->page ? rows[r]->page : FLEX_PAGE;
    }
    for (r = 5 + n; r < 7; ++r)         /* stock's spare rows, as stock has them */
        otm_pages[r] = NEIGHBOR_PAGE;
    otm_pages[r] = 0;                   /* then its end */
    n_rows = n;
    otm_last = 4 + n;
    otm_total = 5 + n;
    otm_clamp = n > 1 ? 4 + n : 5;
    otm_ready = 1;
}

static inline void ready(void)
{
    if (!otm_ready)
        otm_init();
}

static unsigned valid_bank(void)
{
    uint32_t b = U32(BANK);
    return b >= 0x40000000u && b < 0x47f00000u;
}

volatile uint8_t *ot_part(void)
{
    return valid_bank() ? (Part *)(uintptr_t)(U32(BANK) + PART_OFF + (U8(CUR_PART) & 3u) * PART_SIZE) : 0;
}

static Part *mirror_of(Part *part)
{
    return (Part *)(uintptr_t)(MIRROR + ((uint32_t)(uintptr_t)part - U32(BANK) - PART_OFF));
}

/* Track t's machine in `part`: its row (5 on), or 0 for a stock track. A machine's
 * track is a FLEX track (0.1 serves no machine on STATIC's backing). */
static unsigned owner(const Part *part, unsigned t, const struct ot_machine **who)
{
    *who = 0;
    if (t >= 8 || part[TYPES + t] != FLEX)
        return 0;
    const Part *s = part + SIG + 30u * t;
    for (unsigned k = 0; k < n_rows; ++k) {
        const struct ot_machine *m = rows[k];
        if (s[0] == m->sig[0] && s[1] == m->sig[1] && s[2] >= m->sig_lo && s[2] <= m->sig_hi) {
            *who = m;
            return 5u + k;
        }
    }
    return 0;
}

static unsigned orphan(const Part *part, unsigned t)
{
    if (t >= 8 || part[TYPES + t] != FLEX)
        return 0;
    const Part *s = part + SIG + 30u * t;
    for (unsigned k = 0; k < sizeof known / sizeof *known; ++k)
        if (s[0] == known[k][0] && s[1] == known[k][1] && s[2] >= known[k][2] && s[2] <= known[k][3])
            return 1;
    return 0;
}

const struct ot_machine *ot_track_machine(unsigned track)
{
    const struct ot_machine *who;
    Part *part;
    ready();
    part = ot_part();
    if (part)
        owner(part, track, &who);
    else
        who = 0;
    return who;
}

unsigned ot_machine_row(const struct ot_machine *m)
{
    ready();
    for (unsigned k = 0; k < n_rows; ++k)
        if (rows[k] == m)
            return 5u + k;
    return 0;
}

/* The machine byte at `ptr`, as a row: a current-Part track a machine owns reads as its row. */
unsigned otm_type(unsigned type, const volatile uint8_t *ptr)
{
    const struct ot_machine *who;
    Part *part;
    uint32_t t;
    ready();
    part = ot_part();
    if (!part || type > FLEX)
        return type;
    t = (uint32_t)(uintptr_t)ptr - (uint32_t)(uintptr_t)(part + TYPES);
    if (t >= 8)
        return type;
    unsigned row = owner(part, t, &who);
    return row ? row : type;
}

/* The chooser's row match: a machine may say its row is not the track's (chooser_type). */
unsigned otm_chooser_type(unsigned type, const volatile uint8_t *ptr)
{
    const struct ot_machine *who = 0;
    Part *part;
    ready();
    part = ot_part();
    if (part)
        owner(part, U8(CUR_TRACK), &who);
    if (who && who->chooser_type && who->chooser_type(type, ptr) != 5)
        return type;
    return otm_type(type, ptr);
}

/* name(row): rows past stock's five. */
const char *otm_name(unsigned row)
{
    ready();
    return row < 5 + n_rows ? otm_names[row] : (const char *)NO_NAME;
}

/* The page resolver: an owned track's page, or 0 for stock's lookup. */
uint32_t otm_page(const volatile uint8_t *slot)
{
    const struct ot_machine *who;
    Part *part;
    uint32_t t;
    ready();
    part = ot_part();
    if (!part)
        return 0;
    t = (uint32_t)(uintptr_t)slot - (uint32_t)(uintptr_t)(part + TYPES);
    if (t >= 8 || !owner(part, t, &who))
        return 0;
    if (who->track_page)
        return who->track_page(slot);
    return who->page;
}

static void set_flex(Part *part, Part *mirror, unsigned track, const uint8_t *v)
{
    for (unsigned k = 0; k < 12; ++k) {
        unsigned at = (k < 6 ? 0x2au : 0x1dau) + 30u * track + 6u + k % 6u;
        part[at] = mirror[at] = v[k];
    }
}

static void set_sig(Part *part, Part *mirror, unsigned track, uint8_t a, uint8_t b, uint8_t v)
{
    unsigned at = SIG + 30u * track;
    part[at] = mirror[at] = a;
    part[at + 1] = mirror[at + 1] = b;
    part[at + 2] = mirror[at + 2] = v;
}

/* A track leaves its machine: the machine's leave, and stock's FLEX defaults back for a
 * PROTECT machine the companion signs (the 12 bytes held its parameters). */
static void leave(Part *part, Part *mirror, unsigned track, const struct ot_machine *old)
{
    if (old->leave)
        old->leave(track);
    if (!old->assign && (old->flags & OT_MACHINE_PROTECT))
        set_flex(part, mirror, track, (const uint8_t *)FLEX_DEFAULTS);
}

/* The track's row in `part`: its machine's (5 on), or its machine byte. */
static unsigned row_of(const Part *part, unsigned track)
{
    const struct ot_machine *who;
    unsigned row = owner(part, track, &who);
    return row ? row : part[TYPES + track];
}

static unsigned assign(Part *part, unsigned track, unsigned row);

/* A chooser or the slot assigner commits `row` for `track` of `part`: -> the machine byte to
 * store, or -1 to refuse (the track stays as it was). The byte and its battery copy are stored
 * here as well (the seam stores the same), then ev_machine_set tells mods, when the row changed. */
unsigned otm_assign(Part *part, unsigned track, unsigned row)
{
    unsigned from, type;
    ready();
    if (!valid_bank() || track >= 8)
        return row < 5 ? row : FLEX;
    from = row_of(part, track);
    type = assign(part, track, row);
    if (type != (unsigned)-1) {
        uint32_t off = (uint32_t)(uintptr_t)part - U32(BANK) - PART_OFF;
        if (off % PART_SIZE == 0 && off / PART_SIZE < 4) {
            part[TYPES + track] = mirror_of(part)[TYPES + track] = (uint8_t)type;
            unsigned to = row_of(part, track);
            if (to != from)
                for (void (*const *f)(volatile uint8_t *, unsigned, unsigned, unsigned) = ev_machine_set; *f; ++f)
                    (*f)(part, track, from, to);
        }
    }
    return type;
}

static unsigned assign(Part *part, unsigned track, unsigned row)
{
    const struct ot_machine *old, *next = 0;
    Part *mirror;
    uint32_t off;
    off = (uint32_t)(uintptr_t)part - U32(BANK) - PART_OFF;
    if (off % PART_SIZE || off / PART_SIZE >= 4)
        return (unsigned)-1;
    mirror = mirror_of(part);
    if (row >= 5 && row < 5 + n_rows)
        next = rows[row - 5];
    owner(part, track, &old);
    unsigned absent = !old && orphan(part, track);
    if (next) {
        if (next->admit_track && !next->admit_track(part, track))
            return (unsigned)-1;
        if (old && old != next)
            leave(part, mirror, track, old);
        if (absent)
            set_flex(part, mirror, track, (const uint8_t *)FLEX_DEFAULTS);
        if (next->assign)
            return next->assign(part, track, 1);
        if (old != next && next->defaults)
            set_flex(part, mirror, track, next->defaults);
        set_sig(part, mirror, track, next->sig[0], next->sig[1], next->sig_lo);
        return FLEX;
    }
    if (old && old->assign) {
        unsigned type = old->assign(part, track, 0);
        const struct ot_machine *still;
        owner(part, track, &still);
        if (still == old)
            return type;
        leave(part, mirror, track, old);
    } else if (old || absent) {
        if (old)
            leave(part, mirror, track, old);
        else
            set_flex(part, mirror, track, (const uint8_t *)FLEX_DEFAULTS);
        set_sig(part, mirror, track, 0, 0, 0);
    }
    return row < 5 ? row : FLEX;
}

/* The Part validator: stock never sees a PROTECT machine's (or a known absent one's) 12
 * FLEX bytes, and the machines' validate wrap it in row order. */
static unsigned wraps, depth;

static int next_validator(uint8_t *part)
{
    while (depth < wraps) {
        const struct ot_machine *m = rows[depth++];
        if (m->validate)
            return m->validate(part, next_validator);
    }
    return otm_stock_validate(part);
}

int otm_validate(uint8_t *part)
{
    const struct ot_machine *who;
    uint8_t saved[8][12];
    unsigned mask = 0, t, k;
    int result;
    ready();
    for (t = 0; t < 8; ++t) {
        owner(part, t, &who);
        if (who ? (who->flags & OT_MACHINE_PROTECT) != 0 : orphan(part, t)) {
            mask |= 1u << t;
            for (k = 0; k < 12; ++k) {
                unsigned at = (k < 6 ? 0x2au : 0x1dau) + 30u * t + 6u + k % 6u;
                saved[t][k] = part[at];
                part[at] = U8(FLEX_DEFAULTS + k);
            }
        }
    }
    wraps = n_rows;
    depth = 0;
    result = next_validator(part);
    wraps = depth = 0;
    for (t = 0; t < 8; ++t)
        if (mask & (1u << t))
            for (k = 0; k < 12; ++k)
                part[(k < 6 ? 0x2au : 0x1dau) + 30u * t + 6u + k % 6u] = saved[t][k];
    return result;
}

/* The FLEX renderer (STATIC's and PICKUP's too), in the audio render. */
int otm_render(unsigned track, unsigned ping, unsigned start, unsigned end)
{
    const struct ot_machine *who;
    Part *part;
    ready();
    part = ot_part();
    if (part && owner(part, track, &who)) {
        if (who->render)
            return who->render(track, ping, start, end);
    } else if (part && track < 8 && part[TYPES + track] == FLEX && !orphan(part, track)) {
        for (unsigned k = 0; k < n_rows; ++k)
            if ((rows[k]->flags & OT_MACHINE_RENDER_FLEX) && rows[k]->render)
                return rows[k]->render(track, ping, start, end);
    }
    return ot_stock_render(track, ping, start, end);
}

/* The chooser's level (seams.s): a track of a machine with OT_MACHINE_LIST_FIRST reads as its
 * row, so the window opens on the machine list; any other as its byte. */
unsigned otm_level(unsigned type, const volatile uint8_t *ptr)
{
    unsigned row = otm_type(type, ptr);
    return row >= 5 && (rows[row - 5]->flags & OT_MACHINE_LIST_FIRST) ? row : type;
}

/* The tick (seams.s, in the UI task's tick body, 60 times a second): each machine's ui_tick,
 * then the current track's machine's page, where the screen's builders that follow read it. */
void otm_tick(void)
{
    const struct ot_machine *who;
    Part *part;
    unsigned t, row;
    ready();
    for (unsigned k = 0; k < n_rows; ++k)
        if (rows[k]->ui_tick)
            rows[k]->ui_tick();
    part = ot_part();
    t = U8(CUR_TRACK);
    if (part && t < 8 && (row = owner(part, t, &who)) != 0 && who->track_page) {
        uint32_t page = who->track_page(part + TYPES + t);
        if (page)
            otm_pages[row] = page;
    }
}
