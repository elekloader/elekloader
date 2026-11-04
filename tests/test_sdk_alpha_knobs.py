# SPDX-License-Identifier: GPL-2.0-or-later
"""A machine's knobs: each encoder edits its own knob, a knob maps its range onto its bounds, a knob
can sit on two pages, and every track keeps its own values and page.

Run with digiemu's Python:

  ELEKLOADER_DIGIEMU=<digiemu> ELEKLOADER_STOCK_154=<Digitakt_OS1.54.syx> \
  ELEKLOADER_STOCK_DN1_144=<Digitone_and_Digitone_Keys_OS1.44.syx> ELEKLOADER_CROSS=m68k-elf- \
  uv run --project "$ELEKLOADER_DIGIEMU" python tests/test_sdk_alpha_knobs.py
"""
import unittest

from sdk_alpha_harness import sdk_machine, sdk_tests

# One page of eight knobs, KNB A to KNB H, on encoders A to H.
EIGHT = sdk_machine("Eight", """
    #include "elekloader/sdk_alpha_machine.h"

    static const struct elek_ui_knob knobs[] = {
        { .id = 0, .label = "KNB A" }, { .id = 1, .label = "KNB B" },
        { .id = 2, .label = "KNB C" }, { .id = 3, .label = "KNB D" },
        { .id = 4, .label = "KNB E" }, { .id = 5, .label = "KNB F" },
        { .id = 6, .label = "KNB G" }, { .id = 7, .label = "KNB H" },
    };
    static const struct elek_ui_page pages[] = {
        { .label = "EIGHT", .knob = { &knobs[0], &knobs[1], &knobs[2], &knobs[3],
                                      &knobs[4], &knobs[5], &knobs[6], &knobs[7] } },
    };
    static const struct elek_machine eight = {
        .id = @ID@, .name = "Eight", .short_name = "EGHT", .pages = pages, .page_count = 1,
    };
    ELEK_REGISTER_MACHINE(eight);
""")

# Knobs with different bounds, on encoders A to F; G and H hold none. The callback keeps the raw
# value (native, 0..32512) and the mapped value it was given for each knob.
BOUNDS = sdk_machine("Bounds", """
    #include "elekloader/sdk_alpha_machine.h"

    volatile int32_t bounds_raw[6], bounds_value[6];

    static const struct elek_ui_knob knobs[] = {
        { .id = 0, .label = "DFLT" },                                   /* 0..127 */
        { .id = 1, .label = "PITCH", .min = ELEK_UI_BOUND(10), .max = ELEK_UI_BOUND(20) },
        { .id = 2, .label = "ZERO", .max = ELEK_UI_BOUND(0) },          /* 0..0 */
        { .id = 3, .label = "FIXED", .min = ELEK_UI_BOUND(7), .max = ELEK_UI_BOUND(7) },
        { .id = 4, .label = "SIGN", .min = ELEK_UI_BOUND(-10), .max = ELEK_UI_BOUND(10) },
        { .id = 5, .label = "FULL", .min = ELEK_UI_BOUND(-2147483647 - 1), .max = ELEK_UI_BOUND(2147483647) },
    };
    static const struct elek_ui_page pages[] = {
        { .label = "BOUNDS", .knob = { &knobs[0], &knobs[1], &knobs[2], &knobs[3], &knobs[4], &knobs[5] } },
    };
    static void present(const struct elek_ui_context *ctx, struct elek_ui_presentation *out)
    {
        if (!ctx->knob)
            return;
        for (int k = 0; k < 6; ++k) {
            bounds_raw[k] = ctx->knob[k]->raw_value;
            bounds_value[k] = ctx->knob[k]->value;
        }
    }
    static const struct elek_machine bounds = {
        .id = @ID@, .name = "Bounds", .short_name = "BNDS",
        .pages = pages, .page_count = 1, .present = present,
    };
    ELEK_REGISTER_MACHINE(bounds);
""")

