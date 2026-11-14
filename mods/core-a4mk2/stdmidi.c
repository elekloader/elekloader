/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2 0.7: std1's MIDI and sequencer-tick events (README, "Standard
 * events (0.7)"; elekloader/sdk/include/std1.h, analog-four-mk2/ports.h).
 * The sites are in stdmidi.s; each calls one of these with the stock
 * routine's own arguments, and a nonzero return skips what stock would do.
 *
 *   ev_midi_in    the MIDI task's dispatch of each message by its status
 *                 (0x4006578a DIN, 0x40065818 USB): channel messages, real
 *                 time and SysEx, before stock's handler for that status.
 *   ev_sysex_in   the SysEx router (0x40066320).
 *   ev_midi_cc    stock's apply of one CC to one track (0x40068632), which
 *                 the CC handler calls once per track whose channel matches.
 *   ev_midi_pc    the program-change handler (0x40065e9e), any channel.
 *   ev_midi_out   the three senders: the general send (0x400687aa), the
 *                 OUTPUT TO send (0x40068698) and the real-time byte
 *                 (0x40068704). Below them there is nothing a message can
 *                 reach the ports by without one of them.
 *   ev_seq_midi   the audio interrupt's queueing of a sequencer note-on for
 *                 the note-out task (0x40101acc): only a track whose SEND
 *                 MIDI is on sends notes (pattern track byte +0x2e8, off
 *                 by default).
 *   ev_seq_tick   the sequencer tick interrupt's exit (0x4007badc).
 */

#include <stdint.h>
#include <std1.h>
#include <analog-four-mk2/ports.h>

typedef int (*fn_msg)(const uint8_t *msg, unsigned len, unsigned port);
typedef int (*fn_cc)(int track, int cc, int value, int flags);
typedef int (*fn_pc)(int channel, int program, int flags);
typedef void (*fn_void)(void);

extern const fn_msg std_midi_in[];
extern const fn_msg std_sysex_in[];
extern const fn_cc std_midi_cc[];
extern const fn_pc std_midi_pc[];
extern const fn_msg std_midi_out[];
extern const fn_msg std_seq_midi[];
extern const fn_void std_seq_tick[];

static int run_msg(const fn_msg *f, const uint8_t *msg, unsigned len, unsigned port)
{
    for (; *f; f++)
        if ((*f)(msg, len, port))
            return 1;
    return 0;
}

/* ================================ MIDI in ================================= */

/* the MIDI task's iface bits: 0x08 / 0x10 DIN, 0x02 / 0x04 USB cable 0 (the
 * second of each when the channel is the auto channel) */
static unsigned in_port(int32_t iface)
{
    if (iface & 0x18)
        return PORT_DIN;
    if (iface & 0x06)
        return PORT_USB;
    return 0;
}

/* stdmidi.s, the dispatch sites: the message stock is about to hand to its
 * handler for the status */
int core_std_midi_in(const uint8_t *msg, int32_t len, int32_t port)
{
    if (!std_midi_in[0] || !msg || len <= 0)
        return 0;
    return run_msg(std_midi_in, msg, (unsigned)len, (unsigned)port);
}

int core_std_sysex_in(const uint8_t *msg, int32_t len, int32_t iface)
{
    if (!std_sysex_in[0] || !msg || len <= 0)
        return 0;
    return run_msg(std_sysex_in, msg, (unsigned)len, in_port(iface));
}

/* 0x40068632(track, cc, value, auto, perf): track 0..5 (synth 1..4, FX,
 * CV) for a CC on that track's channel, or on the auto channel while it is
 * the active track; -1 with perf set for a CC on the PERF channel (stock
 * applies those to the kit's performance set). The NRPN and data entry CCs
 * (6, 38, 98, 99) are stock's and core's (midi.c) and are not passed. */
int core_std_midi_cc(int32_t track, int32_t cc, int32_t value, int32_t autoch, int32_t perf)
{
    const fn_cc *f;
    int flags;
    if (!std_midi_cc[0] || cc == 6 || cc == 38 || cc == 98 || cc == 99)
        return 0;
    flags = autoch ? CORE_MIDI_CC_AUTO : 0;
    if (perf || track < 0) {
        track = -1;
        flags |= CORE_MIDI_CC_PERF;
    }
    for (f = std_midi_cc; *f; f++)
        if ((*f)(track, cc, value, flags))
            return 1;
    return 0;
}

/* MIDI CONFIG, as the MIDI task reads it: a record of 0x9b0 bytes chosen by
 * 0x412aa57a, longs at 2 past each field's address */
