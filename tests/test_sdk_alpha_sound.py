# SPDX-License-Identifier: GPL-2.0-or-later
"""A machine's .render makes its sound: a sine at the note played, tuned by a FINE knob, measured in
what the device outputs. Without .render a machine plays what it did before (nothing on the
Digitakt, FM on the Digitones).

Run as tests/sdk_alpha_harness.py says.
"""
import unittest

from sdk_alpha_harness import ROOT, sdk_machine, sdk_tests

# SINE's render on the Digitone II, whose DSP plays it: the probes below, then examples/sdk-sine's
# sine (sine-dn2.s), which reads FINE as cents too. R4 = v, R8 = out, as the dispatcher calls it.
SINE_PROBES_DN2 = """
.SECTION/DM seg_dmda;
.GLOBAL sound_test_pitch.;
.GLOBAL sound_test_control.;
.GLOBAL sound_test_start_count.;
.GLOBAL sound_test_overlap.;
.GLOBAL sound_test_starts.;
.VAR sound_test_pitch. = 0;
.VAR sound_test_control. = 0;
.VAR sound_test_start_count. = 0;
.VAR sound_test_overlap. = 0;
.VAR sound_test_starts. = 0;        // [2][3]: track, state, pitch
.VAR = 0;
.VAR = 0;
.VAR = 0;
.VAR = 0;
.VAR = 0;

.SECTION/SW seg_swco;
.GLOBAL sound_test_render.;
sound_test_render.:
    I4=R4;
    R0=DM(I4,M5);                   // knob
    I4=R0;
    R0=DM(I4,M5);                   // FINE
    DM(sound_test_control.)=R0;
    R3=4;
    R0=R4+R3;
    I4=R0;
    R9=DM(I4,M5);                   // track
    R0=12;
    R0=R4+R0;
    I4=R0;
    R10=DM(I4,M5);                  // pitch
    DM(sound_test_pitch.)=R10;
    R0=20;
    R0=R4+R0;
    I4=R0;
    R11=DM(I4,M5);                  // state
    R0=8;
    R0=R4+R0;
    I4=R0;
    R0=DM(I4,M5);                   // start
    R1=DM(sound_test_start_count.);
    R0=PASS R0;
    IF EQ JUMP sound_test_started;
    R2=2;
    COMP(R1,R2);
    IF GE JUMP sound_test_counted;
    R2=12;
    R2=R1*R2 (SSI);
    R0=sound_test_starts.;
    R2=R2+R0;
    I4=R2;
    DM(I4,M5)=R9;
    R2=R2+R3;
    I4=R2;
    DM(I4,M5)=R11;
    R2=R2+R3;
    I4=R2;
    DM(I4,M5)=R10;
sound_test_counted:
    R1=R1+1;
    DM(sound_test_start_count.)=R1;
sound_test_started:
    R2=2;
    COMP(R1,R2);
    IF LT JUMP sound_test_play;
    R0=sound_test_starts.;
    R0=R0+R3;
    I4=R0;
    R0=DM(I4,M5);                   // the first note's state
    COMP(R0,R11);
    IF NE JUMP sound_test_play;
    R0=DM(sound_test_overlap.);
    R0=R0+1;
    DM(sound_test_overlap.)=R0;
sound_test_play:
    JUMP sine_render.;
"""

# SINE plays a sine at the note, FINE cents (-100..100, from 0) away. sound_test_pitch is the last
# pitch it was given, so a test can say which note the key played. Writable globals here
# are observation probes for the emulator, not an instrument state pattern.
SINE = sdk_machine("Sine", """
    #include "elekloader/sdk_alpha_machine.h"

    enum { FINE };
    static const struct elek_knob knobs[] = { [FINE] = { "FINE", .min = -100, .max = 100 } };
    #ifdef ELEK_TARGET_SHARC
    void sound_test_render(const struct elek_voice *v, elek_sample *out);
    #define RENDER sound_test_render
    #else
    struct state { uint32_t phase; };
    volatile int32_t sound_test_pitch;
    volatile int32_t sound_test_control;
    volatile uint32_t sound_test_start_count, sound_test_overlap;
    volatile uint32_t sound_test_starts[2][3];

    /* sin(2 pi ph / 2^32), Q15: a parabola, then its square nudged in. */
    static int32_t sine(uint32_t ph)
    {
        int32_t x = (int32_t)ph >> 16, a = x < 0 ? -x : x, y, z;
        y = (x * (32768 - a)) >> 13;
        z = y < 0 ? -y : y;
        y += (7373 * (((y * z) >> 15) - y)) >> 15;
        return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
    }

    static void render(const struct elek_voice *v, elek_sample *out)
    {
        struct state *state = ELEK_STATE(struct state, v);
        uint32_t inc = elek_phase_inc(v->pitch + v->knob[FINE] * 65536 / 100);
        sound_test_pitch = v->pitch;
        sound_test_control = v->knob[FINE];
        if (v->start) {
            uint32_t n = sound_test_start_count;
            if (n < 2) {
                sound_test_starts[n][0] = v->track;
                sound_test_starts[n][1] = (uint32_t)(uintptr_t)v->state;
                sound_test_starts[n][2] = v->pitch;
            }
            sound_test_start_count = n + 1;
        }
        /* each voice has its own state: the first note's still sounds */
        if (sound_test_start_count >= 2 && (uint32_t)(uintptr_t)v->state == sound_test_starts[0][1])
            ++sound_test_overlap;
        for (int i = 0; i < ELEK_VOICE_FRAMES; ++i) {
            int32_t y = sine(state->phase);
            elek_sample sample = elek_level(y < 0 ? -y : y, 32767);
            out[i] = y < 0 ? -sample : sample;
            state->phase += inc;
        }
    }
    #define RENDER render
    #endif

    static const struct elek_machine sine_machine = {
        .id = @ID@, .name = "Sine", .short_name = "SINE", ELEK_KNOBS(knobs),
        .pages = { { "SINE", { [ELEK_A] = &knobs[FINE] } } }, .render = RENDER,
    };
    ELEK_REGISTER_MACHINE(sine_machine);
""", sharc={'probes-dn2.s': SINE_PROBES_DN2,
            'sine-dn2.s': (ROOT / 'examples/sdk-sine/sine-dn2.s').read_text()})

