/* SPDX-License-Identifier: GPL-2.0-or-later */
/* seq-events for the Octatrack: the sequencer's two record builders, as the events in
 * elekloader/sdk/include/seq-events.h. The Octatrack does not read the pattern when a step fires.
 * One step ahead of its time, the sequencer's tick (an interrupt) calls a builder for each track
 * (0x4009d1e8 for an audio track, 0x4009cf4c for a MIDI track): builder(track, bank, pattern,
 * step, n) reads the step's masks, its timing halfword and its 32 lock bytes from the pattern
 * and stages the record that is applied when the step's time comes (n: the pending slot, -1 for
 * the first step at the transport start and for edits). seq.s hands each build to enter, which
 * runs the stages seq.c offers on a virtual record of the step:
 *
 *     the step the build reads            ev_fire_step         (the argument is replaced)
 *     does it fire                        ev_gen_trig/ev_fire_trig
 *         -> the masks are written for the one build; a step with nothing stored is cleared
 *     after the stock condition           ev_fire_cond         (sq_cond, in the builder)
 *     the notes, velocity and locks       ev_gen_notes... ev_fire_locks, once the step fires
 *         -> the lock bytes are written for the one build
 *
 * and returns through its frame's trampoline (sq_tramp0-2), whose sq_exit puts every byte it
 * wrote back and frees the frame. The builder, its timing and its slides are stock's, on the
 * step's data as the handlers left it: the pattern is the same before and after, and nothing reads
 * it in between but the interrupt that is running. With no handler anywhere, enter changes
 * nothing. The first step is built when PLAY is pressed, before the transport reports playing,
 * and a step is built again when it is edited: the events run in those builds too. */
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

/* seq.c's stages */
extern int32_t sq_on_step(int32_t track, int32_t real, const uint8_t *pat);
extern uint32_t sq_on_trig(int32_t track, int32_t step, int32_t first, uint32_t word);
extern int32_t sq_on_cond(int32_t track, int32_t step, int32_t fails);
extern const void *sq_on_notes(int32_t track, int32_t step, uint32_t word, const void *src);
extern int32_t sq_on_vel(int32_t track, int32_t step, int32_t vel);
extern void sq_on_locks(int32_t track, int32_t step, void *list);
extern void sq_tramp0(void), sq_tramp1(void), sq_tramp2(void);   /* seq.s: a builder's return, by frame */

const uint8_t *volatile sq_fire_pat[SQ_TRACKS];

#define DEPTH 3                         /* builds in flight: one in a task, one in the tick, one spare */
#define SAVED_TIMING 1u

/* Each build in flight has a frame of its own, and the frame's index is in the return address it
 * leaves the builder (sq_tramp0..2): a build finds its frame by where it returns to, whatever
 * order the builds nest or interleave in. */
static void (*const tramps[DEPTH])(void) = { sq_tramp0, sq_tramp1, sq_tramp2 };

/* A build in flight. Everything written into the pattern is kept here, to put back. */
struct frame {
    struct dev_step st;
    uint32_t ret;                       /* the builder's real return address */
    uint32_t saved;                     /* SAVED_TIMING, and a mask's bit in the low nibble <<8 */
    uint32_t mask_old;                  /* bit k: mask kind k's bit as it was */
    uint32_t lock_chg;                  /* bit i: lock byte i was written */
    uint16_t timing_old;
    uint8_t locks_old[32];
};

/* The last build of each track (a repeat of an edit is not a new fire) */
struct built {
    int32_t n, real;
    uint32_t bank, ptn;
};

static struct frame frames[DEPTH];
static uint32_t used;                   /* bit i: frames[i] is a build in flight */
static struct built last[SQ_TRACKS];

static const uint8_t kind_off[4] = { MASK_TRIG, MASK_TRIGLESS, MASK_LOCK, MASK_ONE };

/* The interrupt mask up to 7 for the few instructions that take or give a frame; the old mask is
 * the value to put back */
