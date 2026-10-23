/* SPDX-License-Identifier: GPL-2.0-or-later */
/* seq-events: the step fire as events (elekloader/sdk/include/seq-events.h), the part every device
 * shares. Each device's mod (mods/seq-events-dn1) builds it with its own sites (seq.s) and its
 * device layer (dev.h: the firmware's layouts, as static inline functions this file inlines).
 *   Interrupt level (seq.s, the audio and MIDI tracks' fires; lookups only, no firmware calls):
 *     sq_on_step: the step clock of every track (the real step), then ev_fire_step's handlers.
 *     sq_on_trig: ev_gen_trig's handlers, then ev_fire_trig's.
 *     sq_on_cond, sq_on_prob: ev_fire_cond's.
 *     sq_on_notes: the stored notes, or a generated trig's NOTE, in a buffer of ours:
 *       ev_gen_notes' handlers, then ev_fire_notes'.
 *     sq_on_vel: ev_fire_vel's; sq_on_locks: ev_fire_locks' (audio tracks).
 *   sq_track_info, sq_stored: the pattern the device layer names, read as data when asked (no
 *     object methods: a handler may run before the project objects are built).
 *   ev_tick: the clocks' lengths (order 10), and the re-arms asked for (order 90). */
#include <stdint.h>
#include "dev.h"

extern const ev_fire_step_fn ev_fire_step[];
extern const ev_gen_trig_fn ev_gen_trig[];
extern const ev_fire_trig_fn ev_fire_trig[];
extern const ev_fire_cond_fn ev_fire_cond[];
extern const ev_gen_notes_fn ev_gen_notes[];
extern const ev_fire_notes_fn ev_fire_notes[];
extern const ev_fire_vel_fn ev_fire_vel[];
extern const ev_fire_locks_fn ev_fire_locks[];

struct sq_clock sq_clock[SQ_TRACKS];
static int32_t clock_len[SQ_TRACKS];    /* the UI task's: a 1-step track wraps at every step */
static int32_t inited, ready_ticks;
static uint32_t rearm_due;              /* tracks asked for this tick */
static int8_t notes_buf[SQ_TRACKS][SQ_NOTES];

int32_t sq_track_info(int32_t track, struct sq_track *out, uint32_t size)
{
    const uint8_t *pat = dev_pattern(), *from;
    uint8_t *to = (uint8_t *)out;
    struct sq_track info;
    uint32_t i;
    if ((uint32_t)track >= SQ_TRACKS || !out || !pat || dev_track_info(pat, track, &info) < 0)
        return -1;
    if (size >= sizeof info) {          /* field by field: a struct copy is a call to memcpy */
        out->len = info.len;
        out->master = info.master;
        out->note = info.note;
        out->root = info.root;
        out->scale = info.scale;
        out->pad = 0;
        return 0;
    }
    from = (const uint8_t *)&info;
    for (i = 0; i < size; i++)          /* a mod built against a shorter struct */
        to[i] = from[i];
    return 0;
}

uint64_t sq_stored(int32_t track)
{
    const uint8_t *pat = dev_pattern();
    uint32_t half[2] = { 0, 0 };        /* 32-bit shifts: a 64-bit one is a call to libgcc */
    int32_t len, master, s;
    if ((uint32_t)track >= SQ_TRACKS || !pat || (len = dev_track_len(pat, track, &master)) < 0)
        return 0;
    for (s = 0; s < len; s++)
        if (dev_trig_in(dev_step_word(pat, track, s)) & SQ_TRIG)
            half[s >> 5] |= 1u << (s & 31);
    return (uint64_t)half[1] << 32 | half[0];
}

void sq_rearm(uint32_t tracks)
{
    rearm_due |= tracks;
}

/* ev_tick, order 10 (before the mods'): the clocks' lengths */
void sq_tick(void *ctrl)
{
    const uint8_t *pat = dev_pattern();
    int32_t lens[SQ_TRACKS], t, master;
    (void)ctrl;
    if (!inited) {
        for (t = 0; t < SQ_TRACKS; t++) {
            sq_clock[t].parity = 0;
            sq_clock[t].last = SQ_CLOCK_NONE;
            clock_len[t] = 16;
        }
        inited = 1;
    }
    if (!pat)
        return;
    for (t = 0; t < SQ_TRACKS; t++)
        if ((lens[t] = dev_track_len(pat, t, &master)) < 0)
            return;                     /* not a pattern yet: next tick */
    for (t = 0; t < SQ_TRACKS; t++)
        clock_len[t] = lens[t];
    if (ready_ticks < 2)
        ready_ticks++;
}

/* ev_tick, order 90 (after the mods'): the re-arms asked for, once (a second in a tick would
 * prepare the first step twice); not before the second tick with a pattern, nor while playing */
void sq_tick_end(void *ctrl)
{
    uint32_t tracks = rearm_due;
    int32_t t;
    (void)ctrl;
    rearm_due = 0;
    if (ready_ticks < 2 || !tracks || dev_playing())
        return;
    for (t = 0; t < SQ_TRACKS; t++)
        if (tracks & (1u << t))
            dev_rearm(t);
}

/* ---- the fires (interrupt level, seq.s) --------------------------------------------------- */

/* The step load: the clock sees the real step, the handlers pick the step read. pat: the pattern
 * this fire reads. -> the step to read */
static const uint8_t *volatile fire_pat[SQ_TRACKS];

