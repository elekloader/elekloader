# SPDX-License-Identifier: GPL-2.0-or-later
"""Every knob of an SDK machine is an LFO destination. The LFO's destination list names the knobs
with the machine's labels as they stand, and an LFO keeps pointing at its knob when the knob is
renamed or hidden.

Run as tests/sdk_alpha_harness.py says.
"""
import unittest

from sdk_alpha_harness import sdk_machine, sdk_tests

NO_PAGE = 0xffffffff            # .present's page while it only names an LFO destination

# Eight knobs on one page: XYZ, NAME, then P03 to P08. While NAME is not 0 the callback calls XYZ "QWE",
# and at 127 it hides XYZ. It keeps the page it was called for.
LFO = sdk_machine("Lfo", """
    #include "elekloader/sdk_alpha_machine.h"

    volatile int32_t lfo_page;

    static const struct elek_knob knobs[] = {
        { "XYZ", .max = 127 }, { "NAME", .max = 127 }, { "P03", .max = 127 }, { "P04", .max = 127 },
        { "P05", .max = 127 }, { "P06", .max = 127 }, { "P07", .max = 127 }, { "P08", .max = 127 },
    };
    static void present(const struct elek_ui *ui)
    {
        lfo_page = ui->page;
        if (ui->knob[1]) {
            ui->view[0].label = "QWE";
            ui->view[0].hidden = ui->knob[1] == 127;
        }
    }
    static const struct elek_machine lfo = {
        .id = @ID@, .name = "Lfo", .short_name = "LFO", ELEK_KNOBS(knobs),
        .pages = { { "LFO", { &knobs[1], &knobs[2], &knobs[3], &knobs[0],
                              &knobs[4], &knobs[5], &knobs[6], &knobs[7] } } },
        .present = present,
    };
    ELEK_REGISTER_MACHINE(lfo);
""")

# Fifteen knobs on two pages (the Digitakt's most): XYZ, NAME, then P03 to P15. XYZ is on both pages.
FIFTEEN = sdk_machine("Fifteen", """
    #include "elekloader/sdk_alpha_machine.h"

    volatile int32_t fifteen_page;

    static const struct elek_knob knobs[] = {
        { "XYZ", .max = 127 }, { "NAME", .max = 127 }, { "P03", .max = 127 }, { "P04", .max = 127 },
        { "P05", .max = 127 }, { "P06", .max = 127 }, { "P07", .max = 127 }, { "P08", .max = 127 },
        { "P09", .max = 127 }, { "P10", .max = 127 }, { "P11", .max = 127 }, { "P12", .max = 127 },
        { "P13", .max = 127 }, { "P14", .max = 127 }, { "P15", .max = 127 },
    };
    static void present(const struct elek_ui *ui)
    {
        fifteen_page = ui->page;
        if (ui->knob[1]) {
            ui->view[0].label = "QWE";
            ui->view[0].hidden = ui->knob[1] == 127;
        }
    }
    static const struct elek_machine fifteen = {
        .id = @ID@, .name = "Fifteen", .short_name = "FIFT", ELEK_KNOBS(knobs),
        .pages = {
            { "FIRST", { &knobs[1], &knobs[2], &knobs[3], &knobs[0], &knobs[4], &knobs[5], &knobs[6], &knobs[7] } },
            { "SECOND", { &knobs[8], &knobs[9], &knobs[10], &knobs[11], &knobs[12], &knobs[13], &knobs[14], &knobs[0] } },
        },
        .present = present,
    };
    ELEK_REGISTER_MACHINE(fifteen);
""")


@sdk_tests
class LfoDestinations:
    def test_an_lfo_can_point_at_every_knob(self):
        used = self.track.use(LFO)
        for number in range(1, self.device.lfo_count + 1):
            lfo = self.track.lfo(number)
            for knob in used.all_knobs():
                with self.subTest(lfo=number, knob=knob.spec['label']):
                    lfo.choose(knob)
                    self.assertEqual(lfo.target.spec['id'], knob.spec['id'])

    def test_the_list_names_every_knob_with_its_label(self):
        used = self.track.use(LFO)
        for number in range(1, self.device.lfo_count + 1):
            lfo = self.track.lfo(number)
            for knob in used.all_knobs():
                with self.subTest(lfo=number, knob=knob.spec['label']):
                    lfo.choose(knob)
                    self.assertEqual(lfo.label_of(knob), knob.spec['label'])

    def test_the_callback_is_called_without_a_page_while_the_list_names_a_knob(self):
        xyz, = self.track.use(LFO).knobs("XYZ")
        self.track.lfo(1).choose(xyz)
        self.assertEqual(self.read('lfo_page'), NO_PAGE)

    def test_a_renamed_knob_is_renamed_in_the_list(self):
        xyz, name = self.track.use(LFO).knobs("XYZ", "NAME")
        name.turn(+24)
        self.track.lfo(1).choose(xyz)
        self.assertEqual(self.track.lfo(1).label_of(xyz), "QWE")

    def test_an_lfo_keeps_its_knob_when_the_knob_is_renamed_and_hidden(self):
        xyz, name = self.track.use(LFO).knobs("XYZ", "NAME")
        for number in range(1, self.device.lfo_count + 1):
            self.track.lfo(number).choose(xyz)
            self.track.lfo(number).confirm()
        for value in (24, 127, 0):
            with self.subTest(name=value):
                name.set(value)
                for number in range(1, self.device.lfo_count + 1):
                    lfo = self.track.lfo(number)
                    self.assertEqual(lfo.target.spec['id'], xyz.spec['id'])   # kept, before the list moves
                    lfo.choose(xyz)
                    self.assertEqual(lfo.label_of(xyz), "XYZ" if value == 0 else "QWE")
                    lfo.confirm()
        self.assertEqual(xyz.label, "XYZ")    # returning to the SDK page restores its page context
        self.assertEqual(self.read('lfo_page'), 0)


@sdk_tests
class FifteenKnobs:
    def test_an_lfo_can_point_at_every_one_of_fifteen_knobs(self):
        used = self.track.use(FIFTEEN)
        lfo = self.track.lfo(1)
        for knob in used.all_knobs():
            with self.subTest(knob=knob.spec['label']):
                lfo.choose(knob)
                self.assertEqual(lfo.target.spec['id'], knob.spec['id'])
                self.assertEqual(lfo.label_of(knob), knob.spec['label'])


if __name__ == '__main__':
    unittest.main()