# Two pages. PITCH sits on both: encoder D of the first, B of the second. The callback keeps the page
# it was called for and every knob's raw value, whichever page the knob is on.
PAGES = sdk_machine("Pages", """
    #include "elekloader/sdk_alpha_machine.h"

    volatile int32_t pages_page, pages_raw[3];

    static const struct elek_ui_knob knobs[] = {
        { .id = 0, .label = "AMNT" }, { .id = 1, .label = "PITCH" }, { .id = 2, .label = "OTHER" },
    };
    static const struct elek_ui_page pages[] = {
        { .label = "FIRST", .knob = { [ELEK_ENCODER_A] = &knobs[0], [ELEK_ENCODER_D] = &knobs[1] } },
        { .label = "SECOND", .knob = { [ELEK_ENCODER_A] = &knobs[2], [ELEK_ENCODER_B] = &knobs[1] } },
    };
    static void present(const struct elek_ui_context *ctx, struct elek_ui_presentation *out)
    {
        if (!ctx->knob)
            return;
        if (ctx->page >= 0)
            pages_page = ctx->page;
        for (int k = 0; k < 3; ++k)
            pages_raw[k] = ctx->knob[k]->raw_value;
    }
    static const struct elek_machine pages_machine = {
        .id = @ID@, .name = "Pages", .short_name = "PAGE",
        .pages = pages, .page_count = 2, .present = present,
    };
    ELEK_REGISTER_MACHINE(pages_machine);
""")


