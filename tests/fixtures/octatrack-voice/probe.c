/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A probe of core-ot 0.7's voice events, for ot_emu (octabam's emulator): it counts every event, checks
 * each one's arguments against the firmware's own memory (`pr_bad` stays 0), notes the interrupt level
 * it ran at, and acts on controls a script pokes (one byte a track): change the command byte, leave
 * the binder's pointer stores or its start counter alone, build a track's DSP word as for a frame with no
 * trig, and store a byte in a track's record. The variables are dumped at the end of a run. The test
 * suite only links it (test_the_voice_probe_links); the run is described in mods/core-ot/README.md. */
#include <stdint.h>
#include "octatrack/core.h"

#define U8(a)   (*(volatile uint8_t *)(uintptr_t)(a))
#define S8(a)   (*(volatile int8_t *)(uintptr_t)(a))
#define U16(a)  (*(volatile uint16_t *)(uintptr_t)(a))
#define U32(a)  (*(volatile uint32_t *)(uintptr_t)(a))

enum { E_START, E_REL, E_KILL, E_BIND, E_SEEK, E_TRIG, E_DSPTRIG, E_DSPREC, E_N };
volatile uint32_t pr_magic = 0x50523037;
volatile uint32_t pr_count[E_N];            /* events seen */
volatile uint32_t pr_trk[E_N][8];           /* per track */
volatile uint32_t pr_bad[E_N];              /* arguments that did not match the firmware's memory */
volatile uint32_t pr_ipl[E_N][2];           /* min, max interrupt level in the handler */
volatile uint32_t pr_in_frame[E_N];         /* handlers called between a frame's begin and end */
volatile uint32_t pr_extra[16];             /* [0] pr_start2 calls, [1] the chain's command != the array's */
/* controls (poked), one byte a track */
volatile uint8_t pr_mod_start[8];           /* xor this into the command byte */
volatile uint8_t pr_veto_bind[8];
volatile uint8_t pr_veto_seek[8];
volatile uint8_t pr_veto_dsp[8];
volatile uint8_t pr_mark_rec[8];            /* store this byte at the record's +0x1b (nonzero) */
volatile uint32_t pr_dsp_bad;               /* frames whose command array != what the chain returned */
volatile uint32_t pr_mark_bad, pr_mark_ok;
volatile uint8_t pr_fired[8];
volatile uint8_t pr_final[8];
volatile uint32_t pr_cmd_seen[256];         /* the command bytes ev_voice_start saw */
volatile uint32_t pr_dspw_n[2];             /* DSP words seen by ev_dsp_record: bit 7 (a trig built) clear, set */
volatile uint32_t pr_frame_depth;
volatile uint32_t pr_voice_ptrs[8][2];      /* the voice's arena pointers at the last ev_voice_bind */
volatile uint32_t pr_cnt144[8];             /* the start counter at the last ev_voice_seek */
volatile uint32_t pr_seek_same[8][2];       /* ev_voice_seek by the same flag */
volatile uint32_t pr_trig_flags[8];         /* the flags ev_trig_apply saw, ORed */
volatile uint32_t pr_rel_words[8];          /* the words ev_voice_release saw, ORed */
struct rec { uint32_t kind, a, b, c; };
volatile uint32_t pr_n;
volatile struct rec pr_log[96];

static inline uint32_t ipl(void)
{
    uint32_t sr;
    __asm__ volatile("move.w %%sr,%0" : "=d"(sr));
    return (sr >> 8) & 7;
}

static void note(unsigned e, unsigned trk)
{
    pr_count[e]++;
    if (trk < 8)
        pr_trk[e][trk]++;
    uint32_t l = ipl();
    if (!pr_ipl[e][1] && !pr_ipl[e][0])
        pr_ipl[e][0] = 7;
    if (l < pr_ipl[e][0]) pr_ipl[e][0] = l;
    if (l > pr_ipl[e][1]) pr_ipl[e][1] = l;
    if (pr_frame_depth)
        pr_in_frame[e]++;
}

static void log4(uint32_t kind, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t i = pr_n;
    if (i < 96) {
        pr_log[i].kind = kind; pr_log[i].a = a; pr_log[i].b = b; pr_log[i].c = c;
        pr_n = i + 1;
    }
}

