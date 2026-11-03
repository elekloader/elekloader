# SPDX-License-Identifier: GPL-2.0-or-later
"""A knob can have a 16x16 glyph in place of the stock graphic, declared or chosen by .present. The
pixels on the screen are the glyph's, a different glyph does not change the value, .present can put
the stock graphic back, every track has its own, and the value shown while a knob turns is not cut
short.

Run with digiemu's Python:

  ELEKLOADER_DIGIEMU=<digiemu> ELEKLOADER_STOCK_154=<Digitakt_OS1.54.syx> \
  ELEKLOADER_STOCK_DN1_144=<Digitone_and_Digitone_Keys_OS1.44.syx> ELEKLOADER_CROSS=m68k-elf- \
  uv run --project "$ELEKLOADER_DIGIEMU" python tests/test_sdk_alpha_glyphs.py
"""
import unittest

from sdk_alpha_harness import sdk_machine, sdk_tests

# 16 rows of 16 pixels, the top row first, bit 15 the left pixel.
DIAMOND = (0x0100, 0x0380, 0x07c0, 0x0fe0, 0x1ff0, 0x3ff8, 0x7ffc, 0xfffe,
           0xfffe, 0x7ffc, 0x3ff8, 0x1ff0, 0x0fe0, 0x07c0, 0x0380, 0x0100)
DIGITS = {                      # 1 to 4, in the middle of the 16 rows
    1: (0, 0, 0, 0x0100, 0x0300, 0x0100, 0x0100, 0x0100, 0x0100, 0x0380, 0, 0, 0, 0, 0, 0),
    2: (0, 0, 0, 0x0380, 0x0440, 0x0040, 0x0080, 0x0100, 0x0200, 0x07c0, 0, 0, 0, 0, 0, 0),
    3: (0, 0, 0, 0x0780, 0x0040, 0x0040, 0x0380, 0x0040, 0x0040, 0x0780, 0, 0, 0, 0, 0, 0),
    4: (0, 0, 0, 0x0080, 0x0180, 0x0280, 0x0480, 0x07c0, 0x0080, 0x0080, 0, 0, 0, 0, 0, 0),
}


def rows(glyph):
    """A glyph as a C list."""
    return ', '.join('0x%04x' % row for row in glyph)


# One knob with a declared glyph, and no callback.
SHAPE = sdk_machine("Shape", """
    #include "elekloader/sdk_alpha_machine.h"

    static const struct elek_ui_glyph diamond = { .rows = { @DIAMOND@ } };
    static const struct elek_ui_knob shape = { .id = 0, .label = "SHAPE", .glyph = &diamond };
    static const struct elek_ui_page pages[] = {
        { .label = "SHAPE", .knob = { &shape } },
    };
    static const struct elek_machine shape_machine = {
        .id = @ID@, .name = "Shape", .short_name = "SHAP", .pages = pages, .page_count = 1,
    };
    ELEK_REGISTER_MACHINE(shape_machine);
""", diamond=rows(DIAMOND))

# STATE (1 to 4, on encoder A) shows the digit it holds as its glyph. STOCK (0 or 1, on B) at 1 makes
# the callback pick the stock graphic instead.
STATES = sdk_machine("States", """
    #include "elekloader/sdk_alpha_machine.h"

    static const struct elek_ui_glyph digits[] = {
        { .rows = { @ONE@ } }, { .rows = { @TWO@ } }, { .rows = { @THREE@ } }, { .rows = { @FOUR@ } },
    };
    static const struct elek_ui_knob state = {
        .id = 0, .label = "STATE", .min = ELEK_UI_BOUND(1), .max = ELEK_UI_BOUND(4), .glyph = &digits[0],
    };
    static const struct elek_ui_knob stock = { .id = 1, .label = "STOCK", .max = ELEK_UI_BOUND(1) };
    static const struct elek_ui_knob hide = { .id = 2, .label = "HIDE", .max = ELEK_UI_BOUND(1) };
    static const struct elek_ui_page pages[] = {
        { .label = "STATES", .knob = { &state, &stock, &hide } },
    };
    static void present(const struct elek_ui_context *ctx, struct elek_ui_presentation *out)
    {
        if (ctx->knob)
            out->knob[0]->glyph = ctx->knob[1]->value ? 0 : &digits[ctx->knob[0]->value - 1];
        if (ctx->knob && ctx->knob[2]->value)
            out->knob[0]->visible = 0;
    }
    static const struct elek_machine states = {
        .id = @ID@, .name = "States", .short_name = "STAT",
        .pages = pages, .page_count = 1, .present = present,
    };
    ELEK_REGISTER_MACHINE(states);
""", one=rows(DIGITS[1]), two=rows(DIGITS[2]), three=rows(DIGITS[3]), four=rows(DIGITS[4]))

GRID = sdk_machine("Glyph grid", """
    #include "elekloader/sdk_alpha_machine.h"

    static const struct elek_ui_glyph digits[] = {
        { .rows = { @ONE@ } }, { .rows = { @TWO@ } }, { .rows = { @THREE@ } }, { .rows = { @FOUR@ } },
    };
    #define KNOB(n) { .id = n, .label = "GLYPH", .glyph = &digits[(n) % 4] }
    static const struct elek_ui_knob knobs[] = {
        KNOB(0), KNOB(1), KNOB(2), KNOB(3), KNOB(4), KNOB(5), KNOB(6), KNOB(7),
    };
    static const struct elek_ui_page pages[] = {
        { .label = "GRID", .knob = {
            &knobs[7], &knobs[6], &knobs[5], &knobs[4], &knobs[3], &knobs[2], &knobs[1], &knobs[0],
        } },
    };
    static const struct elek_machine grid = {
        .id = @ID@, .name = "Glyph grid", .short_name = "GRID", .pages = pages, .page_count = 1,
    };
    ELEK_REGISTER_MACHINE(grid);
""", one=rows(DIGITS[1]), two=rows(DIGITS[2]), three=rows(DIGITS[3]), four=rows(DIGITS[4]))