#define CFG_SEL (*(volatile int32_t *)0x412aa57a)
#define CFG(a) (*(volatile int32_t *)((a) + 2 + CFG_SEL * 0x9b0))
#define CFG_AUTO_CH 0x412aa708u     /* the auto channel */
#define CFG_PC_CH 0x412aa724u       /* PROG CH IN's channel, < 0 the auto channel */
#define CFG_PC_RX 0x412aa754u       /* program changes received */

/* 0x40065e9e(data, len, iface): every program change the task dispatches */
int core_std_midi_pc(const uint8_t *msg, int32_t len, int32_t iface)
{
    const fn_pc *f;
    int ch, autoch, pcch, flags = 0;
    (void)iface;
    if (!std_midi_pc[0] || !msg || len < 2)
        return 0;
    ch = msg[0] & 15;
    autoch = CFG(CFG_AUTO_CH);
    pcch = CFG(CFG_PC_CH);
    if (ch == autoch)
        flags |= CORE_MIDI_CC_AUTO;
    if (CFG(CFG_PC_RX) && ch == (pcch < 0 ? autoch : pcch))
        flags |= CORE_MIDI_PC_STOCK;
    for (f = std_midi_pc; *f; f++)
        if ((*f)(ch, msg[1] & 0x7f, flags))
            return 1;
    return 0;
}

/* =============================== MIDI out ================================= */

/* nonzero while stock sends at all; each sender returns at once while 0 */
#define MIDI_UP (*(volatile int32_t *)0x418717c8)

/* OUTPUT TO, as the senders read it: bit 0 DIN, bit 1 USB (std1's PORT_DIN
 * and PORT_USB) */
static unsigned out_bits(void)
{
    unsigned s = ((unsigned (*)(void))0x401494de)();
    s = ((unsigned (*)(unsigned))0x400a51ca)(s);
    return ((unsigned (*)(unsigned))0x400ab65e)(s) & (PORT_DIN | PORT_USB);
}

/* 0x400687aa(len, data, dest): dest 1 OUTPUT TO, 2 USB, 3 DIN, 4 both */
int core_std_out_send(int32_t len, const uint8_t *msg, int32_t dest)
{
    unsigned port;
    if (!std_midi_out[0] || !MIDI_UP || !msg || len <= 0)
        return 0;
    switch (dest) {
    case 1: port = out_bits(); break;
    case 2: port = PORT_USB; break;
    case 3: port = PORT_DIN; break;
    case 4: port = PORT_DIN | PORT_USB; break;
    default: port = 0; break;
    }
    if (!port)
        return 0;                   /* stock sends nothing */
    return run_msg(std_midi_out, msg, (unsigned)len, port);
}

/* 0x40068698(len, data): to OUTPUT TO's ports (program changes out) */
int core_std_out_bits(int32_t len, const uint8_t *msg)
{
    unsigned port;
    if (!std_midi_out[0] || !MIDI_UP || !msg || len <= 0)
        return 0;
    port = out_bits();
    if (!port)
        return 0;
    return run_msg(std_midi_out, msg, (unsigned)len, port);
}

/* 0x40068704(byte): clock, start, continue and stop to OUTPUT TO's ports,
 * from the clock-out interrupt or a task */
int core_std_out_rt(int32_t byte)
{
    uint8_t b = (uint8_t)byte;
    unsigned port;
    if (!std_midi_out[0] || !MIDI_UP)
        return 0;
    port = out_bits();
    if (!port)
        return 0;
    return run_msg(std_midi_out, &b, 1, port);
}

/* ============================== sequencer ================================= */

/* the note-out task's record (16 bytes, a ring at 0x417f73f0): +0 kind (1 a
 * note), +8 track 0..3, +9 note, +10 velocity, +12 length; its channel is
 * the track's (0x41809664, 16 or more: not sent) */
#define TRACK_CH ((volatile int8_t *)0x41809664)

/* stdmidi.s, before the audio interrupt queues a note: nonzero drops it, and
 * the task then sends neither its note-on nor its note-off */
int core_std_seq_note(const uint8_t *rec)
{
    uint8_t msg[3];
    int ch;
    if (!std_seq_midi[0] || !rec || rec[0] != 1 || rec[8] > 3)
        return 0;
    ch = TRACK_CH[rec[8]];
    if (ch < 0 || ch > 15)
        return 0;                   /* the task sends nothing */
    msg[0] = (uint8_t)(0x90 | ch);
    msg[1] = rec[9] & 0x7f;
    msg[2] = rec[10] & 0x7f;
    return run_msg(std_seq_midi, msg, 3, 0);
}

/* stdmidi.s, at the sequencer tick interrupt's exit */
void core_std_seq_tick(void)
{
    const fn_void *f;
    for (f = std_seq_tick; *f; f++)
        (*f)();
}
