# SPDX-License-Identifier: GPL-2.0-or-later
"""A saved project brings the SDK machines back as they were: the machine on each track, its knobs'
values, what its callback shows and hides, and where its LFOs point.

The tests save and load through the device's own menus, as a user does.

Run as tests/sdk_alpha_harness.py says.
"""
import unittest

from sdk_alpha_harness import sdk_machine, sdk_tests

# HIDE on encoder A, TRGT on B. While HIDE is not 0 the callback calls TRGT "QWE", and at 127 it
# hides TRGT.
SAVED = sdk_machine("Saved", """
    #include "elekloader/sdk_alpha_machine.h"

    static const struct elek_knob knobs[] = { { "HIDE", .max = 127 }, { "TRGT", .max = 127 } };
    static void present(const struct elek_ui *ui)
    {
        if (ui->knob[0]) {
            ui->view[1].label = "QWE";
            ui->view[1].hidden = ui->knob[0] == 127;
        }
    }
    static const struct elek_machine saved = {
        .id = @ID@, .name = "Saved", .short_name = "SAVD", ELEK_KNOBS(knobs),
        .pages = { { "SAVED", { [ELEK_A] = &knobs[0], [ELEK_B] = &knobs[1] } } }, .present = present,
    };
    ELEK_REGISTER_MACHINE(saved);
""")


@sdk_tests
class Saving:
    def test_a_saved_project_brings_the_machine_back(self):
        self.track.use(SAVED)
        self.save_project()
        self.track.use_stock()
        self.load_project()
        self.assertEqual(self.track.machine_id, SAVED.id)

    def test_a_saved_project_brings_the_knob_values_back(self):
        hide, trgt = self.track.use(SAVED).knobs("HIDE", "TRGT")
        hide.turn(+40)
        trgt.turn(+20)
        saved = hide.raw, trgt.raw
        self.assertGreater(min(saved), 0)
        self.save_project()
        hide.set(0)
        trgt.set(0)
        self.load_project()
        self.assertEqual((hide.raw, trgt.raw), saved)

    def test_each_track_comes_back_with_its_own_values(self):
        first, = self.tracks[0].use(SAVED).knobs("HIDE")
        second, = self.tracks[1].use(SAVED).knobs("HIDE")
        first.turn(+20)
        second.turn(+50)
        saved = first.raw, second.raw
        self.assertNotEqual(*saved)
        self.save_project()
        first.set(0)
        second.set(0)
        self.load_project()
        self.assertEqual((first.raw, second.raw), saved)

    def test_a_loaded_project_shows_what_the_callback_shows_and_hides(self):
        hide, trgt = self.track.use(SAVED).knobs("HIDE", "TRGT")
        hide.set(127)
        self.assertEqual((trgt.label, trgt.visible), ("QWE", False))
        self.save_project()
        hide.set(0)
        self.assertEqual((trgt.label, trgt.visible), ("TRGT", True))
        self.load_project()
        self.assertEqual((trgt.label, trgt.visible), ("QWE", False))

    def test_a_saved_project_brings_an_lfo_destination_back(self):
        hide, trgt = self.track.use(SAVED).knobs("HIDE", "TRGT")
        lfo = self.track.lfo(1)
        lfo.choose(trgt)
        lfo.confirm()
        self.save_project()
        lfo.choose(hide)
        lfo.confirm()
        self.assertEqual(lfo.target.spec['id'], hide.spec['id'])
        self.load_project()
        self.assertEqual(lfo.target.spec['id'], trgt.spec['id'])


if __name__ == '__main__':
    unittest.main()