# The same page, and no .render.
SILENT = sdk_machine("Silent", """
    #include "elekloader/sdk_alpha_machine.h"

    static const struct elek_knob knobs[] = { { "FINE", .max = 127 } };
    static const struct elek_machine silent = {
        .id = @ID@, .name = "Silent", .short_name = "SLNT", ELEK_KNOBS(knobs),
        .pages = { { "SINE", { &knobs[0] } } },
    };
    ELEK_REGISTER_MACHINE(silent);
""")


def hz(pitch):
    """The frequency of a 16.16 pitch: 60 << 16 is middle C."""
    return 261.6256 * 2 ** ((pitch / 65536 - 60) / 12)


@sdk_tests
class Sound:
    audio = True
    requires = ('render',)

    def assertClose(self, value, expected, tolerance, what):
        self.assertIsNotNone(value, '%s: no tone' % what)
        self.assertLess(abs(value / expected - 1), tolerance,
                        '%s: %.2f Hz, expected %.2f Hz' % (what, value, expected))

    def test_the_machine_plays_a_sine_at_the_note(self):
        self.track.use(SINE)
        sound = self.track.play(0)
        self.assertGreater(sound.peak, 1000, 'the key played nothing')
        self.assertClose(sound.frequency, hz(self.read('sound_test_pitch')), 0.01, 'key 0')

    def test_the_pitch_follows_the_key(self):
        self.track.use(SINE)
        low = self.track.play(0)
        low_pitch = self.read('sound_test_pitch')
        high = self.track.play(12)
        self.assertEqual(self.read('sound_test_pitch') - low_pitch, 12 << 16, 'the key an octave up')
        self.assertClose(high.frequency, 2 * low.frequency, 0.005, 'an octave up')

    def test_fine_starts_at_its_default(self):
        fine, = self.track.use(SINE).knobs("FINE")
        self.assertEqual(fine.value, 0)

    def test_fine_tunes_up_to_a_semitone_either_way(self):
        fine, = self.track.use(SINE).knobs("FINE")
        centre = self.track.play(0).frequency
        fine.set(100)
        self.assertClose(self.track.play(0).frequency, centre * 2 ** (1 / 12), 0.005, 'FINE 100')
        fine.set(-100)
        self.assertClose(self.track.play(0).frequency, centre / 2 ** (1 / 12), 0.005, 'FINE -100')

    def test_a_machine_without_render_plays_what_it_did(self):
        self.track.use(SILENT)
        played = self.track.play(0).peak > 1000
        self.assertEqual(played, self.panel.sound_without_render)


@sdk_tests
class Polyphony:
    audio = True
    requires = ('render', 'polyphony')

    def test_overlapping_notes_have_separate_voice_state(self):
        self.track.use(SINE)
        self.panel.keyboard()
        self.panel.press_key(0)
        self.wait(100)
        self.panel.press_key(7)
        self.wait(100)
        self.panel.release_key(7)
        self.panel.release_key(0)
        self.wait(100)
        self.assertGreaterEqual(self.read('sound_test_start_count'), 2)
        first = [self.read('sound_test_starts', i) for i in range(3)]
        second = [self.read('sound_test_starts', 3 + i) for i in range(3)]
        self.assertEqual((first[0], second[0]), (self.track.index, self.track.index))
        self.assertNotEqual(first[1], second[1], 'both notes shared voice state')
        self.assertEqual(second[2] - first[2], 7 << 16)
        self.assertGreater(self.read('sound_test_overlap'), 0, 'the first voice stopped at the second note')


@sdk_tests
class Modulation:
    audio = True
    requires = ('render', 'audio_modulation')

    def test_lfo_changes_the_voices_controls_without_editing_the_knob(self):
        fine = self.track.use(SINE).knob('FINE')
        original = fine.raw
        lfo = self.track.lfo(1)
        lfo.choose(fine)
        lfo.confirm()
        self.panel.set_lfo_depth()
        self.panel.keyboard()
        self.panel.press_key(0)
        self.wait(100)
        values = []
        for _ in range(12):
            values.append(self.read('sound_test_control'))
            self.wait(50)
        self.panel.release_key(0)
        self.wait(100)
        self.assertGreater(len(set(values)), 1, 'the render received an unmodulated control')
        self.assertEqual(fine.raw, original)


if __name__ == '__main__':
    unittest.main()
