# SPDX-License-Identifier: GPL-2.0-or-later
"""A .present callback may hide a knob. A hidden knob keeps its value and its encoder does nothing,
and copying, clearing and pasting the page take the hidden knob along.

Run as tests/sdk_alpha_harness.py says.
"""
import unittest

from sdk_alpha_harness import sdk_machine, sdk_tests

# HIDE and TRGT are two knobs on one page. The machine's rule: HIDE at 127 hides TRGT.
HIDING = sdk_machine("Hiding", """
    #include "elekloader/sdk_alpha_machine.h"

    enum { HIDE, TRGT };
    static const struct elek_knob knobs[] = { [HIDE] = { "HIDE", .max = 127 }, [TRGT] = { "TRGT", .max = 127 } };
    static void present(const struct elek_ui_context *ctx, struct elek_ui_presentation *out)
    {
        if (ctx->knob[HIDE]->value == 127)
            out->knob[TRGT]->visible = 0;
    }
    static const struct elek_machine hiding = {
        .id = @ID@, .name = "Hiding", .short_name = "HIDE", ELEK_KNOBS(knobs),
        .pages = { { "MAIN", { [ELEK_A] = &knobs[HIDE], [ELEK_B] = &knobs[TRGT] } } }, .present = present,
    };
    ELEK_REGISTER_MACHINE(hiding);
""")


@sdk_tests
class Hiding:
    def test_a_knob_is_shown_until_it_is_hidden(self):
        hide, trgt = self.track.use(HIDING).knobs("HIDE", "TRGT")
        self.assertTrue(trgt.visible)

    def test_the_machine_hides_a_knob_at_127(self):
        hide, trgt = self.track.use(HIDING).knobs("HIDE", "TRGT")
        hide.set(127)
        self.assertFalse(trgt.visible)

    def test_a_hidden_knob_looks_like_an_empty_position(self):
        hide, trgt = self.track.use(HIDING).knobs("HIDE", "TRGT")
        hide.set(127)
        self.assertEqual(trgt.pixels, self.panel.control_pixels(2))
        hide.set(0)
        self.assertNotEqual(trgt.pixels, self.panel.control_pixels(2))

    def test_a_hidden_knob_ignores_its_encoder(self):
        hide, trgt = self.track.use(HIDING).knobs("HIDE", "TRGT")
        hide.set(127)
        before = trgt.value
        trgt.turn(+32)
        self.assertEqual(trgt.value, before)

    def test_a_hidden_knob_keeps_its_value(self):
        hide, trgt = self.track.use(HIDING).knobs("HIDE", "TRGT")
        trgt.turn(+32)
        value = trgt.value
        self.assertGreater(value, 0)
        hide.set(127)
        self.assertEqual(trgt.value, value)

    def test_a_knob_edits_again_when_it_is_shown(self):
        hide, trgt = self.track.use(HIDING).knobs("HIDE", "TRGT")
        hide.set(127)
        hide.set(0)
        self.assertTrue(trgt.visible)
        before = trgt.value
        trgt.turn(+16)
        self.assertGreater(trgt.value, before)

    def test_each_track_hides_on_its_own(self):
        first, second = self.tracks[0], self.tracks[1]
        hide, trgt = first.use(HIDING).knobs("HIDE", "TRGT")
        other_hide, other_trgt = second.use(HIDING).knobs("HIDE", "TRGT")
        hide.set(127)
        self.assertFalse(trgt.visible)
        self.assertTrue(other_trgt.visible)


@sdk_tests
class PageActions:
    def hidden_target(self):
        """HIDE at 127, TRGT hidden with a value of its own."""
        hide, trgt = self.track.use(HIDING).knobs("HIDE", "TRGT")
        trgt.turn(+20)
        self.assertGreater(trgt.raw, 0)
        hide.set(127)
        self.assertFalse(trgt.visible)
        return hide, trgt

    def test_clear_takes_a_hidden_knob_too(self):
        hide, trgt = self.hidden_target()
        self.track.page_action('clear')
        self.assertEqual(trgt.raw, 0)
        self.assertEqual(hide.raw, 0)

    def test_clear_can_be_undone_with_a_hidden_knob(self):
        hide, trgt = self.hidden_target()
        saved = trgt.raw
        self.track.page_action('clear')
        self.track.page_action('clear')
        self.assertEqual(trgt.raw, saved)
        self.assertEqual(hide.value, 127)
        self.assertFalse(trgt.visible)

    def test_paste_gives_a_hidden_knob_its_value_back(self):
        hide, trgt = self.hidden_target()
        saved = trgt.raw
        self.track.page_action('copy')
        self.track.page_action('clear')
        self.track.page_action('paste')
        self.assertEqual(trgt.raw, saved)
        self.assertEqual(hide.value, 127)
        self.assertFalse(trgt.visible)

    def test_a_page_action_does_not_make_a_hidden_encoder_edit(self):
        hide, trgt = self.hidden_target()
        self.track.page_action('copy')
        self.track.page_action('clear')
        self.track.page_action('paste')
        before = trgt.raw
        trgt.turn(+16)
        self.assertEqual(trgt.raw, before)


if __name__ == '__main__':
    unittest.main()