static inline uint32_t irq_off(void)
{
    uint32_t sr;
    __asm__ volatile("move.w %%sr, %0\n\tmove.w #0x2700, %%sr" : "=d"(sr) : : "cc", "memory");
    return sr;
}

static inline void irq_back(uint32_t sr)
{
    __asm__ volatile("move.w %0, %%sr" : : "d"(sr) : "cc", "memory");
}

static int32_t take(void)
{
    uint32_t sr = irq_off();
    int32_t i;
    for (i = 0; i < DEPTH; i++)
        if (!(used & (1u << i))) {
            used |= 1u << i;
            irq_back(sr);
            return i;
        }
    irq_back(sr);
    return -1;
}

static void give(int32_t i)
{
    uint32_t sr = irq_off();
    used &= ~(1u << i);
    irq_back(sr);
}

static int32_t stages(void)
{
    return ev_fire_step[0] || ev_gen_trig[0] || ev_fire_trig[0] || ev_fire_cond[0] || ev_gen_notes[0] ||
           ev_fire_notes[0] || ev_fire_vel[0] || ev_fire_locks[0];
}

static void set_mask(struct frame *fr, int32_t kind, int32_t on)
{
    const struct dev_step *s = &fr->st;
    uint32_t *m = dev_mask(s->pat, s->track, kind_off[kind], s->step);
    uint32_t bit = 1u << (s->step & 31);
    if (!(fr->saved & (0x100u << kind))) {
        fr->saved |= 0x100u << kind;
        if (*m & bit)
            fr->mask_old |= 1u << kind;
    }
    *m = on ? *m | bit : *m & ~bit;
}

/* Write the stored step as the handlers left it, for this build */
static void materialize(struct frame *fr)
{
    struct dev_step *s = &fr->st;
    uint32_t diff = s->word ^ s->word0;
    int32_t k, kinds = s->track < SQ_AUDIO ? 4 : 3;
    uint8_t *tm;
    for (k = 0; k < kinds; k++)
        if (diff & (1u << k))
            set_mask(fr, k, (s->word >> k) & 1);
    if (s->generated) {                 /* a step with nothing stored: no condition, no timing */
        tm = dev_timing(s->pat, s->track, s->step);
        fr->timing_old = (uint16_t)((tm[0] << 8) | tm[1]);
        fr->saved |= SAVED_TIMING;
        tm[0] = tm[1] = 0;
    }
}

static void flush_locks(struct frame *fr)
{
    uint8_t *real = dev_locks(fr->st.pat, fr->st.track, fr->st.step);
    int32_t i;
    for (i = 0; i < 32; i++)
        if (fr->st.locks[i] != real[i]) {
            if (!(fr->lock_chg & (1u << i))) {
                fr->locks_old[i] = real[i];
                fr->lock_chg |= 1u << i;
            }
            real[i] = fr->st.locks[i];
        }
}

/* The stages that need the step to fire: the notes, the velocity and the locks */
static void fire(struct frame *fr)
{
    struct dev_step *s = &fr->st;
    int32_t vel, was;
    if (s->track < SQ_AUDIO) {
        sq_on_locks(s->track, s->step, s->locks);
    } else if (dev_trig_in(s->word) & SQ_TRIG) {
        sq_on_notes(s->track, s->step, s->word, s);
        if (!s->silent) {
            was = s->locks[SL_VEL] == NOLOCK ? dev_part(s->pat, s->track)[SL_VEL] : s->locks[SL_VEL];
            vel = sq_on_vel(s->track, s->step, was);
            if (vel != was)
                s->locks[SL_VEL] = (uint8_t)vel;
        }
    }
    flush_locks(fr);
}

