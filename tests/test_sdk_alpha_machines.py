# SPDX-License-Identifier: GPL-2.0-or-later
"""An SDK machine can be picked for a track, and its .present callback is called for its track only.

Run as tests/sdk_alpha_harness.py says.
"""
import unittest

from sdk_alpha_devices import Digitone1
from sdk_alpha_harness import SdkTestCase, sdk_example, sdk_machine, sdk_tests

# The smallest machine: a name, and nothing else.
EMPTY = sdk_machine("Empty", """
    #include "elekloader/sdk_alpha_machine.h"

    static const struct elek_machine empty = {
        .id = @ID@, .name = "Empty", .short_name = "EMPT",
    };
    ELEK_REGISTER_MACHINE(empty);
""")

# A machine with only a callback. It counts its calls, the context it was given that was not as
# expected (it has no pages and no knobs), and the track of the last call.
CALLBACK = sdk_machine("Callback", """
    #include "elekloader/sdk_alpha_machine.h"

    volatile uint32_t callback_calls, callback_bad_context, callback_track;

    static void present(const struct elek_ui_context *ctx, struct elek_ui_presentation *out)
    {
        ++callback_calls;
        callback_track = ctx->track;
        if (ctx->page != ELEK_UI_NO_PAGE || ctx->page_count || ctx->knob_count || ctx->knob ||
            out->knob_count || out->knob)
            ++callback_bad_context;
    }
    static const struct elek_machine callback = {
        .id = @ID@, .name = "Callback", .short_name = "CALL", .present = present,
    };
    ELEK_REGISTER_MACHINE(callback);
""")

# The example an author starts from: two pages, with PITCH on both.
HELLO = sdk_example("SDK Hello", "examples/sdk-hello-world")

ONE_PAGE = sdk_machine("One page", """
    #include "elekloader/sdk_alpha_machine.h"

    static const struct elek_ui_page page = { .label = "ONE" };
    static const struct elek_machine one_page = {
        .id = @ID@, .name = "One page", .short_name = "ONE", .pages = &page, .page_count = 1,
    };
    ELEK_REGISTER_MACHINE(one_page);
""")


@sdk_tests
class Machines:
    def test_an_empty_machine_can_be_picked(self):
        self.track.use(EMPTY)
        self.assertEqual(self.track.machine_id, EMPTY.id)

    def test_an_empty_machine_leaves_every_position_of_its_page_empty(self):
        self.track.use(EMPTY)
        self.assertEqual(self.track.empty_positions(), (False,) * 8)

    def test_a_machine_with_only_a_callback_leaves_every_position_empty(self):
        self.track.use(CALLBACK)
        self.assertEqual(self.track.empty_positions(), (False,) * 8)

    def test_the_stock_machine_comes_back(self):
        self.track.use(EMPTY)
        self.track.use_stock()
        self.assertEqual(self.track.machine_id, 0)

    def test_a_callback_is_called_for_the_track_that_uses_it(self):
        self.track.use(CALLBACK)
        self.track.show()
        self.assertGreater(self.read('callback_calls'), 0)
        self.assertEqual(self.read('callback_track'), 0)

    def test_a_callback_is_called_for_the_second_track_too(self):
        self.tracks[1].use(CALLBACK)
        self.tracks[1].show()
        self.assertEqual(self.read('callback_track'), 1)

    def test_a_callback_sees_the_context_of_a_machine_with_no_pages(self):
        self.track.use(CALLBACK)
        self.track.show()
        self.assertGreater(self.read('callback_calls'), 0)
        self.assertEqual(self.read('callback_bad_context'), 0)

    def test_a_callback_stops_when_its_machine_leaves_the_track(self):
        self.track.use(CALLBACK)
        self.track.show()
        self.assertGreater(self.read('callback_calls'), 0)
        self.track.use_stock()
        self.track.show()
        calls = self.read('callback_calls')
        self.wait(1000)
        self.assertEqual(self.read('callback_calls'), calls)

    def test_each_track_keeps_its_own_machine(self):
        self.tracks[0].use(EMPTY)
        self.assertEqual(self.tracks[1].machine_id, 0)
        self.tracks[1].use(CALLBACK)
        self.assertEqual(self.tracks[0].machine_id, EMPTY.id)
        self.assertEqual(self.tracks[1].machine_id, CALLBACK.id)

    def test_the_last_track_gets_its_own_callback_context(self):
        self.tracks[-1].use(CALLBACK)
        self.tracks[-1].show()
        self.assertEqual(self.read('callback_track'), len(self.tracks) - 1)
        self.assertEqual(self.tracks[0].machine_id, 0)

    def test_a_callback_runs_again_after_returning_from_stock(self):
        self.track.use(CALLBACK)
        self.track.show()
        calls = self.read('callback_calls')
        self.track.use_stock()
        self.track.use(CALLBACK)
        self.track.show()
        self.assertGreater(self.read('callback_calls'), calls)

    def test_the_hello_world_example_edits_pitch_on_both_its_pages(self):
        pitch, = self.track.use(HELLO).knobs("PITCH")
        pitch.turn(+16)
        first = pitch.raw
        self.assertGreater(first, 0)
        pitch.turn(+16, page=1)
        self.assertGreater(pitch.raw, first)


class DigitonePages(SdkTestCase):
    device = Digitone1

    def test_an_empty_callback_runs_again_after_returning_from_fm(self):
        self.track.use(CALLBACK)
        self.track.show()
        calls = self.read('callback_calls')
        self.track.use_stock()
        self.track.use(CALLBACK)
        self.track.show()
        self.assertGreater(self.read('callback_calls'), calls)

    def test_settings_cover_the_sdk_page_as_they_cover_fm(self):
        self.track.use(ONE_PAGE)
        self.track.show()
        self.panel.tap('GLOBAL')
        settings = self.panel.screen()
        self.panel.tap('NO')
        self.track.use_stock()
        self.track.show()
        self.panel.tap('GLOBAL')
        self.assertEqual(self.panel.screen(), settings)

    def test_syn2_gets_its_stock_pages_back(self):
        self.track.use_stock()
        self.panel.open_page()
        self.assertTrue(self.panel.wait_until(lambda: any(self.panel.crop(18, 13, 6, 16))),
                        'FM\'s algorithm diagram did not appear')
        stock = self.panel.syn2_pages()
        algorithm = self.panel.crop(18, 13, 6, 16)
        self.assertTrue(any(algorithm))
        self.assertIsNotNone(stock)
        self.assertGreater(len(stock), 1)
        self.assertEqual(stock[0], self.panel.SYN2_FIRST)
        self.track.use(ONE_PAGE)
        self.track.show()
        self.assertEqual(self.panel.syn2_pages(), [self.panel.SDK_PAGE])
        self.track.use_stock()
        self.track.show()
        self.assertTrue(self.panel.wait_until(lambda: any(self.panel.crop(18, 13, 6, 16))),
                        'FM\'s algorithm diagram did not return')
        self.assertEqual(self.track.machine_id, 0)
        self.assertEqual(self.panel.syn2_pages(), stock)
        self.assertEqual(self.panel.crop(18, 13, 6, 16), algorithm)


if __name__ == '__main__':
    unittest.main()
