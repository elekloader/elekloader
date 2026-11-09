# SPDX-License-Identifier: GPL-2.0-or-later
"""A .present callback can change a knob's label. The label shown follows the callback, comes back
when the callback stops changing it, belongs to its track, is cut at five characters, and the
value shown while a knob turns is not cut short by the change.

Most tests read `Knob.label`, the adapter's copy of the label; one compares the screen's pixels.

Run as tests/sdk_alpha_harness.py says.
"""
import unittest

from sdk_alpha_harness import sdk_machine, sdk_tests

# AMNT on encoder A, PITCH on H, far from it. While AMNT is not 0 the machine labels PITCH "A 37", from AMNT's
# value; at 0 PITCH has its own label.
LABELS = sdk_machine("Labels", """
    #include "elekloader/sdk_alpha_machine.h"

    static const struct elek_knob knobs[] = { { "AMNT", .max = 127 }, { "PITCH", .max = 127 } };
    static void present(const struct elek_ui *ui)
    {
        if (ui->knob[0])
            ui->view[1].label = elek_num(ui, "A ", ui->knob[0]);
    }
    static const struct elek_machine labels = {
        .id = @ID@, .name = "Labels", .short_name = "LABL", ELEK_KNOBS(knobs),
        .pages = { { "LABELS", { [ELEK_A] = &knobs[0], [ELEK_H] = &knobs[1] } } }, .present = present,
    };
    ELEK_REGISTER_MACHINE(labels);
""")

# LABELS's layout with PITCH declared "A 37": how the screen draws that label at H.
REFERENCE = sdk_machine("Label reference", """
    #include "elekloader/sdk_alpha_machine.h"

    static const struct elek_knob knobs[] = { { "AMNT", .max = 127 }, { "A 37", .max = 127 } };
    static const struct elek_machine reference = {
        .id = @ID@, .name = "Label reference", .short_name = "LABL", ELEK_KNOBS(knobs),
        .pages = { { "LABELS", { [ELEK_A] = &knobs[0], [ELEK_H] = &knobs[1] } } },
    };
    ELEK_REGISTER_MACHINE(reference);
""")

# One knob, which the callback labels with eight characters: the SDK keeps five.
LONG = sdk_machine("Long label", """
    #include "elekloader/sdk_alpha_machine.h"

    static const struct elek_knob knobs[] = { { "SHORT", .max = 127 } };
    static void present(const struct elek_ui *ui)
    {
        ui->view[0].label = "ABCDEFGH";
    }
    static const struct elek_machine long_label = {
        .id = @ID@, .name = "Long label", .short_name = "LONG", ELEK_KNOBS(knobs),
        .pages = { { "LONG", { &knobs[0] } } }, .present = present,
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
        self.assertEqual(pitch.label, "A 37")

    def test_the_declared_label_comes_back_when_the_callback_stops(self):
        amnt, pitch = self.track.use(LABELS).knobs("AMNT", "PITCH")
        amnt.set(37)
        amnt.set(0)
        self.assertEqual(pitch.label, "PITCH")

    def test_a_changed_label_is_drawn_and_restored(self):
        """On the screen, not the adapter's copy: H shows what a declared "A 37" shows, then PITCH."""
        _, a37 = self.tracks[1].use(REFERENCE).knobs("AMNT", "A 37")
        self.assertTrue(self.panel.wait_until(lambda: any(a37.label_pixels)))
        drawn = a37.label_pixels
        amnt, pitch = self.track.use(LABELS).knobs("AMNT", "PITCH")
        self.assertTrue(self.panel.wait_until(lambda: any(pitch.label_pixels)))
        declared = pitch.label_pixels
        self.assertNotEqual(declared, drawn)
        amnt.set(37)
        self.assertTrue(self.panel.wait_until(lambda: pitch.label_pixels == drawn),
                        'the changed label was not drawn')
        amnt.set(0)
        self.assertTrue(self.panel.wait_until(lambda: pitch.label_pixels == declared),
                        'the declared label was not drawn again')

    def test_each_track_has_its_own_labels(self):
        amnt, pitch = self.tracks[0].use(LABELS).knobs("AMNT", "PITCH")
        other_amnt, other_pitch = self.tracks[1].use(LABELS).knobs("AMNT", "PITCH")
        amnt.set(37)
        self.assertEqual(pitch.label, "A 37")
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