/* The pattern buffer track's fire reads: in a fire (an ev_fire_step, ev_gen_trig, ... handler), the one
 * it is reading, which at a pattern switch can be the next pattern while the active pointer still names
 * the old; outside a fire, the one its last fire read. 0 before any fire. (0.2) */
const void *sq_fire_pattern(int32_t track)
{
    return (uint32_t)track < SQ_TRACKS ? (const void *)fire_pat[track] : 0;
}

int32_t sq_on_step(int32_t track, int32_t real, const uint8_t *pat)
{
    struct sq_clock *ck;
    const ev_fire_step_fn *h;
    int32_t len, master, read = real, r;
    if ((uint32_t)track >= SQ_TRACKS || (uint32_t)real >= SQ_STEPS)
        return real;
    dev_fire_begin(track, pat);
    fire_pat[track] = pat;
    ck = &sq_clock[track];
    if (ck->last != SQ_CLOCK_NONE && ((uint32_t)real < ck->last || clock_len[track] == 1))
        ck->parity ^= 1;                /* a wrap: the parity before the step */
    ck->last = (uint32_t)real;
    if (!ev_fire_step[0] || !DEV_IN_DDR(pat))
        return real;
    if ((len = dev_track_len(pat, track, &master)) < 0)
        len = SQ_STEPS;
    for (h = ev_fire_step; *h; h++)
        if ((uint32_t)(r = (*h)(track, read, len)) < (uint32_t)len)
            read = r;
    return read;
}

/* Both reads of the step word. -> the word, its trig bits as the handlers leave them */
uint32_t sq_on_trig(int32_t track, int32_t step, int32_t first, uint32_t word)
{
    const ev_gen_trig_fn *g;
    const ev_fire_trig_fn *f;
    uint32_t trig = dev_trig_in(word);
    if ((uint32_t)track >= SQ_TRACKS || (uint32_t)step >= SQ_STEPS)
        return word;
    for (g = ev_gen_trig; *g; g++) {
        (*g)(track, step, first, &trig);
        trig &= SQ_TRIG | SQ_LOCK;
    }
    for (f = ev_fire_trig; *f; f++) {
        (*f)(track, step, first, &trig);
        trig &= SQ_TRIG | SQ_LOCK;
    }
    return dev_trig_out(word, trig);
}

static int32_t cond_of(int32_t track, int32_t step, int32_t pass)
{
    const ev_fire_cond_fn *h;
    int32_t was;
    if ((uint32_t)track >= SQ_TRACKS)
        return pass;
    for (h = ev_fire_cond; *h; h++) {
        was = pass;
        (*h)(track, step, &pass);
        if (pass < -1 || pass > 1)
            pass = was;
    }
    return pass;
}

/* After a trig condition's call: fails, its answer (0 passes). -> the answer */
int32_t sq_on_cond(int32_t track, int32_t step, int32_t fails)
{
    int32_t pass = cond_of(track, step, fails == 0);
    if (pass == -1 || pass == (fails == 0))
        return fails;
    return pass ? 0 : 1;
}

/* A probability before it is asked. -> the probability to ask with: 100 always, 0 never */
int32_t sq_on_prob(int32_t track, int32_t step, int32_t prob)
{
    int32_t pass = cond_of(track, step, -1);
    return pass == 1 ? 100 : pass == 0 ? 0 : prob;
}

/* The notes a fired note trig plays. word: the step word as fired; src: what the site gives the
 * device layer. -> what the site gets (dev_notes_out), 0 for the stock notes */
const void *sq_on_notes(int32_t track, int32_t step, uint32_t word, const void *src)
{
    const ev_gen_notes_fn *g;
    const ev_fire_notes_fn *f;
    int8_t *n;
    int32_t generated;
    if ((uint32_t)track >= SQ_TRACKS || (uint32_t)step >= SQ_STEPS ||
        !(dev_trig_in(word) & SQ_TRIG))
        return 0;
    generated = dev_generated(track, step, src);
    if (!generated && !ev_fire_notes[0] && !ev_gen_notes[0])
        return 0;                       /* nothing to change: the step's own */
    n = notes_buf[track];               /* no handler writes the pattern */
    dev_notes_in(track, step, src, generated, n);
    for (g = ev_gen_notes; *g; g++)
        (*g)(track, step, generated, n);
    for (f = ev_fire_notes; *f; f++)
        (*f)(track, step, n);
    return dev_notes_out(track, step, src, generated, n);
}

int32_t sq_on_vel(int32_t track, int32_t step, int32_t vel)
{
    const ev_fire_vel_fn *h;
    if ((uint32_t)track >= SQ_TRACKS || !ev_fire_vel[0])
        return vel;
    for (h = ev_fire_vel; *h; h++) {
        (*h)(track, step, &vel);
        vel = vel < 0 ? 0 : vel > 127 ? 127 : vel;
    }
    return vel;
}

/* The audio tracks' fire's lock list (0: none), after its own locks */
void sq_on_locks(int32_t track, int32_t step, void *list)
{
    struct sq_lock got[SQ_LOCKS];
    const ev_fire_locks_fn *h;
    int32_t n = 0, i, k;
    if ((uint32_t)track >= SQ_AUDIO || !ev_fire_locks[0] || !dev_list_ok(list))
        return;
    for (h = ev_fire_locks; *h && n < SQ_LOCKS; h++)
        if ((k = (*h)(track, step, got + n, SQ_LOCKS - n)) > 0 && k <= SQ_LOCKS - n)
            n += k;
    for (i = 0; i < n; i++)
        if (got[i].slot < SQ_SLOTS)
            dev_lock_set(list, got[i].slot, got[i].value);
}
