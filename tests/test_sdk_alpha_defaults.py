# SPDX-License-Identifier: GPL-2.0-or-later
"""Mapped defaults on selection/reset; edits survive page paste and project load.

The scenarios use Device adapters. Targets without the defaults backend are explicitly skipped.
Run as tests/sdk_alpha_harness.py says.
"""
import unittest
from sdk_alpha_harness import sdk_machine, sdk_tests

DEFAULTS = sdk_machine('Defaults', """
    #include "elekloader/sdk_alpha_machine.h"
    enum { FINE, LEVEL, ZERO, ABOVE };
    static const struct elek_knob knobs[] = {
        [FINE] = { "FINE", .min = -50, .max = 50 },                 /* omitted: 0 */
        [LEVEL] = { "LEVEL", .max = 127, .def = 100 },
        [ZERO] = { "ZERO", .min = -20, .max = 127 },
        [ABOVE] = { "ABOVE", .min = 10, .max = 20 },               /* 0, clamped: 10 */
    };
    static const struct elek_machine machine = {
        .id = @ID@, .name = "Defaults", .short_name = "DEFS", ELEK_KNOBS(knobs),
        .pages = {
            { "DEFAULTS", { &knobs[FINE], &knobs[LEVEL], &knobs[ZERO], &knobs[ABOVE] } },
            { "SHARED", { [ELEK_H] = &knobs[FINE] } },
        },
    };
    ELEK_REGISTER_MACHINE(machine);
""")


EXTRA = sdk_machine('Extra default', """
    #include "elekloader/sdk_alpha_machine.h"
    #define K(i) { "V" #i, .max = 127, .def = 100 }
    static const struct elek_knob knobs[] = { K(0), K(1), K(2), K(3), K(4), K(5), K(6), K(7), K(8),
        K(9), K(10), K(11), K(12), K(13), K(14) };
    static const struct elek_machine extra_machine = {
        .id = @ID@, .name = "Extra default", .short_name = "EXTR", ELEK_KNOBS(knobs),
        .pages = {
            { "FIRST", { &knobs[0], &knobs[1], &knobs[2], &knobs[3], &knobs[4], &knobs[5], &knobs[6], &knobs[7] } },
            { "EXTRA", { &knobs[8], &knobs[9], &knobs[10], &knobs[11], &knobs[12], &knobs[13], &knobs[14] } },
        },
    };
    ELEK_REGISTER_MACHINE(extra_machine);
""")


@sdk_tests
class Defaults:
    requires = ('defaults',)

    def test_second_page_extra_uses_its_own_default(self):
        used = self.track.use(EXTRA)
        extra = used.all_knobs()[-1]
        self.assertEqual(extra.value, 100)
        before = extra.raw
        extra.turn(10)
        self.assertGreater(extra.raw, before)
        self.track.page_action('clear')
        self.assertEqual(extra.value, 100)
        self.assertEqual(extra.raw, before)

    def test_selection_uses_mapped_defaults(self):
        knobs = self.track.use(DEFAULTS).all_knobs()
        self.assertEqual([k.value for k in knobs], [0, 100, 0, 10])

    def test_clear_restores_defaults(self):
        knobs = self.track.use(DEFAULTS).all_knobs()
        for knob in knobs:
            knob.turn(10)
        self.track.page_action('clear')
        self.assertEqual([k.value for k in knobs], [0, 100, 0, 10])

    def test_one_value_is_shared_between_pages(self):
        fine = self.track.use(DEFAULTS).knob('FINE')
        fine.turn(5, page=0)
        first = fine.raw
        fine.turn(5, page=1)
        self.assertGreater(fine.raw, first)
        self.track.show(0)
        self.assertGreater(fine.raw, first)
        self.track.page_action('clear')
        self.assertEqual(fine.value, 0)

    def test_paste_restores_edits_instead_of_defaults(self):
        knobs = self.track.use(DEFAULTS).all_knobs()
        for knob in knobs:
            knob.turn(8)
        edited = [k.raw for k in knobs]
        self.track.page_action('copy')
        self.track.page_action('clear')
        self.assertEqual([k.value for k in knobs], [0, 100, 0, 10])
        self.track.page_action('paste')
        self.assertEqual([k.raw for k in knobs], edited)

    def test_load_restores_edits_instead_of_defaults(self):
        knobs = self.track.use(DEFAULTS).all_knobs()
        for knob in knobs:
            knob.turn(8)
        edited = [k.raw for k in knobs]
        self.save_project()
        self.track.page_action('clear')
        self.load_project()
        self.assertEqual([k.raw for k in knobs], edited)


if __name__ == '__main__':
    unittest.main()