unsigned pr_start(unsigned trk, unsigned cmd)
{
    note(E_START, trk);
    if (trk > 7 || (cmd & 255) == 0 || (cmd & 255) != OT_DSP_CMD(trk)) pr_bad[E_START]++;
    pr_cmd_seen[cmd & 255]++;
    if (pr_n < 8 || (pr_count[E_START] & 31) == 0)
        log4(1, trk, cmd, OT_DSP_CMD(trk));
    return (cmd ^ pr_mod_start[trk & 7]) & 255;
}

unsigned pr_start2(unsigned trk, unsigned cmd)      /* the chain: it sees the first handler's result */
{
    pr_extra[0]++;
    if (trk < 8) {
        pr_fired[trk] = 1;
        pr_final[trk] = cmd & 255;
        if ((cmd & 255) != OT_DSP_CMD(trk)) pr_extra[1]++;     /* the array is stored after the chain */
    }
    return cmd;
}

void pr_release(unsigned trk, unsigned word)
{
    note(E_REL, trk);
    if (trk > 7 || word != OT_MAILBOX(trk) || !(word & OT_MBX_RELEASE)) pr_bad[E_REL]++;
    if (trk < 8) pr_rel_words[trk] |= word;
    log4(2, trk, word, 0);
}

void pr_kill(unsigned trk)
{
    note(E_KILL, trk);
    if (trk > 7) pr_bad[E_KILL]++;
    log4(3, trk, 0, 0);
}

int pr_bind(unsigned trk, volatile uint32_t *voice)
{
    note(E_BIND, trk);
    if (trk > 7 || voice != OT_VOICE(trk)) pr_bad[E_BIND]++;
    if (trk < 8) {
        pr_voice_ptrs[trk][0] = voice[OT_V_ARENA0];
        pr_voice_ptrs[trk][1] = voice[OT_V_ARENA1];
    }
    if (pr_count[E_BIND] < 6) log4(4, trk, (uint32_t)(uintptr_t)voice, voice[OT_V_ARENA0]);
    return trk < 8 && pr_veto_bind[trk];
}

int pr_seek(unsigned trk, volatile uint32_t *voice, unsigned same)
{
    note(E_SEEK, trk);
    if (trk > 7 || voice != OT_VOICE(trk) || same > 1) pr_bad[E_SEEK]++;
    if (trk < 8) {
        pr_cnt144[trk] = voice[OT_V_STARTS];
        pr_seek_same[trk][same & 1]++;
    }
    log4(5, trk, same, voice[OT_V_STARTS]);
    return trk < 8 && pr_veto_seek[trk];
}

void pr_trig(unsigned trk, unsigned flags)
{
    note(E_TRIG, trk);
    if (trk > 7) pr_bad[E_TRIG]++;
    if (trk < 8) pr_trig_flags[trk] |= flags;
    if (pr_count[E_TRIG] < 6) log4(6, trk, flags, 0);
}

int pr_dsp_trig(unsigned trk, unsigned flags)
{
    note(E_DSPTRIG, trk);
    if (trk > 7 || !(flags & 3) || flags != (uint32_t)(int32_t)S8(0x80004858u + 8 * trk)) pr_bad[E_DSPTRIG]++;
    if (pr_count[E_DSPTRIG] < 6) log4(7, trk, flags, 0);
    return trk < 8 && pr_veto_dsp[trk];
}

void pr_dsp_record(unsigned trk, volatile uint16_t *rec)
{
    note(E_DSPREC, trk);
    uint32_t sel = U32(0x800000e0u);
    if (trk > 7 || (uintptr_t)rec != 0x80000110u + (sel << 9) + 64 * trk) pr_bad[E_DSPREC]++;
    if (trk < 8) {
        uint32_t word = U16(0x8000014cu + (sel << 9) + 64 * trk);
        pr_dspw_n[(word & 0x80) ? 1 : 0]++;
        if (pr_mark_rec[trk])
            ((volatile uint8_t *)rec)[0x1b] = pr_mark_rec[trk];
    }
}

void pr_frame_begin(void)
{
    pr_frame_depth = 1;
    for (unsigned t = 0; t < 8; ++t)
        pr_fired[t] = 0;
}

void pr_frame_end(void)
{
    pr_frame_depth = 0;
    uint32_t sel = U32(0x800000e0u);
    for (unsigned t = 0; t < 8; ++t) {
        if (pr_fired[t] && OT_DSP_CMD(t) != pr_final[t]) pr_dsp_bad++;
        if (pr_mark_rec[t]) {
            if (U8(0x80000110u + (sel << 9) + 64 * t + 0x1b) == pr_mark_rec[t]) pr_mark_ok++; else pr_mark_bad++;
        }
    }
}
