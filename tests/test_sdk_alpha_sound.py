# SPDX-License-Identifier: GPL-2.0-or-later
"""A machine's .render makes its sound: a sine at the note played, tuned by a FINE knob, measured in
what the device outputs. Without .render a machine plays what it did before (nothing on the
Digitakt, FM on the Digitone).

Run with digiemu's Python:

  ELEKLOADER_DIGIEMU=<digiemu> ELEKLOADER_STOCK_154=<Digitakt_OS1.54.syx> \\
  ELEKLOADER_STOCK_DN1_144=<Digitone_and_Digitone_Keys_OS1.44.syx> ELEKLOADER_CROSS=m68k-elf- \\
  uv run --project "$ELEKLOADER_DIGIEMU" python tests/test_sdk_alpha_sound.py
"""
import unittest

from sdk_alpha_harness import sdk_expected_failure, sdk_machine, sdk_tests

# SINE plays a sine at the note, FINE cents (-100..100, from 0) away. sound_test_pitch is the last
# pitch it was given, so a test can say which note the key played.
SINE = sdk_machine("Sine", """
    #include "elekloader/sdk_alpha_machine.h"

    enum { FINE };
    static const struct elek_ui_knob fine = {
        .id = FINE, .label = "FINE",
        .min = ELEK_UI_BOUND(-100), .max = ELEK_UI_BOUND(100), .def = ELEK_UI_BOUND(0),
    };
    static const struct elek_ui_page pages[] = {
        { .label = "SINE", .knob = { [ELEK_ENCODER_A] = &fine } },
    };
    static uint32_t phase[ELEK_VOICES_MAX];
    volatile int32_t sound_test_pitch;

    /* sin(2 pi ph / 2^32), Q15: a parabola, then its square nudged in. */
    static int32_t sine(uint32_t ph)
    {
        int32_t x = (int32_t)ph >> 16, a = x < 0 ? -x : x, y, z;
        y = (x * (32768 - a)) >> 13;
        z = y < 0 ? -y : y;
        y += (7373 * (((y * z) >> 15) - y)) >> 15;
        return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
    }

    static void render(const struct elek_ui_context *ctx, const struct elek_voice *v, int32_t *out)
    {
        uint32_t inc = elek_phase_inc(v->pitch + ctx->knob[FINE]->value * 65536 / 100);
        if (v->fresh)
            phase[v->voice] = 0;
        sound_test_pitch = v->pitch;
        for (int i = 0; i < ELEK_VOICE_FRAMES; ++i) {
            out[i] = sine(phase[v->voice]) << 16;
            phase[v->voice] += inc;
        }
    }

    static const struct elek_machine sine_machine = {
        .id = @ID@, .name = "Sine", .short_name = "SINE",
        .pages = pages, .page_count = 1, .render = render,
    };
    ELEK_REGISTER_MACHINE(sine_machine);
""")

# The same page, and no .render.
SILENT = sdk_machine("Silent", """
    #include "elekloader/sdk_alpha_machine.h"

    static const struct elek_ui_knob fine = { .id = 0, .label = "FINE" };
    static const struct elek_ui_page pages[] = { { .label = "SINE", .knob = { &fine } } };
    static const struct elek_machine silent = {
        .id = @ID@, .name = "Silent", .short_name = "SLNT", .pages = pages, .page_count = 1,
    };
    ELEK_REGISTER_MACHINE(silent);
""")


def hz(pitch):
    """The frequency of a 16.16 pitch: 60 << 16 is middle C."""
    return 261.6256 * 2 ** ((pitch / 65536 - 60) / 12)


NO_AUDIO = sdk_expected_failure('digitone-mk2')    # DigiKit records no audio, and .render is not called


@sdk_tests
class Sound:
    audio = True

    def assertClose(self, value, expected, tolerance, what):
        self.assertIsNotNone(value, '%s: no tone' % what)
        self.assertLess(abs(value / expected - 1), tolerance,
                        '%s: %.2f Hz, expected %.2f Hz' % (what, value, expected))

    @NO_AUDIO
    def test_the_machine_plays_a_sine_at_the_note(self):
        self.track.use(SINE)
        sound = self.track.play(0)
        self.assertGreater(sound.peak, 1000, 'the key played nothing')
        self.assertClose(sound.frequency, hz(self.read('sound_test_pitch')), 0.01, 'key 0')

    @NO_AUDIO
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

    @NO_AUDIO
    def test_fine_tunes_up_to_a_semitone_either_way(self):
        fine, = self.track.use(SINE).knobs("FINE")
        centre = self.track.play(0).frequency
        fine.set(100)
        self.assertClose(self.track.play(0).frequency, centre * 2 ** (1 / 12), 0.005, 'FINE 100')
        fine.set(-100)
        self.assertClose(self.track.play(0).frequency, centre / 2 ** (1 / 12), 0.005, 'FINE -100')

    @NO_AUDIO
    def test_a_machine_without_render_plays_what_it_did(self):
        self.track.use(SILENT)
        played = self.track.play(0).peak > 1000
        self.assertEqual(played, self.panel.sound_without_render)


if __name__ == '__main__':
    unittest.main()
