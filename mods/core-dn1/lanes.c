/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-dn1 5.1: lane parameters, mod knobs on the stock pages and TRIG's second page, and each note the
 * sequencer builds (Digitone mk1, OS 1.43 and 1.44: the addresses come from mod.json's defsym).
 *
 * A lane parameter is a mod parameter (core_params, 182-191) whose descriptor's slot is CORE_SLOT_LANE
 * (-1): its values are core's, not a sound slot's. Each pattern has a value per track (a trig
 * parameter's, as NOTE's or VELOCITY's) and may p-lock it on any step, as the stock trig parameters.
 * Values are whole numbers (the record's min, max and default >> 8). The knob turns and shows them
 * as any parameter's: with trig keys held in GRID REC, the held steps' locks (unless the lane flags
 * say CORE_LANE_NOLOCK), else the track's value.
 *
 * Where they live. The Digitone keeps all 128 patterns live in RAM (PATTERNS + k * PATTERN_SIZE), and
 * their stored form only on the +Drive and in a project's image: a pattern is stored by
 * SERIALIZE(rows, locks) and loaded by DESERIALIZE(locks, rows), the locks being the live pattern's
 * +0x1e80. The stored form has 80 lock rows of 0x82 bytes, {id, track, s16 value[64]}; stock writes
 * its rows from the first, at most 79, and 0xff in the rest, and its loader skips any row whose id is
 * 79 or more. Core keeps the lanes per pattern number in its own RAM, writes them after stock's rows
 * (CORE_ROW_LOCKS + lane: a track's p-locks, -1 for none; CORE_ROW_TRACK + lane: the eight tracks'
 * values in the first eight words) and reads them back on a load. A project saved with lanes loads on
 * the stock OS, which skips the rows; one that has no room left for a row loses that row.
 *
 * Not followed (5.1): a pattern, track or page copied and pasted (the copy has no lanes), a pattern
 * cleared (its lanes stay). A trig's locks removed (ev_step_clear) clear its lanes' locks. */
#include <stdint.h>

#define LANE0           182
#define LANES           10
#define TRACKS          8
#define STEPS           64
#define PATTERN_COUNT   128
#define PATTERN_SIZE    0x1611d
#define LOCKS_OFF       0x1e80
#define ROWS            80
#define ROW_SIZE        0x82
#define ROW_LOCKS       0x80            /* + lane */
#define ROW_TRACK       0xc0            /* + lane */
#define LROWS           384             /* lock rows for the whole project */
#define LANE_NOLOCK     1               /* lane flags: a track value only */
#define SLOT_LANE       (-1)
#define PAGE_TRIG2      30
#define PAGE_TRIG       1
#define KNOBS_MAX       32

extern void *const core_params[];       /* descriptors, 0-ended */
extern void *const core_knobs[];        /* struct knob *, 0-ended */
typedef void (*built_fn)(int32_t track, int32_t step, void *event);
extern built_fn const ev_note_built[];
extern int32_t core_held(void *mgr);    /* lanes.s: HELD_STEPS(mgr) */
extern uint8_t fw_patterns[];          /* fw.s: the 128 live patterns */
#define PATTERNS        fw_patterns
extern uint8_t *volatile fw_pattern;

struct desc {                           /* core_params' descriptor */
    int32_t id;
    int32_t group, slot, min, max, def, flags, cc, w6, w7, w8;
    const char *name, *group_name, *short_name;
    void (*format)(int32_t value, char *buf);
    const char *empty;
    int32_t look;
    int32_t lane_flags;                 /* from 5.1, read only for a lane */
};

struct knob { int32_t page, knob, id; };

struct lrow { int16_t v[STEPS]; };

static int16_t track_v[PATTERN_COUNT][LANES][TRACKS];   /* -1: the descriptor's default */
static int16_t row_of[PATTERN_COUNT][LANES][TRACKS];    /* an index into rows, or -1 */
static struct lrow rows[LROWS];
static uint8_t row_used[LROWS];
static struct desc *lane_desc[LANES];
static int32_t lanes_ready;
static int32_t build_pat = -1;          /* in ev_note_built: the pattern the note came from */
volatile int32_t core_lanes_dirty;
static uint32_t rearm;                  /* bit t: track t's lanes changed (the UI task) */
extern void core_rearm(int32_t track);  /* lanes.s: the sequencer prepares the track's first step again */
extern volatile int32_t fw_transport;

/* ---- the lanes ------------------------------------------------------------------------------- */
static void lanes_init(void)
{
    int32_t i, p, l, t;
    for (i = 0; core_params[i]; i++) {
        struct desc *d = (struct desc *)core_params[i];
        if (d->slot == SLOT_LANE && d->id >= LANE0 && d->id < LANE0 + LANES)
            lane_desc[d->id - LANE0] = d;
    }
    for (p = 0; p < PATTERN_COUNT; p++)
        for (l = 0; l < LANES; l++)
            for (t = 0; t < TRACKS; t++) {
                track_v[p][l][t] = -1;
                row_of[p][l][t] = -1;
            }
    lanes_ready = 1;
}

static int32_t lane_of(int32_t id)
{
    if (!lanes_ready)
        lanes_init();
    id -= LANE0;
    if (id < 0 || id >= LANES || !lane_desc[id])
        return -1;
    return id;
}

/* A live pattern's number, or -1 (a clipboard's, an undo buffer's: not followed). */
static int32_t pattern_of(const uint8_t *pat)
{
    uint32_t off = (uint32_t)(pat - PATTERNS);
    if (off % PATTERN_SIZE)
        return -1;
    off /= PATTERN_SIZE;
    return off < PATTERN_COUNT ? (int32_t)off : -1;
}

static int32_t pattern_now(void)
{
    return build_pat >= 0 ? build_pat : pattern_of(fw_pattern);
}

static int32_t clampv(int32_t l, int32_t v)
{
    int32_t lo = lane_desc[l]->min >> 8, hi = lane_desc[l]->max >> 8;
    return v < lo ? lo : v > hi ? hi : v;
}

static int32_t track_value(int32_t p, int32_t l, int32_t t)
{
    int32_t v = p >= 0 ? track_v[p][l][t] : -1;
    return v >= 0 ? v : clampv(l, lane_desc[l]->def >> 8);
}

static int32_t lock_value(int32_t p, int32_t l, int32_t t, int32_t s)
{
    int32_t r;
    if (p < 0 || s < 0 || s >= STEPS)
        return -1;
    r = row_of[p][l][t];
    return r >= 0 ? rows[r].v[s] : -1;
}

static void row_free(int32_t p, int32_t l, int32_t t)
{
    int32_t r = row_of[p][l][t];
    if (r >= 0) {
        row_used[r] = 0;
        row_of[p][l][t] = -1;
    }
}

/* Set a step's lock (v -1 clears it). 0: no row free. */
static int32_t lock_set(int32_t p, int32_t l, int32_t t, int32_t s, int32_t v)
{
    int32_t r = row_of[p][l][t], i;
    if (r < 0) {
        if (v < 0)
            return 1;
        for (r = 0; r < LROWS && row_used[r]; r++)
            ;
        if (r == LROWS)
            return 0;
        row_used[r] = 1;
        for (i = 0; i < STEPS; i++)
            rows[r].v[i] = -1;
        row_of[p][l][t] = (int16_t)r;
    }
    rows[r].v[s] = (int16_t)v;
    if (v < 0) {
        for (i = 0; i < STEPS && rows[r].v[i] < 0; i++)
            ;
        if (i == STEPS)
            row_free(p, l, t);
    }
    return 1;
}

static void pattern_clear(int32_t p)
{
    int32_t l, t;
    for (l = 0; l < LANES; l++)
        for (t = 0; t < TRACKS; t++) {
            track_v[p][l][t] = -1;
            row_free(p, l, t);
        }
}

/* ---- for mods ---------------------------------------------------------------------------------- */
int32_t core_lane_lock(int32_t id, int32_t track, int32_t step)
{
    int32_t l = lane_of(id);
    if (l < 0 || (uint32_t)track >= TRACKS)
        return -1;
    return lock_value(pattern_now(), l, track, step);
}

int32_t core_lane_track(int32_t id, int32_t track)
{
    int32_t l = lane_of(id);
    if (l < 0 || (uint32_t)track >= TRACKS)
        return -1;
    return track_value(pattern_now(), l, track);
}

int32_t core_lane_value(int32_t id, int32_t track, int32_t step)
{
    int32_t v = core_lane_lock(id, track, step);
    return v >= 0 ? v : core_lane_track(id, track);
}

/* The UI task. step -1: the track's value (value -1: back to the default); else the step's lock
 * (value -1 clears it). -> 0 when it could not be kept (no lane, no pattern, no row free). */
int32_t core_lane_set(int32_t id, int32_t track, int32_t step, int32_t value)
{
    int32_t l = lane_of(id), p = pattern_of(fw_pattern);
    if (l < 0 || p < 0 || (uint32_t)track >= TRACKS || step >= STEPS)
        return 0;
    if (value >= 0)
        value = clampv(l, value);
    core_lanes_dirty = 1;
    rearm |= 1u << track;
    if (step < 0) {
        track_v[p][l][track] = (int16_t)(value < 0 ? -1 : value);
        return 1;
    }
    return lock_set(p, l, track, step, value);
}

/* ---- the stored rows --------------------------------------------------------------------------- */
static void put_row(uint8_t *row, int32_t id, int32_t track, const int16_t *v, int32_t n)
{
    int32_t i;
    row[0] = (uint8_t)id;
    row[1] = (uint8_t)track;
    for (i = 0; i < STEPS; i++) {
        int16_t x = i < n ? v[i] : -1;
        row[2 + 2 * i] = (uint8_t)(x >> 8);
        row[3 + 2 * i] = (uint8_t)x;
    }
}

/* After stock's SERIALIZE(rows, locks) returned 1: the pattern's lanes after its rows. */
void core_lanes_save(uint8_t *out, const uint8_t *locks)
{
    int32_t p = pattern_of(locks - LOCKS_OFF), n, l, t;
    if (p < 0 || !lanes_ready)
        return;
    for (n = 0; n < ROWS && !(out[n * ROW_SIZE] == 0xff && out[n * ROW_SIZE + 1] == 0xff); n++)
        ;
    for (l = 0; l < LANES && n < ROWS; l++) {
        for (t = 0; t < TRACKS && track_v[p][l][t] < 0; t++)
            ;
        if (t < TRACKS)
            put_row(out + n++ * ROW_SIZE, ROW_TRACK + l, 0, track_v[p][l], TRACKS);
        for (t = 0; t < TRACKS && n < ROWS; t++)
            if (row_of[p][l][t] >= 0)
                put_row(out + n++ * ROW_SIZE, ROW_LOCKS + l, t, rows[row_of[p][l][t]].v, STEPS);
    }
}

/* After stock's DESERIALIZE(locks, rows): the pattern's lanes from its rows (none: cleared). */
void core_lanes_load(const uint8_t *locks, const uint8_t *in, int32_t ok)
{
    int32_t p = pattern_of(locks - LOCKS_OFF), n, i;
    if (p < 0)
        return;
    if (!lanes_ready)
        lanes_init();
    pattern_clear(p);
    core_lanes_dirty = 1;
    rearm = 0xff;
    if (!(ok & 0xff))
        return;
    for (n = 0; n < ROWS; n++, in += ROW_SIZE) {
        int32_t id = in[0], t = in[1], l;
        if (id >= ROW_LOCKS && id < ROW_LOCKS + LANES && t < TRACKS) {
            l = id - ROW_LOCKS;
            for (i = 0; i < STEPS; i++) {
                int16_t v = (int16_t)((in[2 + 2 * i] << 8) | in[3 + 2 * i]);
                if (v >= 0 && !lock_set(p, l, t, i, v))
                    break;
            }
        } else if (id >= ROW_TRACK && id < ROW_TRACK + LANES) {
            l = id - ROW_TRACK;
            for (i = 0; i < TRACKS; i++)
                track_v[p][l][i] = (int16_t)((in[2 + 2 * i] << 8) | in[3 + 2 * i]);
        }
    }
}

/* A new project (CREATE NEW): no lanes anywhere. */
void core_lanes_new(void)
{
    int32_t p;
    if (!lanes_ready)
        lanes_init();
    for (p = 0; p < PATTERN_COUNT; p++)
        pattern_clear(p);
}

/* ev_step_clear's place: every lock of a step removed (a trig removed). trk: the firmware's track
 * object, +0x2c its pattern, +0x38 its number. */
void core_lanes_step_clear(const uint8_t *trk, int32_t step)
{
    int32_t p, t, l;
    if (!lanes_ready || (uint32_t)step >= STEPS)
        return;
    p = pattern_of(*(const uint8_t *const *)(trk + 0x2c));
    t = *(const int32_t *)(trk + 0x38);
    if (p < 0 || (uint32_t)t >= TRACKS)
        return;
    for (l = 0; l < LANES; l++)
        if (lane_desc[l])
            lock_set(p, l, t, step, -1);
}

/* ---- the knobs ----------------------------------------------------------------------------------- */
/* The held steps of the page's trig-hold manager: h[0] steps 0-31 (bit s), h[1] 32-63. 0: none. */
static int32_t held(void *view, uint32_t h[2])
{
    const uint8_t *mgr = *(const uint8_t *const *)((const uint8_t *)view + 0x6c);
    h[0] = h[1] = 0;
    if (!mgr || !(core_held((void *)mgr) & 0xff))
        return 0;
    h[1] = *(const uint32_t *)(mgr + 0x158);
    h[0] = *(const uint32_t *)(mgr + 0x15c);
    return (h[0] | h[1]) != 0;
}

#define HELD(h, s) (((h)[(s) >> 5] >> ((s) & 31)) & 1)

static int32_t active_track(void)
{
    extern volatile int32_t fw_active_track;
    return fw_active_track & 7;
}

/* core_param_turn's question for a lane: 1 taken (always, for a lane). */
int32_t core_lanes_turn(void *view, int32_t id, int32_t delta, void *mgr)
{
    int32_t l = lane_of(id), p = pattern_of(fw_pattern), t = active_track(), s, d;
    uint32_t h[2];
    (void)mgr;
    if (l < 0)
        return 0;
    if (p < 0)
        return 1;
    d = delta >= 256 || delta <= -256 ? delta / 256 : delta > 0 ? 1 : delta < 0 ? -1 : 0;
    if (held(view, h) && !(lane_desc[l]->lane_flags & LANE_NOLOCK)) {
        for (s = 0; s < STEPS; s++)
            if (HELD(h, s)) {
                int32_t v = lock_value(p, l, t, s);
                lock_set(p, l, t, s, clampv(l, (v >= 0 ? v : track_value(p, l, t)) + d));
            }
    } else {
        track_v[p][l][t] = (int16_t)clampv(l, track_value(p, l, t) + d);
    }
    core_lanes_dirty = 1;
    rearm |= 1u << t;
    return 1;
}

/* core_param_value's question for a lane: 1 answered. */
int32_t core_lanes_value(void *view, int32_t id, int32_t *value, uint8_t *locked)
{
    int32_t l = lane_of(id), p = pattern_of(fw_pattern), t = active_track(), s, v = -1, any = 0;
    uint32_t h[2];
    if (l < 0)
        return 0;
    if (view && !(lane_desc[l]->lane_flags & LANE_NOLOCK))
        any = held(view, h);
    for (s = 0; any && s < STEPS; s++)
        if (HELD(h, s)) {
            v = lock_value(p, l, t, s);
            break;
        }
    if (locked)
        *locked = v >= 0;
    *value = (v >= 0 ? v : track_value(p, l, t)) << 8;
    return 1;
}

/* ev_tick (core's own subscription, order 0): a lane changed, the frame drawn again; and while the sequencer
 * is stopped, the first step of each track whose lanes changed prepared again (PLAY plays the step it
 * prepared ahead: ev_note_built's handlers see it with the lanes as they are now). */
void core_lanes_tick(uint8_t *ctrl)
{
    uint32_t r = rearm;
    if (core_lanes_dirty) {
        core_lanes_dirty = 0;
        ctrl[0x20] = 1;
    }
    if (!r || fw_transport != 0)
        return;
    rearm = 0;
    for (int32_t t = 0; t < TRACKS; t++)
        if (r >> t & 1)
            core_rearm(t);
}

/* ---- each note the sequencer builds (interrupt) ---------------------------------------------------- */
/* The last NOTES notes built, for core_note_step: the event, and what identifies a copy of it (PLAY's first
 * step is built ahead, while stopped, and copied into a fresh event as the sequencer starts): the track,
 * the note count and notes, the flags and the sound. */
#define NOTES 16
static struct note {
    void *event;
    int32_t step;
    uint32_t id[5];                     /* +0x08 track, +0x14 count, +0x18 notes, +0x28 flags, +0x2c sound */
} notes[NOTES];
static uint32_t notes_next;

static void note_id(const uint8_t *ev, uint32_t id[5])
{
    id[0] = *(const uint32_t *)(ev + 0x08);
    id[1] = *(const uint32_t *)(ev + 0x14);
    id[2] = *(const uint32_t *)(ev + 0x18) ^ *(const uint32_t *)(ev + 0x1c);
    id[3] = *(const uint32_t *)(ev + 0x28);
    id[4] = *(const uint32_t *)(ev + 0x2c);
}

/* The step a note event came from (the sequencer's, from ev_voice_on or ev_voice_ready), or -1 (the keys,
 * MIDI, the arpeggiator). Interrupt level or any task. */
int32_t core_note_step(void *event)
{
    uint32_t id[5], i, k, pass;
    if (!event)
        return -1;
    note_id(event, id);
    /* newest first: the event itself (an event given back and taken again for another note is not the
     * same note: its content must match too), then a copy of one */
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < NOTES; i++) {
            k = (notes_next - 1 - i) % NOTES;
            if (notes[k].event && (pass || notes[k].event == event) && notes[k].id[0] == id[0] &&
                notes[k].id[1] == id[1] && notes[k].id[2] == id[2] && notes[k].id[3] == id[3] &&
                notes[k].id[4] == id[4])
                return notes[k].step;
        }
    return -1;
}

/* From the builders' four calls (lanes.s): event the note (0: none), the builder's arguments. */
void core_note_built(void *event, int32_t track, const uint8_t *pattern, int32_t step)
{
    int32_t i;
    struct note *n;
    if (!event)
        return;
    build_pat = pattern_of(pattern);
    for (i = 0; ev_note_built[i]; i++)
        ev_note_built[i](track, step, event);
    build_pat = -1;
    n = &notes[notes_next % NOTES];             /* after the handlers: a sound they changed is in it */
    n->event = event;
    n->step = step;
    note_id(event, n->id);
    notes_next++;
}

/* ---- knobs on stock pages and TRIG's second page ----------------------------------------------------- */
struct page {                           /* core_pages' descriptor, as pages.s reads it */
    int32_t idx, after;
    const char *short_title, *title;
    int32_t ids[8];
    int32_t kind;
    void *view;
    int32_t pos;
};
struct page core_trig2 = { PAGE_TRIG2, PAGE_TRIG, "TRG2", "Trig Parameters", {0}, 9, 0, 0 };
int32_t core_trig2_on;                  /* pages.s: the page is shown when a mod put a knob on it */
static int32_t knobs_done;

/* Called by core_view_init before it builds a view's list, once: the knobs on TRIG's second page, which is
 * then shown. */
void core_knobs_apply(void)
{
    int32_t i;
    if (knobs_done)
        return;
    knobs_done = 1;
    for (i = 0; core_knobs[i]; i++) {
        const struct knob *k = (const struct knob *)core_knobs[i];
        if (k->page == PAGE_TRIG2 && (uint32_t)k->knob < 8 && !core_trig2.ids[k->knob]) {
            core_trig2.ids[k->knob] = k->id;
            core_trig2_on = 1;
        }
    }
}

/* core_page_rec, for a stock page (1-26), after the stock lookup: the knobs mods put on it, each where its box
 * is empty (the static constructors build the records; a lookup always comes after). -> rec. */
void *core_knobs_fix(int32_t idx, uint8_t *rec)
{
    int32_t i, *ids = (int32_t *)(rec + 8);
    if (idx < 1 || idx > 26 || !rec)
        return rec;
    for (i = 0; core_knobs[i]; i++) {
        const struct knob *k = (const struct knob *)core_knobs[i];
        if (k->page == idx && (uint32_t)k->knob < 8 && !ids[k->knob])
            ids[k->knob] = k->id;
    }
    return rec;
}
