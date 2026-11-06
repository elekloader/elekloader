/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sidecar-check (Analog Rytm MKII), part 7: the interrupt send and the step
 * getters (core 0.11), checked in RAM (README). On command each
 * ev_seq_step (or each ev_seq_tick, for 96 ticks) sends one SysEx of a
 * set length from the clock's interrupt through core_midi_try_send,
 * numbered so a USB capture shows order and loss; core's own counts are in
 * its record MIRQ. Each ev_seq_tick keeps core_track_length; a command
 * reads core_step_trig for all 13 tracks of the pattern playing. */
#include <stdint.h>
#include "analog-rytm-mk2/seq.h"
#include "analog-rytm-mk2/moments.h"

enum {
    CMD7_NONE,
    CMD7_STEP,          /* arg = length (8..128): one SysEx at each ev_seq_step; 0 stops */
    CMD7_CLEAR,         /* the counters back to 0 */
    CMD7_TRIGS,         /* core_step_trig of the pattern playing, into note[] and lock[] */
    CMD7_TICK,          /* arg = length: one SysEx at each of the next 96 ev_seq_ticks */
};

struct {
    char magic[4];                              /* SCK7 */
    volatile int32_t cmd, arg;
    int32_t done;
    int32_t len;                                /* the step sends' length, 0 off */
    int32_t tick_left, tick_len;                /* CMD7_TICK's sends to go, their length */
    uint32_t seq;                               /* the next message's number */
    uint32_t sent, refused;                     /* core_midi_try_send's answers */
    int16_t length[13];                         /* core_track_length at the last tick */
    int16_t bad;                                /* a getter answered out of range */
    int32_t trig_pattern;                       /* CMD7_TRIGS: the pattern read */
    uint32_t note[13][2], lock[13][2];          /* steps 0..31, 32..63 */
} sc7 = {{'S', 'C', 'K', '7'}};

/* F0 7D 37 <seq, 4 x 7 bits> <len - 8 bytes of (seq + i) & 0x7f> F7 */
static void send_one(int len)
{
    unsigned char m[128];
    uint32_t n = sc7.seq;
    int i;
    m[0] = 0xf0;
    m[1] = 0x7d;
    m[2] = 0x37;
    m[3] = (unsigned char)(n >> 21 & 0x7f);
    m[4] = (unsigned char)(n >> 14 & 0x7f);
    m[5] = (unsigned char)(n >> 7 & 0x7f);
    m[6] = (unsigned char)(n & 0x7f);
    for (i = 7; i < len - 1; i++)
        m[i] = (unsigned char)((n + (unsigned)i) & 0x7f);
    m[len - 1] = 0xf7;
    if (core_midi_try_send(len, m)) {
        sc7.sent++;
        sc7.seq++;
    } else
        sc7.refused++;
}

void sc7_seq_step(uint32_t steps, uint32_t trigs)
{
    (void)steps;
    (void)trigs;
    if (sc7.len >= 8 && sc7.len <= 128)
        send_one(sc7.len);
}

void sc7_seq_tick(void)
{
    int t, l;
    for (t = 0; t < 13; t++) {
        l = core_track_length(t);
        if (l > 1024)
            sc7.bad++;
        sc7.length[t] = (int16_t)l;
    }
    if (sc7.tick_left > 0) {
        sc7.tick_left--;
        send_one(sc7.tick_len);
    }
}

void sc7_tick(void *ctrl)
{
    int t, s, k, p;
    (void)ctrl;
    if (!sc7.cmd)
        return;
    switch (sc7.cmd) {
    case CMD7_STEP:
        sc7.tick_left = 0;
        sc7.len = sc7.arg;
        break;
    case CMD7_CLEAR:
        sc7.seq = sc7.sent = sc7.refused = 0;
        sc7.bad = 0;
        break;
    case CMD7_TRIGS:
        p = core_pattern_now();
        sc7.trig_pattern = p;
        for (t = 0; t < 13; t++) {
            sc7.note[t][0] = sc7.note[t][1] = sc7.lock[t][0] = sc7.lock[t][1] = 0;
            for (s = 0; s < 64; s++) {
                k = core_step_trig(p < 0 ? 0 : p, t, s);
                if (k == CORE_STEP_NOTE)
                    sc7.note[t][s / 32] |= 1u << (s % 32);
                else if (k == CORE_STEP_LOCK)
                    sc7.lock[t][s / 32] |= 1u << (s % 32);
                else if (k)
                    sc7.bad++;
            }
        }
        if (core_step_trig(128, 0, 0) != -1 || core_step_trig(0, 13, 0) != -1 ||
            core_step_trig(0, 0, 64) != -1 || core_track_length(13) != -1)
            sc7.bad++;
        break;
    case CMD7_TICK:
        sc7.tick_len = sc7.arg < 8 ? 8 : sc7.arg > 128 ? 128 : sc7.arg;
        sc7.tick_left = 96;
        break;
    }
    sc7.cmd = 0;
    sc7.done++;
}
