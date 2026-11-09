# SPDX-License-Identifier: GPL-2.0-or-later
"""A knob's encoder clicks: up to 128 values share the encoder's 128 positions evenly, and a knob that sets
.clicks moves that many clicks a value, stopping at its ends. A MIDI CC still spreads over all the values.

Run as tests/sdk_alpha_harness.py says.
"""
import unittest

from sdk_alpha_harness import sdk_machine, sdk_tests
from test_sdk_alpha_midi import CCS_DN2

# CLK1: 4 values, a click each (knob 0, which takes a CC on every device); STEP: 4 values, evenly (32 clicks
# a value); CLK10: 4 values, ten clicks each. Page 2 holds a plain 0..127 knob on the same encoders (REFA-REFC),
# which moves a value a stock step: a click, as the tests count them. The emulators' encoders do not make a
# step of every detent (a gesture's first detents are swallowed), so a test turns the reference and the knob
# alike and compares them.
CLICKS = sdk_machine("Clicks", """
    #include "elekloader/sdk_alpha_machine.h"

    enum { CLK1, STEP, CLK10, REFA, REFB, REFC };
    static const struct elek_knob knobs[] = {
        [CLK1] = { "CLK1", .max = 3, .clicks = 1 }, [STEP] = { "STEP", .max = 3 },
        [CLK10] = { "CLK10", .max = 3, .clicks = 10 },
        [REFA] = { "REFA", .max = 127 }, [REFB] = { "REFB", .max = 127 }, [REFC] = { "REFC", .max = 127 },
    };
    static const struct elek_machine clicks = {
        .id = @ID@, .name = "Clicks", .short_name = "CLKS", ELEK_KNOBS(knobs),
        .pages = { { "ONE", { &knobs[CLK1], &knobs[STEP], &knobs[CLK10] } },
                   { "TWO", { &knobs[REFA], &knobs[REFB], &knobs[REFC] } } },
    };
    ELEK_REGISTER_MACHINE(clicks);
""")


# LOCKED: CLK1 alone, with a render that keeps the value it plays by track (lk_test_level), so a p-lock is
# heard where it is played. On the Digitone II the render is SHARC: test_sdk_alpha_midi's, renamed.
LOCKED_DN2 = CCS_DN2.replace('cc_test_', 'lk_test_')
LOCKED = sdk_machine("Locked", """
    #include "elekloader/sdk_alpha_machine.h"

    static const struct elek_knob knobs[] = { { "CLK1", .max = 3, .clicks = 1 } };

    #ifdef ELEK_TARGET_SHARC
    void lk_test_render(const struct elek_voice *v, elek_sample *out);
    #define RENDER lk_test_render
    #else
    volatile int32_t lk_test_renders[8], lk_test_level[8];

    static void render(const struct elek_voice *v, elek_sample *out)
    {
        ++lk_test_renders[v->track & 7];
        lk_test_level[v->track & 7] = v->knob[0];
        for (int i = 0; i < ELEK_VOICE_FRAMES; ++i)
            out[i] = 0;
    }
    #define RENDER render
    #endif

    static const struct elek_machine locked = {
        .id = @ID@, .name = "Locked", .short_name = "LOCK", ELEK_KNOBS(knobs),
        .pages = { { "LOCK", { &knobs[0] } } }, .render = RENDER,
    };
    ELEK_REGISTER_MACHINE(locked);
""", sharc={'locked-dn2.s': LOCKED_DN2})


@sdk_tests
class Clicks:
    requires = ('clicks',)

    def detents(self, knob, n):
        """Turn one detent at a time, the screen settled between them, so the encoder does not accelerate."""
        for _ in range(abs(n)):
            knob.turn(1 if n > 0 else -1)

    def steps(self, ref, n):
        """The stock steps that n detents make on ref's encoder, from where ref is."""
        before = ref.value
        self.detents(ref, n)
        return ref.value - before

    def test_four_values_take_32_clicks_each(self):
        step, ref = self.track.use(CLICKS).knobs("STEP", "REFB")
        s = self.steps(ref, 54)
        self.assertGreaterEqual(s, 32)
        self.detents(step, 54)
        self.assertEqual(step.value, s // 32)

    def test_one_click_a_value_stopping_at_the_top(self):
        clk1, ref = self.track.use(CLICKS).knobs("CLK1", "REFA")
        self.assertGreaterEqual(self.steps(ref, 8), 3)
        self.detents(clk1, 8)
        self.assertEqual(clk1.value, 3)
        self.detents(clk1, 12)                      # more steps: it stops at 3
        self.assertEqual(clk1.value, 3)
        self.assertEqual(self.first_change(clk1, -1), 2, 'a click back from the top')

    def first_change(self, knob, direction):
        """Turn until the knob's value changes: the value the first step leaves it at."""
        before = knob.value
        for _ in range(20):
            self.detents(knob, direction)
            if knob.value != before:
                return knob.value
        self.fail('%r did not move' % knob)

    def test_ten_clicks_a_value(self):
        clk10, ref = self.track.use(CLICKS).knobs("CLK10", "REFC")
        s = self.steps(ref, 34)
        self.assertGreaterEqual(s, 20)
        self.detents(clk10, 34)
        self.assertEqual(clk10.value, min(s // 10, 3))

    def test_a_cc_spreads_over_the_values(self):
        clk1, = self.track.use(CLICKS).knobs("CLK1")
        cc = self.panel.knob_cc(0)
        for value, want in ((31, 0), (32, 1), (95, 2), (96, 3), (127, 3)):
            self.panel.cc(1, cc, value, after_ms=200)
            self.assertEqual(clk1.value, want, 'CC %d' % value)
        self.panel.cc(1, cc, 70, after_ms=200)       # value 2, between its points
        self.assertEqual(self.first_change(clk1, -1), 1, 'a click back from a CC value')


@sdk_tests
class LockClicks:
    """A GRID REC p-lock steps as the knob does: the lock played holds a whole value."""
    audio = True
    requires = ('clicks', 'render')

    def test_a_lock_steps_by_clicks(self):
        clk1, = self.track.use(LOCKED).knobs("CLK1")
        self.track.show()
        panel = self.panel
        panel.tap('RECORD')                         # GRID REC
        panel.s.press(panel.s.code('1'))            # trig 1 held: the turn makes its p-lock
        panel.wait(300)
        for _ in range(8):
            panel.turn_now('A', 1, after_ms=250)
        panel.s.release(panel.s.code('1'))
        panel.wait(300)
        panel.tap('RECORD')
        self.assertEqual(clk1.value, 0, 'the turn went into the sound, not the lock')
        panel.tap('PLAY')
        self.addCleanup(panel.tap, 'STOP')
        panel.wait(400)
        self.assertGreater(self.read('lk_test_renders'), 0, 'trig 1 did not play')
        # Unstepped, eight detents leave the lock below a whole value of four (32 positions each): 0.
        self.assertIn(self.read('lk_test_level'), (1, 2, 3))


if __name__ == '__main__':
    unittest.main()
