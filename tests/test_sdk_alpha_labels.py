# SPDX-License-Identifier: GPL-2.0-or-later
"""A .present callback can change a knob's label. The label shown follows the callback, comes back
when the callback stops changing it, belongs to its track, is cut at five characters, and the
value shown while a knob turns is not cut short by the change.

Run as tests/sdk_alpha_harness.py says.
"""
import unittest

from sdk_alpha_harness import sdk_machine, sdk_tests

# AMNT on encoder A, PITCH on H, far from it. While AMNT is not 0 the machine labels PITCH "A 037", from AMNT's
# value; at 0 PITCH has its own label.
LABELS = sdk_machine("Labels", """
    #include "elekloader/sdk_alpha_machine.h"

    static const struct elek_ui_knob amnt = { .id = 0, .label = "AMNT" };
    static const struct elek_ui_knob pitch = { .id = 1, .label = "PITCH" };
    static const struct elek_ui_page pages[] = {
        { .label = "LABELS", .knob = { [ELEK_ENCODER_A] = &amnt, [ELEK_ENCODER_H] = &pitch } },
    };
    static void present(const struct elek_ui_context *ctx, struct elek_ui_presentation *out)
    {
        static char text[] = "A 000";
        unsigned amount;
        if (!ctx->knob)
            return;
        amount = ctx->knob[0]->value;
        if (amount) {
            text[2] = '0' + amount / 100;
            text[3] = '0' + amount / 10 % 10;
            text[4] = '0' + amount % 10;
            out->knob[1]->label = text;
        }
    }
    static const struct elek_machine labels = {
        .id = @ID@, .name = "Labels", .short_name = "LABL",
        .pages = pages, .page_count = 1, .present = present,
    };
    ELEK_REGISTER_MACHINE(labels);
""")

# One knob, which the callback labels with eight characters: the SDK keeps five.
LONG = sdk_machine("Long label", """
    #include "elekloader/sdk_alpha_machine.h"

    static const struct elek_ui_knob knob = { .id = 0, .label = "SHORT" };
    static const struct elek_ui_page pages[] = {
        { .label = "LONG", .knob = { &knob } },
    };
    static void present(const struct elek_ui_context *ctx, struct elek_ui_presentation *out)
    {
        if (ctx->knob)
            out->knob[0]->label = "ABCDEFGH";
    }
    static const struct elek_machine long_label = {
        .id = @ID@, .name = "Long label", .short_name = "LONG",
        .pages = pages, .page_count = 1, .present = present,
    };
    ELEK_REGISTER_MACHINE(long_label);
""")


@sdk_tests
class Labels:
    def test_a_knob_has_its_declared_label_until_the_callback_changes_it(self):
        amnt, pitch = self.track.use(LABELS).knobs("AMNT", "PITCH")
        self.assertEqual(pitch.label, "PITCH")

    def test_a_callback_can_relabel_a_knob(self):
        amnt, pitch = self.track.use(LABELS).knobs("AMNT", "PITCH")
        amnt.set(37)
        self.assertEqual(pitch.label, "A 037")

    def test_the_declared_label_comes_back_when_the_callback_stops(self):
        amnt, pitch = self.track.use(LABELS).knobs("AMNT", "PITCH")
        amnt.set(37)
        amnt.set(0)
        self.assertEqual(pitch.label, "PITCH")

    def test_a_changed_label_is_drawn_and_restored(self):
        amnt, pitch = self.track.use(LABELS).knobs("AMNT", "PITCH")
        self.assertTrue(self.panel.wait_until(lambda: any(pitch.label_pixels)))
        declared = pitch.label_pixels
        amnt.set(37)
        self.assertTrue(self.panel.wait_until(lambda: pitch.label_pixels != declared),
                        'the changed label was not drawn')
        self.assertTrue(any(pitch.label_pixels))
        amnt.set(0)
        self.assertTrue(self.panel.wait_until(lambda: pitch.label_pixels == declared),
                        'the declared label was not drawn again')

    def test_each_track_has_its_own_labels(self):
        amnt, pitch = self.tracks[0].use(LABELS).knobs("AMNT", "PITCH")
        other_amnt, other_pitch = self.tracks[1].use(LABELS).knobs("AMNT", "PITCH")
        amnt.set(37)
        self.assertEqual(pitch.label, "A 037")
        self.assertEqual(other_pitch.label, "PITCH")

    def test_a_label_is_cut_at_five_characters(self):
        knob, = self.track.use(LONG).knobs("SHORT")
        self.assertEqual(knob.label, "ABCDE")

    def test_the_value_stays_on_the_screen_while_a_label_changes(self):
        amnt, pitch = self.track.use(LABELS).knobs("AMNT", "PITCH")
        self.wait(6000)                         # let any value shown on entry time out
        label = amnt.label_pixels
        for i in range(24):
            amnt.turn(+1, settle=False)         # each detent relabels PITCH
            if i >= 6:
                self.assertNotEqual(amnt.label_pixels, label, 'the value went away while turning')
        self.wait(1000)
        self.assertNotEqual(amnt.label_pixels, label, 'the value went away early')
        self.wait(5000)
        self.assertEqual(amnt.label_pixels, label, 'the label did not come back')


if __name__ == '__main__':
    unittest.main()
