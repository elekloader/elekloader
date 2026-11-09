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
    static const struct elek_knob knobs[] = { { "NONE", .max = 1 } };
    #ifdef ELEK_TARGET_SHARC
    void @TAG@_render(const struct elek_voice *v, elek_sample *out);
    #define RENDER @TAG@_render
    #else
    volatile int32_t @TAG@_pitch, @TAG@_starts;
    static void render(const struct elek_voice *v, elek_sample *out)
    {
        @TAG@_pitch = v->pitch;
        @TAG@_starts += v->start != 0;
        for (int i = 0; i < ELEK_VOICE_FRAMES; ++i)
            out[i] = 0;
    }
    #define RENDER render
    #endif
    static const struct elek_machine @TAG@_machine = {
        .id = @ID@, .name = "@TAG@", .short_name = "MONO", ELEK_KNOBS(knobs),
        .pages = { { "MONO", { &knobs[0] } } }, .render = RENDER, .play = @PLAY@, .prio = @PRIO@,
    };
    ELEK_REGISTER_MACHINE(@TAG@_machine);
"""


def sharc_render(tag):
    """The same render on the Digitone II, whose DSP runs it for every slot of the machine's type, each
    block: pitch is the slot's that started last, so another slot's release does not overwrite it.
    R4 = v (start at +8, pitch at +12, state at +20: one per slot), R8 = out."""
    return """
.SECTION/DM seg_dmda;
.GLOBAL {0}_pitch.;
.GLOBAL {0}_starts.;
.VAR {0}_pitch. = 0;
.VAR {0}_starts. = 0;
.VAR {0}_slot. = 0;

.SECTION/SW seg_swco;
.GLOBAL {0}_render.;
{0}_render.:
    R0=20;
    R0=R4+R0;
    I4=R0;
    R11=DM(I4,M5);
    R0=8;
    R0=R4+R0;
    I4=R0;
    R0=DM(I4,M5);
    R0=PASS R0;
    IF EQ JUMP {0}_running;
    R1=DM({0}_starts.);
    R1=R1+1;
    DM({0}_starts.)=R1;
    DM({0}_slot.)=R11;
{0}_running:
    R1=DM({0}_slot.);
    COMP(R1,R11);
    IF NE JUMP {0}_silence;
    R0=12;
    R0=R4+R0;
    I4=R0;
    R10=DM(I4,M5);
    DM({0}_pitch.)=R10;
{0}_silence:
    I4=R8;
    R0=0;
    LCNTR=32, DO {0}_zero UNTIL LCE;
{0}_zero:
    DM(I4,M6)=R0;
    I12=DM(M7,I6);
    JUMP(M14,I12)(DB);
    RFRAME;
    NOP;
""".format(tag)


def machine(tag, play, prio='ELEK_PRIO_LAST'):
    return sdk_machine(tag, MONO_SOURCE, sharc={'render-dn2.s': sharc_render(tag)}, tag=tag, play=play, prio=prio)


MONO = machine('mono', 'ELEK_MONO')
LOW = machine('low', 'ELEK_MONO', 'ELEK_PRIO_LOW')
HIGH = machine('high', 'ELEK_MONO', 'ELEK_PRIO_HIGH')
LEGATO = machine('legato', 'ELEK_MONO_LEGATO')
POLY = machine('poly', 'ELEK_POLY')


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



@sdk_tests
class TrackSetup:
    """The Digitone II's own PLAY MODE and MONO NOTE PRIO (TRK SETUP) carry .play and .prio out there:
    the track's settings follow its SDK machine, and the machine's values win."""
    requires = ('mono', 'trk_setup')

    def setup_after(self, machine):
        self.track.use(machine)
        self.wait(100)
        return self.panel.trk_setup(self.track.index)

    def test_the_track_takes_the_machines_play_mode_and_prio(self):
        self.assertEqual(self.setup_after(LEGATO), (3, 0))
        self.assertEqual(self.setup_after(LOW), (2, 1))
        self.assertEqual(self.setup_after(HIGH), (2, 2))
        self.assertEqual(self.setup_after(POLY), (0, 2))    # POLY: MONO NOTE PRIO shows N/A, left as it was

    def test_a_change_in_trk_setup_goes_back(self):
        self.assertEqual(self.setup_after(MONO), (2, 0))
        self.panel.step_play_mode()
        self.wait(100)
        self.assertEqual(self.panel.trk_setup(self.track.index), (2, 0))

if __name__ == '__main__':
    unittest.main()
