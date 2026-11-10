# SPDX-License-Identifier: GPL-2.0-or-later
"""A .present callback may set the text shown for a knob's value. It is drawn while the knob turns,
and the number is drawn when the callback sets none.

Run as tests/sdk_alpha_harness.py says.
"""
import unittest

from sdk_alpha_harness import sdk_machine, sdk_tests

# TYPE shows SAW or PULS; LEVEL sets no value text.
VALUES = sdk_machine("Values", """
    #include "elekloader/sdk_alpha_machine.h"

    enum { TYPE, LEVEL };
    static const struct elek_knob knobs[] = { [TYPE] = { "TYPE", .max = 1 }, [LEVEL] = { "LEVEL", .max = 127 } };
    static void present(const struct elek_ui *ui)
    {
        ui->view[TYPE].value = ui->knob[TYPE] ? "PULS" : "SAW";
    }
    static const struct elek_machine values = {
        .id = @ID@, .name = "Values", .short_name = "VALS", ELEK_KNOBS(knobs),
        .pages = { { "MAIN", { [ELEK_A] = &knobs[TYPE], [ELEK_B] = &knobs[LEVEL] } } }, .present = present,
    };
    ELEK_REGISTER_MACHINE(values);
""")

# VALUES's layout with no .present: its TYPE shows the number.
NUMBERS = sdk_machine("Numbers", """
    #include "elekloader/sdk_alpha_machine.h"

    static const struct elek_knob knobs[] = { { "TYPE", .max = 1 }, { "LEVEL", .max = 127 } };
    static const struct elek_machine numbers = {
        .id = @ID@, .name = "Numbers", .short_name = "NUMS", ELEK_KNOBS(knobs),
        .pages = { { "MAIN", { [ELEK_A] = &knobs[0], [ELEK_B] = &knobs[1] } } },
    };
    ELEK_REGISTER_MACHINE(numbers);
""")


@sdk_tests
class Values:
    def turned(self, machine, track, label, value):
        """The screen where `label`'s value shows, just after turning it to `value`."""
        knob, = self.tracks[track].use(machine).knobs(label)
        knob.set(value)
        return knob.label_pixels

    def test_the_value_text_is_drawn_while_the_knob_turns(self):
        number = self.turned(NUMBERS, 1, "TYPE", 1)
        pulse = self.turned(VALUES, 0, "TYPE", 1)
        saw = self.turned(VALUES, 0, "TYPE", 0)
        self.assertTrue(any(pulse) and any(saw))
        self.assertNotEqual(pulse, number)
        self.assertNotEqual(pulse, saw)

    def test_a_knob_without_value_text_shows_its_number(self):
        self.assertEqual(self.turned(VALUES, 0, "LEVEL", 64), self.turned(NUMBERS, 1, "LEVEL", 64))


if __name__ == '__main__':
    unittest.main()
