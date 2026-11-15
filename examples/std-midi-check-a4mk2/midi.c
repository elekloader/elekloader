/* SPDX-License-Identifier: GPL-2.0-or-later */
/* std-midi-check: core-a4mk2 0.7's std1 MIDI events and ev_seq_tick, one
 * mod built with "events": "std1" against elekloader/sdk/include/std1.h and
 * the A4's ports.h. It takes one thing: in ev_midi_in, the marker, CC 119
 * with value 119 on any channel, so the counts show that a take stops the
 * later events (ev_midi_cc never gets it; marker_cc stays 0).
 *
 * Everything it sees is in std_midi (an exported symbol), for an emulator's
 * or a unit's memory read:
 *   magic        0x4d494449 ("MIDI") once ev_boot ran
 *   midi_in ... seq_tick   each event's calls
 *   in_din, in_usb         ev_midi_in's calls by port; in_other any other
 *   in_msg, in_len, in_port        the last ev_midi_in that was not real time
 *   rt_in                  real-time bytes (0xf8..0xff) through ev_midi_in
 *   took                   markers taken in ev_midi_in
 *   sysex_len, sysex_port, sysex_head   the last ev_sysex_in (its first 4 bytes)
 *   cc_track, cc_cc, cc_value, cc_flags  the last ev_midi_cc
 *   marker_cc              markers that reached ev_midi_cc (0 if the take worked)
 *   pc_channel, pc_program, pc_flags     the last ev_midi_pc
 *   out_msg, out_len, out_port           the last ev_midi_out that was not real time
 *   rt_out                 real-time bytes through ev_midi_out; rt_last the last
 *   out_din, out_usb       ev_midi_out's calls with that port bit
 *   seq_msg, seq_buffer    the last ev_seq_midi
 *   ticks_per_tick         ev_seq_tick's calls between two ev_midi_out clocks (0xf8)
 */
#include <stdint.h>
#include <std1.h>
#include "analog-four-mk2/ports.h"

struct std_midi {
    uint32_t magic;
    uint32_t midi_in, sysex_in, midi_cc, midi_pc, midi_out, seq_midi, seq_tick;
    uint32_t in_din, in_usb, in_other, rt_in, took;
    uint8_t in_msg[4];
    uint32_t in_len, in_port;
    uint32_t sysex_len, sysex_port;
    uint8_t sysex_head[4];
    int32_t cc_track, cc_cc, cc_value, cc_flags;
    uint32_t marker_cc;
    int32_t pc_channel, pc_program, pc_flags;
    uint8_t out_msg[4];
    uint32_t out_len, out_port, rt_out, rt_last, out_din, out_usb;
    uint8_t seq_msg[4];
    uint32_t seq_buffer;
    uint32_t ticks_per_tick, ticks_since;
};
volatile struct std_midi std_midi;

static void copy4(volatile uint8_t *to, const uint8_t *msg, unsigned len)
{
    unsigned i;
    for (i = 0; i < 4; i++)
        to[i] = i < len ? msg[i] : 0;
}

static int is_marker(const uint8_t *msg, unsigned len)
{
    return len >= 3 && (msg[0] & 0xf0) == 0xb0 && msg[1] == 119 && msg[2] == 119;
}

void sm_boot(void)
{
    std_midi.magic = 0x4d494449u;
}

int sm_midi_in(const uint8_t *msg, unsigned len, unsigned port)
{
    std_midi.midi_in++;
    if (port == PORT_DIN)
        std_midi.in_din++;
    else if (port == PORT_USB)
        std_midi.in_usb++;
    else
        std_midi.in_other++;
    if (msg[0] >= 0xf8) {
        std_midi.rt_in++;
        return 0;
    }
    copy4(std_midi.in_msg, msg, len);
    std_midi.in_len = len;
    std_midi.in_port = port;
    if (is_marker(msg, len)) {
        std_midi.took++;
        return 1;
    }
    return 0;
}

int sm_sysex_in(const uint8_t *msg, unsigned len, unsigned port)
{
    std_midi.sysex_in++;
    std_midi.sysex_len = len;
    std_midi.sysex_port = port;
    copy4(std_midi.sysex_head, msg, len);
    return 0;
}

int sm_midi_cc(int track, int cc, int value, int flags)
{
    std_midi.midi_cc++;
    std_midi.cc_track = track;
    std_midi.cc_cc = cc;
    std_midi.cc_value = value;
    std_midi.cc_flags = flags;
    if (cc == 119 && value == 119)
        std_midi.marker_cc++;
    return 0;
}

int sm_midi_pc(int channel, int program, int flags)
{
    std_midi.midi_pc++;
    std_midi.pc_channel = channel;
    std_midi.pc_program = program;
    std_midi.pc_flags = flags;
    return 0;
}

int sm_midi_out(const uint8_t *msg, unsigned len, unsigned port)
{
    std_midi.midi_out++;
    if (port & PORT_DIN)
        std_midi.out_din++;
    if (port & PORT_USB)
        std_midi.out_usb++;
    if (len == 1 && msg[0] >= 0xf8) {
        std_midi.rt_out++;
        std_midi.rt_last = msg[0];
        if (msg[0] == 0xf8) {
            std_midi.ticks_per_tick = std_midi.ticks_since;
            std_midi.ticks_since = 0;
        }
        return 0;
    }
    copy4(std_midi.out_msg, msg, len);
    std_midi.out_len = len;
    std_midi.out_port = port;
    return 0;
}

int sm_seq_midi(const uint8_t *msg, unsigned len, unsigned buffer)
{
    std_midi.seq_midi++;
    copy4(std_midi.seq_msg, msg, len);
    std_midi.seq_buffer = buffer;
    return 0;
}

void sm_seq_tick(void)
{
    std_midi.seq_tick++;
    std_midi.ticks_since++;
}
