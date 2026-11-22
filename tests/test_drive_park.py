# SPDX-License-Identifier: GPL-2.0-or-later
"""Parked data on the Rytm MODS page: mods/drive-loader-ar2/park.c, built for the computer by
tests/drive_park_host.c, reads /mods/.store in the Rytm formats 1 and 4 (format 4: format 1 and the
+Drive mods knob runs, elekloader/drive_store.py FORMAT_ONE_KNOBS) as drive_store.decode does, and
FORGET DATA rewrites it byte for byte as drive_store.forget then encode, the knob runs kept. A store in
the A4 formats 2 and 3 reads as a bad file.

    python tests/test_drive_park.py

Needs a C compiler (cc)."""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
from elekloader import drive_store as ds  # noqa: E402
from elekloader import store  # noqa: E402
from test_drive_a4_park import state, parked_mods  # noqa: E402

MOD = os.path.join(os.path.dirname(HERE), "mods", "drive-loader-ar2")
SDK = os.path.join(os.path.dirname(HERE), "elekloader", "sdk", "include")


def knobbed(st):
    return dict(st, knobs=[{"mod": "ka", "first": 0x4000, "count": 4}],
                freed_knobs=[{"mod": "a-gone-mod", "first": 0x4004, "count": 2}], next_knob=0x4006)


class Park(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.exe = os.path.join(cls.work.name, "drive_park_host")
        subprocess.run(["cc", "-O1", "-Wall", "-I", SDK, "-I", MOD, "-include", os.path.join(MOD, "dl.h"),
                        "-o", cls.exe, os.path.join(HERE, "drive_park_host.c")], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def ask(self, raw, *requests):
        r = subprocess.run([self.exe], capture_output=True, text=True, check=True,
                           input="\n".join(["store " + raw.hex()] + list(requests)) + "\n")
        return r.stdout.splitlines()

    def check_store(self, st, fmt):
        raw = ds.encode(st)
        self.assertEqual(raw[4:6], bytes([0, fmt]))
        mods = parked_mods(st)
        self.assertEqual(self.ask(raw, "count")[0], "COUNT %d" % len(mods))
        asks = [[m] for m in mods] + [mods + ["nobody"]]
        got = self.ask(raw, *["forget " + "|".join(a) for a in asks])
        for a, g in zip(asks, got):
            want = st
            for m in a:
                want, _ch = ds.forget(want, m)
            self.assertEqual(g, "FORGET " + ds.encode(want).hex(), a)
            self.assertIsNotNone(ds.decode(bytes.fromhex(g[7:])))
        return got[-1]

    def test_a_format_1_store(self):
        self.check_store(state(kinded=False), ds.FORMAT)

    def test_a_format_4_store_keeps_its_knob_runs(self):
        """The DPSH bug (2026-11-18): a store with knob ids (format 4) read as a bad file, so the MODS page
        showed no PARK row and FORGET DATA could not be reached."""
        st = knobbed(state(kinded=False))
        g = self.check_store(st, ds.FORMAT_ONE_KNOBS)
        self.assertEqual(ds.decode(bytes.fromhex(g[7:]))["knobs"], st["knobs"])
        self.assertEqual(self.ask(bytes.fromhex(g[7:]), "count"), ["COUNT 0"])

    def test_store_has_any_live_or_parked_mod(self):
        for st in (state(kinded=False), knobbed(state(kinded=False))):
            raw = ds.encode(st)
            out = self.ask(raw, "has ~T live", "has ~T ae-cover", "has ~T rand", "has ~T cc-mod", "has ~T other",
                           "has ~T liv", "hasp ~T liv", "hasp ~T x")
            self.assertEqual(out, ["HAS 1", "HAS 1", "HAS 1", "HAS 1", "HAS 0", "HAS 0", "HAS 1", "HAS 0"])
            self.assertEqual(self.ask(raw[:-1], "has ~T live"), ["HAS 0"])   # a bad file keeps nothing

    def test_the_a4_formats_are_refused(self):
        for st in (state(), knobbed(state())):
            self.assertEqual(self.ask(ds.encode(st), "count", "forget ~T rand"), ["COUNT -1", "FORGET -1"])


if __name__ == "__main__":
    unittest.main()
