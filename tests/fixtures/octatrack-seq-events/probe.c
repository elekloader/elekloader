/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe for seq-events-ot (tests/probe_octatrack_seq_events.py): a handler for every stage, each
 * of which does what the configuration `sp_cfg` says, by track, and nothing when it is zero. The
 * probe writes the configuration into the image's RAM before the sequencer starts. Not a mod to
 * ship: it counts its calls and keeps a small log. */
#include "octatrack/seq.h"

struct cfg {
    uint32_t mask;      /* the tracks the stages act on: bit t */
    int32_t gen;        /* ev_gen_trig: 1 Euclid 5/16 (trigger trigs); 2 a lock trig on step%4 == 1 where
                         * there is none; 3 every trig becomes a lock trig; 4 every lock trig a trig */
    int32_t drop;       /* ev_fire_trig: 1 clears the trig on step%4 == 3 */
    int32_t remap;      /* ev_fire_step: 1 reads the mirrored step */
    int32_t cond;       /* ev_fire_cond: 1 passes every step; 2 fails the even ones */
    int32_t notes;      /* ev_fire_notes: 1 a fifth up and an octave; 2 no note */
    int32_t vel;        /* ev_fire_vel: 1 100 + step */
    int32_t locks;      /* ev_fire_locks: 1 slot 15 (AMP VOL) = 50 + step, slot 0 = 99 */
    int32_t gnotes;     /* ev_gen_notes: 1 the note 48 + step, no chord */
};
struct cfg sp_cfg;
uint32_t sp_calls[8];   /* calls per stage, in the order above */

/* T1's trig stage, in order: the step, first, the trig it was given, the step clock's parity and last step */
struct ent { int8_t step, first, trig, parity, last, pad[3]; };
struct ent sp_log[96];
uint32_t sp_n;

static int on(int32_t track)
{
    return (sp_cfg.mask >> track) & 1;
}

int32_t sp_step(int32_t track, int32_t step, int32_t len)
{
    sp_calls[0]++;
    if (on(track) && sp_cfg.remap == 1)
        return len - 1 - step;
    return step;
}

void sp_gen(int32_t track, int32_t step, int32_t first, uint32_t *trig)
{
    struct ent *e;
    sp_calls[1]++;
    if (track == 0 && sp_n < 96) {
        e = &sp_log[sp_n++];
        e->step = (int8_t)step;
        e->first = (int8_t)first;
        e->trig = (int8_t)*trig;
        e->parity = (int8_t)sq_clock[0].parity;
        e->last = (int8_t)sq_clock[0].last;
    }
    if (!on(track))
        return;
    if (sp_cfg.gen == 1) {
        if ((step % 16) * 5 % 16 < 5)
            *trig |= SQ_TRIG;
        else
            *trig &= ~SQ_TRIG;
    } else if (sp_cfg.gen == 2 && !*trig && (step & 3) == 1) {
        *trig = SQ_LOCK;
    } else if (sp_cfg.gen == 3 && *trig == SQ_TRIG) {
        *trig = SQ_LOCK;
    } else if (sp_cfg.gen == 4 && *trig == SQ_LOCK) {
        *trig = SQ_TRIG;
    }
}

void sp_fire(int32_t track, int32_t step, int32_t first, uint32_t *trig)
{
    (void)first;
    sp_calls[2]++;
    if (on(track) && sp_cfg.drop == 1 && (step & 3) == 3)
        *trig = 0;
}

void sp_cond(int32_t track, int32_t step, int32_t *pass)
{
    sp_calls[3]++;
    if (!on(track))
        return;
    if (sp_cfg.cond == 1)
        *pass = 1;
    else if (sp_cfg.cond == 2 && !(step & 1))
        *pass = 0;
}

void sp_gnotes(int32_t track, int32_t step, int32_t generated, int8_t notes[SQ_NOTES])
{
    (void)generated;
    sp_calls[4]++;
    if (on(track) && sp_cfg.gnotes == 1) {
        notes[0] = (int8_t)(48 + step);
        notes[1] = notes[2] = notes[3] = 0;
    }
}

void sp_notes(int32_t track, int32_t step, int8_t notes[SQ_NOTES])
{
    (void)step;
    sp_calls[5]++;
    if (!on(track))
        return;
    if (sp_cfg.notes == 1) {
        notes[0] += 12;
        if (!notes[1])
            notes[1] = 7;
    } else if (sp_cfg.notes == 2) {
        notes[0] = -1;
    }
}

void sp_vel(int32_t track, int32_t step, int32_t *vel)
{
    sp_calls[6]++;
    if (on(track) && sp_cfg.vel == 1)
        *vel = 100 + step;
}

int32_t sp_locks(int32_t track, int32_t step, struct sq_lock *out, int32_t room)
{
    sp_calls[7]++;
    if (!on(track) || sp_cfg.locks != 1 || room < 2)
        return 0;
    out[0].slot = 15;
    out[0].value = (int16_t)((50 + step) << 8);
    out[1].slot = 0;
    out[1].value = (int16_t)(99 << 8);
    return 2;
}
