# SPDX-License-Identifier: GPL-2.0-or-later
"""Mono machines (.play and .prio): the companion keeps the keys held on a track and plays the one .prio
chooses, restarting the AMP (ELEK_MONO) or not (ELEK_MONO_LEGATO) at each change.

The machine records the pitch its render was given and counts the blocks with start set, so each check
reads what the device did with the keys: a new start is the AMP restarted, none is legato.
Run as tests/sdk_alpha_harness.py says.
"""
import unittest
from sdk_alpha_harness import sdk_machine, sdk_tests

MONO_SOURCE = """
    #include "elekloader/sdk_alpha_machine.h"
    volatile int32_t @TAG@_pitch, @TAG@_starts;
    static const struct elek_knob knobs[] = { { "NONE", .max = 1 } };
    static void render(const struct elek_voice *v, elek_sample *out)
    {
        @TAG@_pitch = v->pitch;
        @TAG@_starts += v->start != 0;
        for (int i = 0; i < ELEK_VOICE_FRAMES; ++i)
            out[i] = 0;
    }
    static const struct elek_machine @TAG@_machine = {
        .id = @ID@, .name = "@TAG@", .short_name = "MONO", ELEK_KNOBS(knobs),
        .pages = { { "MONO", { &knobs[0] } } }, .render = render, .play = @PLAY@, .prio = @PRIO@,
    };
    ELEK_REGISTER_MACHINE(@TAG@_machine);
"""
MONO = sdk_machine('mono', MONO_SOURCE, tag='mono', play='ELEK_MONO', prio='ELEK_PRIO_LAST')
LOW = sdk_machine('low', MONO_SOURCE, tag='low', play='ELEK_MONO', prio='ELEK_PRIO_LOW')
HIGH = sdk_machine('high', MONO_SOURCE, tag='high', play='ELEK_MONO', prio='ELEK_PRIO_HIGH')
LEGATO = sdk_machine('legato', MONO_SOURCE, tag='legato', play='ELEK_MONO_LEGATO', prio='ELEK_PRIO_LAST')

POLY = sdk_machine('poly', """
    #include "elekloader/sdk_alpha_machine.h"
    volatile int32_t poly_pitch, poly_starts;
    static const struct elek_knob knobs[] = { { "NONE", .max = 1 } };
    static void render(const struct elek_voice *v, elek_sample *out)
    {
        poly_pitch = v->pitch;
        poly_starts += v->start != 0;
        for (int i = 0; i < ELEK_VOICE_FRAMES; ++i)
            out[i] = 0;
    }
    static const struct elek_machine poly_machine = {
        .id = @ID@, .name = "poly", .short_name = "POLY", ELEK_KNOBS(knobs),
        .pages = { { "NONE", { &knobs[0] } } }, .render = render,
    };
    ELEK_REGISTER_MACHINE(poly_machine);
""")


@sdk_tests
class Mono:
    audio = True
    requires = ('mono',)

    def play(self, machine=MONO):
        self.track.use(machine)
        self.prefix = machine.name
        self.panel.keyboard()
        self.wait(50)

    def key(self, key, down):
        (self.panel.press_key if down else self.panel.release_key)(key)
        self.wait(60)

    def heard(self):
        """The note the render plays, in semitones from the keyboard's first key, and the starts so far."""
        if not hasattr(self, 'base'):
            raise AssertionError('press key 0 first')
        return (self.read(self.prefix + '_pitch') >> 16) - self.base, self.read(self.prefix + '_starts')

    def all_up(self, *keys):
        """Release the keys; the firmware then counts none held on the track: the stock was given one
        release for each note it played."""
        for key in keys:
            self.key(key, False)
        self.assertEqual(self.panel.held_keys(self.track.index), 0)

    def first(self):
        self.key(0, True)
        self.base = self.read(self.prefix + '_pitch') >> 16
        return self.heard()

    def test_last_note_returns_to_the_key_still_held(self):
        self.play()
        _, starts = self.first()
        self.key(4, True)
        self.assertEqual(self.heard(), (4, starts + 1))
        self.key(4, False)
        self.assertEqual(self.heard(), (0, starts + 2))         # C again, the AMP restarted
        self.all_up(0)

    def test_releasing_an_older_key_keeps_the_newest(self):
        self.play()
        _, starts = self.first()
        self.key(4, True)
        self.key(0, False)
        self.assertEqual(self.heard(), (4, starts + 1))
        self.key(4, False)
        self.key(0, True)                                       # a key pressed again after
        self.assertEqual(self.heard(), (0, starts + 2))
        self.all_up(0)

    def test_low_note_keeps_the_lowest_key(self):
        self.play(LOW)
        self.first()
        self.key(0, False)
        self.key(4, True)
        _, starts = self.heard()
        self.key(7, True)                                       # above: E sounds on
        self.assertEqual(self.heard(), (4, starts))
        self.key(0, True)                                       # below: C
        self.assertEqual(self.heard(), (0, starts + 1))
        self.key(0, False)                                      # E returns
        self.assertEqual(self.heard(), (4, starts + 2))
        self.all_up(7, 4)

    def test_high_note_keeps_the_highest_key(self):
        self.play(HIGH)
        _, starts = self.first()
        self.key(4, True)                                       # above: E
        self.assertEqual(self.heard(), (4, starts + 1))
        self.key(0, False)
        self.key(0, True)                                       # below: E sounds on
        self.assertEqual(self.heard(), (4, starts + 1))
        self.key(4, False)                                      # C returns
        self.assertEqual(self.heard(), (0, starts + 2))
        self.all_up(0)

    def test_single_trigger_moves_the_pitch_with_the_amp_running_on(self):
        self.play(LEGATO)
        _, starts = self.first()
        self.key(4, True)
        self.assertEqual(self.heard(), (4, starts))             # legato: start clear
        self.key(4, False)
        self.assertEqual(self.heard(), (0, starts))
        self.all_up(0)
        self.key(7, True)                                       # after every key is up: a new note
        self.assertEqual(self.heard(), (7, starts + 1))
        self.key(4, True)
        self.all_up(7, 4)                                       # the key the stock holds released first

    def test_a_poly_machine_plays_every_key(self):
        self.play(POLY)
        _, starts = self.first()
        self.key(4, True)
        self.key(4, False)
        self.assertEqual(self.heard(), (4, starts + 1))         # no return to C: the stock's own way


@sdk_tests
class MidiIn(Mono):
    """Mono's checks again, with MIDI notes on the track's channel for the trig keys."""
    requires = ('mono', 'midi_in')

    def key(self, key, down):
        self.panel.midi_note(self.track.index, 60 + key, 100 if down else 0)
        self.wait(60)


@sdk_tests
class OneVoice:
    """Where the tracks share the voices, a mono machine's track sounds on one, whichever the stock gives
    each note."""
    audio = True
    requires = ('mono', 'polyphony')

    def voices_after(self, machine, keys):
        self.track.use(machine)
        self.panel.keyboard()
        self.wait(50)
        for key in keys:
            self.panel.press_key(key)
            self.wait(60)
        return self.panel.sounding_voices(self.track.index)

    def test_a_mono_track_sounds_on_one_voice(self):
        self.assertEqual(self.voices_after(MONO, (0, 4, 7)), 1)
        for key in (7, 4):                                      # back to E, then C: retriggers
            self.panel.release_key(key)
            self.wait(60)
            self.assertEqual(self.panel.sounding_voices(self.track.index), 1)

    def test_a_poly_machine_sounds_on_a_voice_a_key(self):
        self.assertEqual(self.voices_after(POLY, (0, 4)), 2)


if __name__ == '__main__':
    unittest.main()
