# SPDX-License-Identifier: GPL-2.0-or-later
"""The Drive Loader's /mods route names (mods/drive-loader-ar2/names.inc), built for the computer:
what each upload asks the route for. `elekloader mods put` reads /mods/.build first (the build
check), then writes the file, so both must pass the route's name check; the router hands the route
one capture a segment, and the route registers patterns to DL_DEPTH folders, so a deeper path
reaches no entry at all.

    python tests/test_drive_route.py

Needs a C compiler (cc)."""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from elekloader import datarpc                                   # noqa: E402

SRC = os.path.join(os.path.dirname(HERE), 'mods', 'drive-loader-ar2')
DEPTH = 3                                  # fs.h DL_DEPTH: the route's patterns go to /mods/*/*/*/*


class Route(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.exe = os.path.join(cls.work.name, 'drive_route_names_host')
        subprocess.run(['cc', '-O1', '-Wall', '-o', cls.exe, os.path.join(HERE, 'drive_route_names_host.c')],
                       check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def entry(self, *paths):
        """-> per path below /mods: None (no route entry: deeper than the patterns, or refused by
        the name check) or (folders, writable, joined)."""
        routed = [p for p in paths if p.count('/') <= DEPTH]
        r = subprocess.run([self.exe], input=''.join(p + '\n' for p in routed), capture_output=True,
                           text=True, check=True)
        got = iter(r.stdout.splitlines())
        out = []
        for p in paths:
            if p not in routed:
                out.append(None)
                continue
            w = next(got).split(' ', 3)
            out.append(None if w[0] == 'refused' else (int(w[1]), w[2] == '1', w[3]))
        return out

    def put(self, name):
        """What `mods put` asks for: the .build read, then the file. -> True when both pass."""
        build, f = self.entry('.build', name)
        return build is not None and f is not None and f[1]

    def test_the_build_and_status_reads_pass(self):
        # every put reads .build first: e37adef refused it ("Invalid name"), so no put went through
        self.assertEqual(self.entry('.build', '.status'), [(0, False, '.build'), (0, False, '.status')])

    def test_puts_flat_and_into_folders(self):
        for name in ('drive-hello-0.1.elemod', '~T a-0.1.elemod', '~T NAV/~T a-0.1.elemod',
                     '~T NAV/~T A/~T b-0.1.elemod', '~T NAV/~T A/~T B/~T c-0.1.elemod'):
            self.assertTrue(self.put(name), name)
        self.assertEqual(self.entry('~T NAV/~T A/~T B/~T c-0.1.elemod'),
                         [(3, True, '~T NAV/~T A/~T B/~T c-0.1.elemod')])

    def test_four_deep_is_refused(self):
        name = '~T NAV/~T A/~T B/~T C/~T d-0.1.elemod'
        self.assertFalse(self.put(name))                   # no route pattern that deep
        mods = datarpc.Mods(None)
        with self.assertRaises(datarpc.ModError):          # and the CLI refuses it before sending
            mods.path(name)

    def test_names_the_route_refuses(self):
        for bad in ('.armed', '.hidden', '~T NAV/.build', 'a//b.elemod', '', '~T NAV/', '../x.elemod',
                    'x' * 64 + '.elemod'):
            self.assertEqual(self.entry(bad), [None], bad)
        # folders and non-.elemod names pass the name check but are not writable
        self.assertEqual(self.entry('~T NAV'), [(0, False, '~T NAV')])
        self.assertEqual(self.entry('active'), [(0, True, 'active')])


    def test_a_folder_with_entries_is_not_cleared(self):
        # FN02 (53d8c12) in the emulator: `mods rm` of a folder holding a file printed DELETED and
        # the folder stayed (stock's rmdir refused after the route had said yes)
        r = subprocess.run([self.exe], input='rmdir 0\nrmdir 1\nrmdir 3\n', capture_output=True,
                           text=True, check=True)
        self.assertEqual(r.stdout.splitlines(), ['cleared', 'Folder not empty', 'Folder not empty'])


if __name__ == '__main__':
    unittest.main()
