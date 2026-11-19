/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.16: the silent-voice skip (SKIPS.md, "The silent-voice skip
 * (0.16)"). Every address is OS 1.74's.
 *
 * The render calls one machine routine per voice slot (8 slots) every block
 * through 0x419ca110, from the slot's first SYN trig until the power goes
 * off, whether the voice sounds or not. Core stops calling it once the slot
 * is silent and its call changes nothing the voice board gets (or only what
 * a machine below is let off), and at the slot's next SYN trig puts the
 * slot's digital state back to what the boot left, so that note starts as
 * the first one after a power-on does.
 *
 * Only while a mod asks for it and none holds it off (core_voice_skip_want
 * nonzero, core_voice_skip_hold 0; analog-rytm-mk2/voice.h); off, the site
 * is stock's (idle.s).
 *
 * Watched (idle.s tests the gain first, then core_voice_quiet), cheapest
 * first; all of these, every block:
 *   - the machine's gain, both words, below 0x1000 (2^-19 of full scale,
 *     -114 dB): the machine's digital part (noise, click) is gone;
 *   - the recorded machine is a public one (0..33) and not CP CLASSIC (6),
 *     whose generator is a global one; machines mods add (34 up) keep state
 *     core can't know;
 *   - no trig of the owner, and the machine's own sequence done or not
 *     waiting for its timer (state < 0, or timer +4 below 0x10);
 *   - the slot's analog amp envelope (0x4010d0fc's sequence, 0x42fb49b4 +
 *     0x20 x slot) in its decay (3) or release (5), for twice the time
 *     stock's own rate-to-time routine (0x4011e7d6) gives for that rate from
 *     full level, plus 375 blocks (0.25 s), against the rate it has now;
 *   - nothing the call reads changed since the last block: the owner track's
 *     0x54-byte parameter block, its pitch, the owner, the idle bit, the
 *     pointers and the machine's sequence state.
 * Then core makes the call itself for QUIET_BLOCKS blocks and checks that it
 * changes nothing in the control frame (0x800063c0, 0x3d8 bytes) or the
 * slot's sequence record (0x8000fef8 + 0x20 x slot: state, timer and the
 * machine's private words), leaves both gain words as they were, and leaves
 * the slot's click buffer (0x80006bf8 + 0x80 x slot) at zero. Passed:
 *   - a byte set from 0 in a place the render clears after each send (byte
 *     +4 of entries 0x15..0x4c, 8 bytes apart, 0x401214d8), the same bytes
 *     every call, at most REPLAY of them, which core writes again in each
 *     skipped block;
 *   - BD PLASTIC, SD NATURAL, BD SHARP and BD ACOUSTIC (21, 23, 26, 30): their
 *     private record words (+8..+0x1c), an envelope that keeps decaying, which
 *     stay where they are while skipped (owner's pick, 2026-11-09);
 *   - XT CLASSIC (8) on slots 3 and 5: its ring-out words (frame +0x7a/+0x7c,
 *     +0x7e/+0x80), which stay where they are while skipped (owner's pick).
 * A call that changes anything else (SY CHIP's arpeggio counter) is left to
 * stock for BLOCKED_BLOCKS blocks before core looks again.
 *
 * Skipped, a block costs a few loads: a trig of the owner, a new owner, a
 * new sequence state or idle bit gives the slot back to stock in that block.
 * Everything else, a knob, an LFO, a kit's values, reaches the voice at the
 * next check: every PROBE_BLOCKS blocks core makes the call and checks it as
 * above, and a change gives the slot back to stock.
 *
 * At the next SYN trig (flag bit 7 of 0x8000e53c) of a slot that was
 * skipped, just before stock's call for that note, its generator and filter
 * struct (0x80007e20 + 0x2c x slot), gain words, resonator buffer
 * (0x80007040 + 0x80 x slot), the machine's private record words
 * (+8..+0x1c) and for slot 7 the two RS words at 0x80007f6c go back to the
 * snapshot core takes at the render's first block. A SYN-off trig resets
 * nothing, as in stock.
 */

#include <stdint.h>

extern volatile uint32_t core_block;    /* load.s: the render's block count */

typedef uint32_t u32a2 __attribute__((aligned(2), may_alias));

#define MACH_REC ((volatile uint32_t *)0x419ca0d0)  /* + slot: the recorded machine */
#define PARAM_PTR ((volatile uint32_t *)0x419ca0f0) /* + slot */
#define IDLE_BITS (*(volatile uint8_t *)0x419ca0cc)
#define TRACK_TRIG ((volatile uint32_t *)0x8000e508) /* + track: 1 trig, 2 stop */
#define TRACK_TRIG_FLAGS ((volatile uint32_t *)0x8000e53c) /* + track: bit 7 SYN */
#define TRACK_FLAGS ((volatile uint8_t *)0x8000ea18)
#define TRACK_PITCH ((volatile uint32_t *)0x80006388)
#define TRACK_PARAMS 0x8000f7baU                    /* + track x 0x54 */
#define PARAM_LONGS 21
#define FRAME ((volatile uint32_t *)0x800063c0)
#define FRAME_LONGS (0x3d8 / 4)
#define RECORD(s) ((volatile uint32_t *)(0x8000fef8U + 0x20U * (s)))
#define GAIN(s) ((volatile uint32_t *)(0x80007000U + 8U * (s)))
#define CLICK(s) ((volatile uint32_t *)(0x80006bf8U + 0x80U * (s)))
#define AMP_STATE(s) (*(volatile int32_t *)(0x42fb49b4U + 0x20U * (s)))
#define AMP_RATES ((const uint32_t *)0x401ef030)
#define RATE_TIME ((int (*)(int, uint32_t))0x4011e7d6)

/* the snapshot's regions: gains + resonator buffers; the structs + RS words;
 * the sequence records */
#define SNAP_A 0x80007000U
#define SNAP_A_LONGS ((0x40 + 0x400) / 4)
#define SNAP_B 0x80007e20U
#define SNAP_B_LONGS (0x160 / 4)
#define SNAP_C 0x8000fef8U
#define SNAP_C_LONGS (0x100 / 4)
#define CLEARED_FIRST (0x15 * 8 + 4)            /* frame offsets the render clears */
#define CLEARED_LAST (0x4c * 8 + 4)
#define REPLAY 4

#define QUIET_BLOCKS 16
#define PROBE_BLOCKS 256
#define BLOCKED_BLOCKS 256
#define SETTLE_BLOCKS 16
#define MARGIN_BLOCKS 375
#define LAST_PUBLIC 33
#define CP_CLASSIC 6
#define XT_CLASSIC 8
/* machines whose private record words may move: 21, 23, 26, 30 */
#define RECORD_FREE ((1U << 21) | (1U << 23) | (1U << 26) | (1U << 30))

enum { WATCH = 0, SKIPPED = 1, BLOCKED = 2 };
enum { IN_N = PARAM_LONGS + 7 };
#define RECORDED 0x1000000U

/* per slot: idle.s clears both longs when the voice sounds.
 *   m: count in bits 0-15, mode in 16-23, bit 24 inputs recorded
 *   amp: blocks in the decay in bits 0-23, the amp state + 1 in 24-31 */
struct voice_state {
    uint32_t m, amp;
};
struct voice_state core_voice_state[8];
static uint32_t inputs[8][IN_N];
static uint32_t amp_rate[8], amp_need[8];
/* a skipped slot's key: owner, idle bit, sequence state when it was skipped */
static uint32_t skip_owner[8], skip_state[8];
static uint8_t skip_idle[8];
static uint32_t snap_a[SNAP_A_LONGS], snap_b[SNAP_B_LONGS], snap_c[SNAP_C_LONGS];
uint8_t core_voice_due;                     /* slots skipped since their last SYN trig */
/* per slot: the frame bytes its quiet call sets, as offset << 8 | value */
static uint32_t replay[8][REPLAY], replay_new[REPLAY];
static uint8_t replays[8];
static uint32_t frame_before[FRAME_LONGS];
/* idle.s: the frame copied by movem, and a bit per 8-long chunk (31 chunks)
 * that differs from the copy (C's two loops were most of the checked call's
 * cost, about 5 and 14 instructions a long) */
extern void core_frame_save(uint32_t *dst);
extern uint32_t core_frame_diff(const uint32_t *before);
_Static_assert(FRAME_LONGS == 246, "idle.s copies 41 x 6 longs and checks 30 x 8 + 6");
static uint8_t snapped;
/* blocks skipped, for the emulator checks and a meter */
uint32_t core_voice_skips;
/* the checked call (a quiet call or a probe): at most one slot makes one in
 * a block. A checked call costs the machine's call plus a copy and a compare
 * of the 0x3d8-byte frame; several in one block (every slot watched from the
 * same block, as after a STOP's trig to every track or the skip's switch-on,
 * and their probes then 256 blocks apart in step) ran that block past the
 * period. The turn starts at slot core_block & 7, so a slot that asks every
 * block gets it within 8 blocks. */
static uint32_t checked_block, checked_slot, turn_block;
static uint8_t asks_now, asks_last;     /* slots that asked this block, the last */

/* 1: slot s makes this block's checked call */
static int check_turn(uint32_t s)
{
    uint32_t b = core_block;
    if (turn_block != b) {
        asks_last = turn_block + 1 == b ? asks_now : 0;
        asks_now = 0;
        turn_block = b;
    }
    asks_now |= 1U << s;
    if (checked_block == b)
        return checked_slot == s;
    /* the slot loop runs 0..7: a slot before this block's start waits while
     * one at or after it asked in the last block */
    uint32_t r = b & 7;
    if (s < r && (asks_last >> r))
        return 0;
    checked_block = b;
    checked_slot = s;
    return 1;
}

/* ev_render_in, after core_load_in: the boot's state, before any machine */
void core_voice_boot(void)
{
    if (snapped)
        return;
    for (int i = 0; i < SNAP_A_LONGS; i++)
        snap_a[i] = ((volatile uint32_t *)SNAP_A)[i];
    for (int i = 0; i < SNAP_B_LONGS; i++)
        snap_b[i] = ((volatile uint32_t *)SNAP_B)[i];
    for (int i = 0; i < SNAP_C_LONGS; i++)
        snap_c[i] = ((volatile uint32_t *)SNAP_C)[i];
    snapped = 1;
}

/* the slot's digital state back to the boot's (not its sequence state and
 * timer, which the trig sets) */
static void reset_slot(uint32_t s)
{
    volatile uint32_t *a = (volatile uint32_t *)SNAP_A, *b = (volatile uint32_t *)SNAP_B;
    volatile uint32_t *c = (volatile uint32_t *)SNAP_C;
    a[2 * s] = snap_a[2 * s];
    a[2 * s + 1] = snap_a[2 * s + 1];
    for (uint32_t i = 0x10 + 0x20 * s; i < 0x30 + 0x20 * s; i++)
        a[i] = snap_a[i];
    for (uint32_t i = 11 * s; i < 11 * s + 11; i++)
        b[i] = snap_b[i];
    for (uint32_t i = 8 * s + 2; i < 8 * s + 8; i++)
        c[i] = snap_c[i];
    if (s == 7) {                       /* 0x80007f6c, 0x80007f70 */
        b[0x53] = snap_b[0x53];
        b[0x54] = snap_b[0x54];
    }
}

static int small(uint32_t g)
{
    return g + 0xfffU <= 0x1ffeU;
}

/* the decay's time is up (or not in a decay); the time is worked out only
 * when the state or the rate changes */
static int amp_done(uint32_t s, uint32_t owner, struct voice_state *v)
{
    int32_t st = AMP_STATE(s);
    if (st != 3 && st != 5) {
        v->amp = 0;
        return 0;
    }
    const uint8_t *p = (const uint8_t *)(TRACK_PARAMS + 0x54U * owner);
    uint32_t rate = AMP_RATES[(int8_t)p[st == 3 ? 0x24 : 0x28]] & 0xfffff;
    uint32_t tag = (uint32_t)(st + 1) << 24;
    if ((v->amp & 0xff000000U) != tag || amp_rate[s] != rate) {
        v->amp = tag;
        amp_rate[s] = rate;
        amp_need[s] = 2U * ((uint32_t)RATE_TIME(0x7f, rate) >> 4) + MARGIN_BLOCKS;
        return 0;
    }
    uint32_t n = v->amp & 0xffffffU;
    if (n < amp_need[s]) {
        v->amp = tag | (n + 1);
        return 0;
    }
    return 1;
}

static uint32_t pitch_of(uint32_t owner)
{
    return (TRACK_FLAGS[owner] & 1) ? TRACK_PITCH[owner] : 0x3c0000U;
}

static void idle_clear(uint32_t s)
{
    if (IDLE_BITS & (1U << s))
        *(volatile uint16_t *)(PARAM_PTR[s] + 4) = 0;
}

static void set(struct voice_state *v, uint32_t mode, uint32_t count, uint32_t rec)
{
    v->m = rec | mode << 16 | count;
}

/* the 16-bit frame words a machine is let off, as a mask over the long at
 * byte offset off */
static uint32_t free_bits(uint32_t mach, uint32_t s, uint32_t off)
{
    if (mach != XT_CLASSIC)
        return 0;
    if (s == 3)                         /* +0x7a/+0x7c: the long at 0x78's low word, 0x7c's high */
        return off == 0x78 ? 0xffffU : off == 0x7c ? 0xffff0000U : 0;
    if (s == 5)                         /* +0x7e/+0x80 */
        return off == 0x7c ? 0xffffU : off == 0x80 ? 0xffff0000U : 0;
    return 0;
}

/* idle.s, the skip off, a trig of a slot that was skipped: its SYN trig
 * still puts back the boot's state */
void core_voice_off(uint32_t s, uint32_t owner)
{
    if ((core_voice_due >> s & 1) && TRACK_TRIG[owner] == 1 && (TRACK_TRIG_FLAGS[owner] & 0x80)) {
        reset_slot(s);
        core_voice_due &= ~(1U << s);
    }
}

/* idle.s, the skip on and the gain small: 0 skips the call, else the
 * pointer runs it */
uint32_t core_voice_quiet(uint32_t s, uint32_t owner, uint32_t ptr)
{
    struct voice_state *v = &core_voice_state[s];
    uint32_t m = v->m, mode = m >> 16 & 0xff, count = m & 0xffff;
    uint32_t trig = TRACK_TRIG[owner];
    int32_t state = (int32_t)RECORD(s)[0];
    uint32_t idle = IDLE_BITS >> s & 1;

    /* skipped: the cheap key, and the call made and checked every PROBE_BLOCKS
     * (later, in the first block that gives this slot the turn) */
    if (mode == SKIPPED && !trig && owner == skip_owner[s] && (uint32_t)state == skip_state[s]
        && idle == skip_idle[s] && (count + 1 < PROBE_BLOCKS || !check_turn(s))) {
        if (idle)
            *(volatile uint16_t *)(PARAM_PTR[s] + 4) = 0;
        for (uint32_t i = 0; i < replays[s]; i++)
            ((volatile uint8_t *)FRAME)[replay[s][i] >> 8] = (uint8_t)replay[s][i];
        core_voice_skips++;
        if (count + 1 < PROBE_BLOCKS)
            v->m = m + 1;
        return 0;
    }

    /* a SYN trig of a slot that was skipped: the boot's state, then stock */
    if (trig) {
        if ((core_voice_due >> s & 1) && trig == 1 && (TRACK_TRIG_FLAGS[owner] & 0x80)) {
            reset_slot(s);
            core_voice_due &= ~(1U << s);
        }
        v->m = 0;
        return ptr;
    }
    uint32_t mach = MACH_REC[s];
    if (mach > LAST_PUBLIC || mach == CP_CLASSIC || !snapped)
        return ptr;
    if (mode == BLOCKED) {
        v->m = count > 1 ? (BLOCKED << 16 | (count - 1)) : 0;
        return ptr;
    }
    int probe = mode == SKIPPED && owner == skip_owner[s] && (uint32_t)state == skip_state[s]
                && idle == skip_idle[s];
    if (mode == SKIPPED && !probe) {
        v->m = 0;                       /* the key changed: stock, watched again */
        return ptr;
    }
    if (!probe && ((state >= 0 && (int32_t)RECORD(s)[1] >= 0x10) || !amp_done(s, owner, v))) {
        v->m = 0;
        return ptr;
    }

    /* the call's inputs, the same as the last block's */
    const u32a2 *pb = (const u32a2 *)(TRACK_PARAMS + 0x54U * owner);
    uint32_t *in = inputs[s];
    int same = (m & RECORDED) != 0;
    uint32_t x[IN_N - PARAM_LONGS] = {pitch_of(owner), owner, idle, mach, ptr, PARAM_PTR[s], (uint32_t)state};
    for (int i = 0; i < PARAM_LONGS; i++) {
        uint32_t w = pb[i];
        if (in[i] != w) {
            in[i] = w;
            same = 0;
        }
    }
    for (int i = 0; i < IN_N - PARAM_LONGS; i++) {
        if (in[PARAM_LONGS + i] != x[i]) {
            in[PARAM_LONGS + i] = x[i];
            same = 0;
        }
    }
    if (!same) {
        /* recorded; inputs that moved (an LFO) are looked at again only after
         * SETTLE_BLOCKS, and a probe that finds new inputs gives the slot to stock */
        v->m = (m & RECORDED) ? (BLOCKED << 16 | SETTLE_BLOCKS) : RECORDED;
        return ptr;
    }

    /* not this slot's turn: stock's call, nothing counted */
    if (!check_turn(s))
        return ptr;

    /* the call, made here, with what it changes */
    volatile uint32_t *rec = RECORD(s), *g = GAIN(s), *ck = CLICK(s);
    uint32_t rec0[8], g0 = g[0], g1 = g[1];
    uint32_t rec_n = (RECORD_FREE >> mach & 1) ? 2 : 8;
    core_frame_save(frame_before);
    for (uint32_t i = 0; i < rec_n; i++)
        rec0[i] = rec[i];
    idle_clear(s);
    ((void (*)(uint32_t, volatile uint32_t *, uint32_t, uint32_t))ptr)(s, FRAME, PARAM_PTR[s], pitch_of(owner));
    int changed = 0;
    for (uint32_t i = 0; i < rec_n; i++)
        changed |= rec0[i] != rec[i];
    uint32_t n = 0;
    /* only the 8-long chunks holding a long that changed are looked at long
     * by long; every long of another chunk is unchanged, which the rule below
     * passes over anyway, so the result is the same */
    uint32_t diff = changed ? 0 : core_frame_diff(frame_before);
    for (uint32_t c = 0; diff && !changed; c++, diff >>= 1) {
        if (!(diff & 1))
            continue;
        for (uint32_t i = 8 * c; i < 8 * c + 8 && i < FRAME_LONGS && !changed; i++) {
            uint32_t off = 4 * i, was = frame_before[i], now = FRAME[i];
            uint32_t d = (was ^ now) & ~free_bits(mach, s, off);
            if (!d)
                continue;
            /* one byte, the long's first, set from 0 where the render clears */
            if (d & 0xffffffU || was >> 24 || off < CLEARED_FIRST
                || off > CLEARED_LAST || (off - CLEARED_FIRST) % 8 || n == REPLAY)
                changed = 1;
            else
                replay_new[n++] = off << 8 | now >> 24;
        }
    }
    if (changed) {
        set(v, BLOCKED, BLOCKED_BLOCKS, 0);
        return 0;
    }
    int settled = g[0] == g0 && g[1] == g1 && small(g0) && small(g1);
    for (int i = 0; i < 32; i++)
        settled &= ck[i] == 0;
    if ((count || probe) && n != replays[s])
        settled = 0;
    for (uint32_t i = 0; i < n && settled && (count || probe); i++)
        settled = replay[s][i] == replay_new[i];
    replays[s] = (uint8_t)n;
    for (uint32_t i = 0; i < n; i++)
        replay[s][i] = replay_new[i];
    if (!settled) {
        set(v, WATCH, 0, RECORDED);
        return 0;
    }
    if (!probe && ++count < QUIET_BLOCKS) {
        set(v, WATCH, count, RECORDED);
        return 0;
    }
    skip_owner[s] = owner;
    skip_state[s] = (uint32_t)state;
    skip_idle[s] = (uint8_t)idle;
    core_voice_due |= 1U << s;
    set(v, SKIPPED, 0, RECORDED);
    return 0;
}