# A v3 knob ends before the glyph pointer. The next word is deliberately not a valid glyph.
LEGACY = sdk_machine("Legacy glyph", """
    #define sdk_alpha_dt1_ui_v5 sdk_alpha_dt1_ui_v3
    #include "elekloader/sdk_alpha_machine.h"

    struct legacy_knob {
        uint32_t id;
        const char *label;
        struct elek_ui_bound min, max;
    };
    _Static_assert(sizeof(struct legacy_knob) == 24, "v3 knob layout");
    static const struct {
        struct legacy_knob knob;
        const void *next_word;
    } legacy = { .knob = { .id = 0, .label = "PLAIN" }, .next_word = (void *)1 };
    static const struct elek_ui_page pages[] = {
        { .label = "LEGACY", .knob = { (const struct elek_ui_knob *)&legacy.knob } },
    };
    static const struct elek_machine machine = {
        .id = @ID@, .name = "Legacy glyph", .short_name = "OLD", .pages = pages, .page_count = 1,
    };
    ELEK_REGISTER_MACHINE(machine);
""")


@sdk_tests
class Glyphs:
    def test_glyphs_follow_logical_ids_in_all_eight_positions(self):
        self.track.use(GRID)
        self.track.show()
        self.assertTrue(self.panel.wait_until(lambda: tuple(self.panel.crop(25, 13, 16, 16)) == DIGITS[4]))
        for encoder in range(8):
            with self.subTest(encoder='ABCDEFGH'[encoder]):
                self.assertEqual(tuple(self.panel.crop(25 + 26 * (encoder % 4),
                                                       13 + 27 * (encoder // 4), 16, 16)),
                                 DIGITS[(7 - encoder) % 4 + 1])

    def test_an_older_knob_descriptor_keeps_the_stock_graphic(self):
        knob, = self.track.use(LEGACY).knobs("PLAIN")
        before = knob.glyph_on_screen
        self.assertTrue(any(before))
        knob.turn(+32)
        self.assertGreater(knob.raw, 0)
        self.assertNotEqual(knob.glyph_on_screen, before)

    def test_a_hidden_knob_does_not_draw_its_glyph(self):
        state, hide = self.track.use(STATES).knobs("STATE", "HIDE")
        state.set(2)
        hide.set(1)
        self.assertFalse(state.visible)
        self.assertEqual(state.pixels, self.panel.control_pixels(3))
        hide.set(0)
        self.assertEqual(state.value, 2)
        self.assertEqual(state.glyph_on_screen, DIGITS[2])

    def test_a_declared_glyph_is_drawn_in_place_of_the_stock_graphic(self):
        shape, = self.track.use(SHAPE).knobs("SHAPE")
        self.assertEqual(shape.glyph_on_screen, DIAMOND)

    def test_a_declared_glyph_stays_while_the_value_changes(self):
        shape, = self.track.use(SHAPE).knobs("SHAPE")
        shape.turn(+32)
        self.assertGreater(shape.raw, 0)
        self.assertEqual(shape.glyph_on_screen, DIAMOND)

    def test_a_callback_picks_a_glyph_by_the_value(self):
        state, stock = self.track.use(STATES).knobs("STATE", "STOCK")
        for wanted in (1, 2, 3, 4, 3, 2):
            with self.subTest(value=wanted):
                state.set(wanted)
                self.assertEqual(state.glyph_on_screen, DIGITS[wanted])

    def test_a_callback_can_put_the_stock_graphic_back_without_changing_the_value(self):
        state, stock = self.track.use(STATES).knobs("STATE", "STOCK")
        state.set(2)
        stock.set(1)
        self.assertEqual(state.value, 2)
        self.assertNotEqual(state.glyph_on_screen, DIGITS[2])
        stock.set(0)
        self.assertEqual(state.value, 2)
        self.assertEqual(state.glyph_on_screen, DIGITS[2])

    def test_each_track_has_its_own_glyph(self):
        state, stock = self.tracks[0].use(STATES).knobs("STATE", "STOCK")
        other, other_stock = self.tracks[1].use(STATES).knobs("STATE", "STOCK")
        state.set(2)
        self.assertEqual(state.glyph_on_screen, DIGITS[2])
        self.assertEqual(other.glyph_on_screen, DIGITS[1])

    def test_the_value_stays_on_the_screen_while_the_glyph_changes(self):
        state, stock = self.track.use(STATES).knobs("STATE", "STOCK")
        self.assertEqual(state.glyph_on_screen, DIGITS[1])
        self.wait(6000)                         # let any value shown on entry time out
        label = state.label_pixels
        for i in range(64):
            state.turn(+1, settle=False)
            if i >= 6:
                self.assertNotEqual(state.label_pixels, label, 'the value went away while turning')
            if state.value == 4:
                break
        self.wait(1000)
        self.assertNotEqual(state.label_pixels, label, 'the value went away early')
        self.wait(5000)
        self.assertEqual(state.label_pixels, label, 'the label did not come back')


if __name__ == '__main__':
    unittest.main()