@sdk_tests
class Knobs:
    def seen(self, name, knob):
        """What the callback was given for the knob (a signed word)."""
        word = self.read(name, knob.spec['id'])
        return word - (1 << 32) if word >= 1 << 31 else word

    def test_every_knob_starts_at_zero(self):
        knobs = self.track.use(EIGHT).all_knobs()
        self.assertEqual([knob.raw for knob in knobs], [0] * 8)

    def test_the_page_uses_four_evenly_spaced_columns_without_an_algorithm_diagram(self):
        self.track.use(EIGHT)
        self.track.show()
        self.assertTrue(self.panel.wait_until(
            lambda: any(self.panel.glyph_pixels())
            and not any(self.panel.algorithm_pixels())), 'the SDK knob grid did not appear')
        first = self.panel.glyph_pixels()
        self.assertTrue(any(first), 'encoder A has no knob graphic')
        for encoder in range(8):
            with self.subTest(encoder='ABCDEFGH'[encoder]):
                self.assertEqual(self.panel.glyph_pixels(encoder), first)
        self.assertFalse(any(self.panel.algorithm_pixels()), 'FM diagram remains beside the grid')

    def test_each_encoder_edits_its_own_knob(self):
        knobs = self.track.use(EIGHT).all_knobs()
        for edited in knobs:
            with self.subTest(knob=edited.spec['label']):
                before = [knob.raw for knob in knobs]
                edited.set(127)
                self.assertEqual([knob.raw for knob in knobs],
                                 [127 << 8 if knob is edited else raw for knob, raw in zip(knobs, before)])
                edited.set(0)
                self.assertEqual([knob.raw for knob in knobs], before)

    def test_an_encoder_with_no_knob_changes_nothing(self):
        used = self.track.use(BOUNDS)
        knobs = used.all_knobs()
        for knob in knobs:
            knob.turn(+10)
        before = [knob.raw for knob in knobs]
        self.track.turn_encoder('G', +10)
        self.assertEqual([knob.raw for knob in knobs], before)

    def test_a_knob_with_no_bounds_runs_from_0_to_127(self):
        dflt, = self.track.use(BOUNDS).knobs("DFLT")
        self.assertEqual(dflt.value, 0)
        dflt.set(127)
        self.assertEqual(self.seen('bounds_value', dflt), 127)

    def test_a_knob_maps_its_range_onto_its_bounds(self):
        pitch, = self.track.use(BOUNDS).knobs("PITCH")
        self.assertEqual(self.seen('bounds_value', pitch), 10)
        pitch.set(20)
        self.assertEqual(self.seen('bounds_value', pitch), 20)
        pitch.set(10)
        self.assertEqual(self.seen('bounds_value', pitch), 10)

    def test_the_callback_is_given_the_value_between_the_bounds_too(self):
        pitch, sign = self.track.use(BOUNDS).knobs("PITCH", "SIGN")
        for knob in (pitch, sign):
            knob.turn(+37)
            self.assertEqual(self.seen('bounds_raw', knob), knob.raw)
            self.assertEqual(self.seen('bounds_value', knob), knob.value)

    def test_a_signed_range_reaches_both_ends(self):
        sign, = self.track.use(BOUNDS).knobs("SIGN")
        sign.set(10)
        self.assertEqual(self.seen('bounds_value', sign), 10)
        sign.set(-10)
        self.assertEqual(self.seen('bounds_value', sign), -10)

    def test_a_knob_can_span_the_whole_32_bit_range(self):
        full, = self.track.use(BOUNDS).knobs("FULL")
        self.assertEqual(self.seen('bounds_value', full), -2**31)
        full.set(2**31 - 1)
        self.assertEqual(self.seen('bounds_value', full), 2**31 - 1)

    def test_a_knob_whose_max_is_zero_reads_zero_but_keeps_its_raw_value(self):
        zero, = self.track.use(BOUNDS).knobs("ZERO")
        zero.turn(+10)
        self.assertGreater(zero.raw, 0)
        self.assertEqual(self.seen('bounds_value', zero), 0)

    def test_a_knob_with_equal_bounds_reads_that_value_but_keeps_its_raw_value(self):
        fixed, = self.track.use(BOUNDS).knobs("FIXED")
        fixed.turn(+10)
        self.assertGreater(fixed.raw, 0)
        self.assertEqual(self.seen('bounds_value', fixed), 7)

    def test_a_knob_on_two_pages_is_one_knob(self):
        pitch, = self.track.use(PAGES).knobs("PITCH")
        pitch.turn(+8, page=0)
        first = pitch.raw
        self.assertGreater(first, 0)
        pitch.turn(+8, page=1)
        self.assertGreater(pitch.raw, first)

    def test_the_second_page_has_knobs_of_its_own(self):
        amnt, other = self.track.use(PAGES).knobs("AMNT", "OTHER")
        other.turn(+10)
        self.assertGreater(other.raw, 0)
        self.assertEqual(amnt.raw, 0)

    def test_the_callback_sees_the_page_it_is_called_for(self):
        amnt, = self.track.use(PAGES).knobs("AMNT")
        self.track.show(0)
        self.assertEqual(self.read('pages_page'), 0)
        self.track.show(1)
        self.assertEqual(self.read('pages_page'), 1)

    def test_the_callback_sees_a_knob_that_is_not_on_the_page(self):
        amnt, = self.track.use(PAGES).knobs("AMNT")
        amnt.turn(+20)
        self.track.show(1)
        self.assertEqual(self.read('pages_raw', amnt.spec['id']), amnt.raw)

    def test_each_track_has_its_own_values(self):
        first, = self.tracks[0].use(EIGHT).knobs("KNB A")
        second, = self.tracks[1].use(EIGHT).knobs("KNB A")
        first.turn(+20)
        self.assertGreater(first.raw, 0)
        self.assertEqual(second.raw, 0)
        second.turn(+30)
        self.assertGreater(second.raw, first.raw)

    def test_each_track_has_its_own_page(self):
        self.tracks[0].use(PAGES)
        self.tracks[0].show(1)
        self.tracks[1].use(PAGES)
        self.tracks[1].show(0)
        self.assertEqual(self.tracks[0].page, 1)
        self.assertEqual(self.tracks[1].page, 0)


if __name__ == '__main__':
    unittest.main()
