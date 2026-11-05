# SPDX-License-Identifier: GPL-2.0-or-later
"""Mapped defaults on selection/reset; edits survive page paste and project load.

The scenarios use Device adapters. Targets without the defaults backend are explicitly skipped.
Run as tests/sdk_alpha_harness.py says.
"""
import unittest
from sdk_alpha_harness import sdk_machine, sdk_tests

DEFAULTS = sdk_machine('Defaults', """
    #include "elekloader/sdk_alpha_machine.h"
    static const struct elek_ui_knob fine = {
        .id = 0, .label = "FINE", .min = ELEK_UI_BOUND(-50), .max = ELEK_UI_BOUND(50),
        .def = ELEK_UI_BOUND(0),
    };
    static const struct elek_ui_knob level = {
        .id = 1, .label = "LEVEL", .def = ELEK_UI_BOUND(100),
    };
    static const struct elek_ui_knob zero = {
        .id = 2, .label = "ZERO", .min = ELEK_UI_BOUND(-20), .def = ELEK_UI_BOUND(0),
    };
    static const struct elek_ui_knob omit = {
        .id = 3, .label = "OMIT", .min = ELEK_UI_BOUND(-20),
    };
    static const struct elek_ui_page pages[] = {
        { .label = "DEFAULTS", .knob = { &fine, &level, &zero, &omit } },
        { .label = "SHARED", .knob = { [ELEK_ENCODER_H] = &fine } },
    };
    static const struct elek_machine machine = {
        .id = @ID@, .name = "Defaults", .short_name = "DEFS", .pages = pages, .page_count = 2,
    };
    ELEK_REGISTER_MACHINE(machine);
""")


EXTRA = sdk_machine('Extra default', """
    #include "elekloader/sdk_alpha_machine.h"
    #define K(i) { .id = i, .label = "V" #i, .def = ELEK_UI_BOUND(100) }
    static const struct elek_ui_knob knobs[] = { K(0), K(1), K(2), K(3), K(4), K(5), K(6), K(7), K(8),
        K(9), K(10), K(11), K(12), K(13), K(14) };
    static const struct elek_ui_page pages[] = {
        { .label = "FIRST", .knob = { &knobs[0], &knobs[1], &knobs[2], &knobs[3],
                                     &knobs[4], &knobs[5], &knobs[6], &knobs[7] } },
        { .label = "EXTRA", .knob = { &knobs[8], &knobs[9], &knobs[10], &knobs[11],
                                     &knobs[12], &knobs[13], &knobs[14] } },
    };
    static const struct elek_machine extra_machine = {
        .id = @ID@, .name = "Extra default", .short_name = "EXTR", .pages = pages, .page_count = 2,
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
        self.assertEqual([k.value for k in knobs], [0, 100, 0, -20])

    def test_clear_restores_defaults(self):
        knobs = self.track.use(DEFAULTS).all_knobs()
        for knob in knobs:
            knob.turn(10)
        self.track.page_action('clear')
        self.assertEqual([k.value for k in knobs], [0, 100, 0, -20])

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
        self.assertEqual([k.value for k in knobs], [0, 100, 0, -20])
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
