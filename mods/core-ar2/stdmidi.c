/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.17: std1's MIDI events (README, "Standard events (0.17)";
 * elekloader/sdk/include/std1.h, analog-rytm-mk2/ports.h). The sites are in
 * stdmidi.s; each calls one of these with the stock routine's own
 * arguments, and a nonzero return skips what stock would do. core-a4mk2's
 * stdmidi.c is the model: the Rytm's MIDI task has the same shape.
 * examples/std-midi-check-ar2 is a std1 mod over all five.
 *
 *   ev_midi_in    the MIDI task's dispatch of each message by its status
 *                 (0x40082e18 DIN, 0x40082e9e USB): channel messages, real
 *                 time and SysEx, before stock's handler for that status.
 *   ev_midi_cc    stock's apply of one CC to one track (0x40085652), which
 *                 the CC handler (0x4008303a) calls once per track whose
 *                 channel matches, and once for the PERF channel.
 *   ev_midi_pc    the program-change handler (0x40083384), any channel.
 *   ev_midi_out   the three senders: the general send (0x400857ca), the
 *                 port-setting send (0x400856b8) and the real-time byte
 *                 (0x40085724). Channel messages, the sequencer's notes and
 *                 clock, start, continue and stop go through them; SysEx
 *                 dumps and replies go straight to the ports and are not
 *                 reported.
 *   ev_seq_midi   the audio interrupt's queueing of a sequencer note for
 *                 the note-out task (0x401200b2).
 */

#include <stdint.h>
#include <std1.h>
#include <analog-rytm-mk2/ports.h>

typedef int (*fn_msg)(const uint8_t *msg, unsigned len, unsigned port);
typedef int (*fn_cc)(int track, int cc, int value, int flags);
typedef int (*fn_pc)(int channel, int program, int flags);

extern const fn_msg std_midi_in[];
extern const fn_cc std_midi_cc[];
extern const fn_pc std_midi_pc[];
extern const fn_msg std_midi_out[];
extern const fn_msg std_seq_midi[];

static int run_msg(const fn_msg *f, const uint8_t *msg, unsigned len, unsigned port)
{
    for (; *f; f++)
        if ((*f)(msg, len, port))
            return 1;
    return 0;
}

/* ================================ MIDI in ================================= */

/* stdmidi.s, the dispatch sites: the message stock is about to hand to its
 * handler for the status; port is PORT_DIN or PORT_USB */
int core_std_midi_in(const uint8_t *msg, int32_t len, int32_t port)
{
    if (!std_midi_in[0] || !msg || len <= 0)
        return 0;
    return run_msg(std_midi_in, msg, (unsigned)len, (unsigned)port);
}

/* MIDI CONFIG, as the MIDI task reads it: one record of 0xe4 bytes per
 * selector (0x413189af), each field a long */
#define CFG_SEL (*(volatile int32_t *)0x413189af)
#define CFG(a) (*(volatile int32_t *)((a) + CFG_SEL * 0xe4))
#define CFG_AUTO_CH 0x4131931bu     /* the auto channel */
#define CFG_PC_CH 0x41319353u       /* the program change channel, < 0 the auto channel */
#define CFG_PC_RX 0x41319383u       /* program changes received */

/* 0x40085652(track, cc, value, auto, perf): track 0..12 (12 the FX track)
 * for a CC on that track's channel, or on the auto channel while it is the
 * active track; -1 with perf set for a CC on the PERF channel (0x4131935b).
 * The NRPN and data entry CCs (6, 38, 98, 99) are stock's and core's
 * (midi.c) and are not passed. */
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

/* 0x40083384(data, len, iface): every program change the task dispatches */
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
#define MIDI_UP (*(volatile int32_t *)0x419f28a8)

/* the ports setting, as the senders read it (0x4017fa0a, 0x400ae49e,
 * 0x400b55b6): bit 0 DIN, bit 1 USB (std1's PORT_DIN and PORT_USB) */
static unsigned out_bits(void)
{
    unsigned s = ((unsigned (*)(void))0x4017fa0a)();
    s = ((unsigned (*)(unsigned))0x400ae49e)(s);
    return ((unsigned (*)(unsigned))0x400b55b6)(s) & (PORT_DIN | PORT_USB);
}

/* 0x400857ca(len, data, dest): dest 1 the ports setting, 2 USB, 3 DIN, 4
 * both */
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

/* 0x400856b8(len, data): to the ports setting's ports */
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

/* 0x40085724(byte): clock, start, continue and stop to the ports setting's
 * ports, from the clock interrupt or a task */
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

/* the note-out task's record (0x4009127e reads it from the queue
 * 0x41908de0): +0 kind (1 a note), +8 track 0..11, +9 note, +10 velocity,
 * +12 length; its channel is the track's (0x4191b074, 16 or more: not
 * sent), and the task drops every note while 0x4190b060 is nonzero */
#define TRACK_CH ((volatile uint8_t *)0x4191b074)
#define NOTES_OFF (*(volatile int32_t *)0x4190b060)

/* stdmidi.s, before the audio interrupt queues a note: nonzero drops it, and
 * the task then sends neither its note-on nor its note-off */
int core_std_seq_note(const uint8_t *rec)
{
    uint8_t msg[3];
    unsigned ch;
    if (!std_seq_midi[0] || !rec || rec[0] != 1 || rec[8] > 11 || NOTES_OFF)
        return 0;
    ch = TRACK_CH[rec[8]];
    if (ch > 15)
        return 0;                   /* the task sends nothing */
    msg[0] = (uint8_t)(0x90 | ch);
    msg[1] = rec[9] & 0x7f;
    msg[2] = rec[10] & 0x7f;
    return run_msg(std_seq_midi, msg, 3, 0);
}