static void enter(uint32_t *f, int32_t base)
{
    uint32_t t = f[1], bank = f[2], ptn = f[3], real = f[4], kp = 0, kl = 0;
    int32_t n = (int32_t)f[5], track, read, first = 1, i, k;
    const uint8_t *pat, *src;
    struct frame *fr;
    struct built *b;
    struct dev_step *s;
    if (t > 7 || bank > 15 || ptn > 15 || real >= SQ_STEPS || !stages() || (k = take()) < 0)
        return;
    track = base + (int32_t)t;
    pat = dev_bank((int32_t)bank) + PAT_STRIDE * ptn;
    fr = &frames[k];
    s = &fr->st;
    b = &last[track];
    if (n < 0) {                        /* the first step, built when PLAY is pressed (before the transport reports
                                         * playing), or an edit of a step */
        first = !(b->n < 0 && b->bank == bank && b->ptn == ptn && b->real == (int32_t)real);
        kp = sq_clock[track].parity;    /* the clock follows the sequencer's own builds */
        kl = sq_clock[track].last;
    }
    read = sq_on_step(track, (int32_t)real, pat);
    if (n < 0) {
        sq_clock[track].parity = kp;
        sq_clock[track].last = kl;
    }
    if ((uint32_t)read >= SQ_STEPS)
        read = (int32_t)real;
    b->n = n;
    b->bank = bank;
    b->ptn = ptn;
    b->real = (int32_t)real;
    s->pat = pat;
    s->track = track;
    s->step = read;
    s->word0 = dev_step_word(pat, track, read);
    s->word = sq_on_trig(track, read, first, s->word0);
    s->generated = !s->word0 && s->word;
    s->silent = 0;
    fr->saved = fr->mask_old = fr->lock_chg = 0;
    src = dev_locks(pat, track, read);
    for (i = 0; i < 32; i++)
        s->locks[i] = s->generated ? NOLOCK : src[i];
    if (s->word != s->word0)
        materialize(fr);
    fr->ret = f[0];
    f[0] = (uint32_t)tramps[k];
    f[4] = (uint32_t)read;
}

void sq_a_enter(uint32_t *f)
{
    enter(f, 0);
}

void sq_m_enter(uint32_t *f)
{
    enter(f, SQ_AUDIO);
}

/* After the stock condition: its answer (0 passes), and the step as the handlers left it. A step
 * that does not fire is not asked. ret: where the builder will return to, which names its frame */
int32_t sq_cond(int32_t track, int32_t step, int32_t fails, const uint32_t *ret)
{
    struct frame *fr;
    int32_t i;
    for (i = 0; i < DEPTH && *ret != (uint32_t)tramps[i]; i++)
        ;
    if (i == DEPTH)
        return fails;
    fr = &frames[i];
    if (fr->st.track != track || fr->st.step != step || !(dev_trig_in(fr->st.word)))
        return fails;
    fails = sq_on_cond(track, step, fails);
    if (!fails)
        fire(fr);
    return fails;
}

/* The builder's return, to frame i's trampoline: put back what enter and sq_cond wrote. -> the
 * builder's real return address */
uint32_t sq_exit(int32_t i)
{
    struct frame *fr = &frames[i];
    const struct dev_step *s = &fr->st;
    int32_t k, j;
    uint8_t *real, *tm;
    uint32_t *m, bit = 1u << (s->step & 31), ret = fr->ret;
    real = dev_locks(s->pat, s->track, s->step);
    for (j = 0; j < 32; j++)
        if (fr->lock_chg & (1u << j))
            real[j] = fr->locks_old[j];
    if (fr->saved & SAVED_TIMING) {
        tm = dev_timing(s->pat, s->track, s->step);
        tm[0] = (uint8_t)(fr->timing_old >> 8);
        tm[1] = (uint8_t)fr->timing_old;
    }
    for (k = 0; k < 4; k++)
        if (fr->saved & (0x100u << k)) {
            m = dev_mask(s->pat, s->track, kind_off[k], s->step);
            *m = (fr->mask_old >> k) & 1 ? *m | bit : *m & ~bit;
        }
    give(i);                            /* the frame is free: nothing of it is read after this */
    return ret;
}
