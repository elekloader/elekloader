# SPDX-License-Identifier: GPL-2.0-or-later
"""The MODS page's build list label (mods/drive-loader-ar2/clip.inc), built for the computer with a
made-up proportional font: a mod's path below /mods is clipped to the room left of the state
column, keeping its tail (the file's name) behind the cut mark, so it never runs into KEEP / ADD.

    python tests/test_drive_page.py

Needs a C compiler (cc)."""
import os
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
LABEL = 48                                         # page.c's label buffer


def fake_width(s):
    return sum(2 if c in '. ' else 6 if c in 'MW~mw' else 4 for c in s)


class Clip(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.exe = os.path.join(cls.work.name, 'drive_page_clip_host')
        subprocess.run(['cc', '-O1', '-Wall', '-o', cls.exe, os.path.join(HERE, 'drive_page_clip_host.c')],
                       check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def clip(self, *cases):
        r = subprocess.run([self.exe], input=''.join('%d %s\n' % c for c in cases), capture_output=True,
                           text=True, check=True)
        return [tuple(l.split(' ', 1)) for l in r.stdout.splitlines()]

    def test_a_short_path_is_left_as_it_is(self):
        self.assertEqual(self.clip((95, 'drive-hello-0.1.elemod')), [(str(fake_width('drive-hello-0.1.elemod')), 'drive-hello-0.1.elemod')])

    def test_long_paths_keep_their_tail_and_fit(self):
        paths = ['~T NAV/~T fnm-01-0.1.elemod', '~T NAV/~T A/~T fnm-03-0.1.elemod',
                 '~T NAV/~T A/~T B/~T fnm-04-0.1.elemod', 'x' * 127]
        for room in (95, 80, 40, 12):
            for (w, label), p in zip(self.clip(*[(room, p) for p in paths]), paths):
                self.assertLessEqual(int(w), room, (room, p, label))
                if fake_width(p) <= room and len(p) < LABEL:
                    self.assertEqual(label, p)
                    continue
                self.assertTrue(label.startswith('<'), label)
                self.assertTrue(p.endswith(label[1:]), (p, label))
                # the longest tail that fits: one more character would not
                more = '<' + p[len(p) - len(label):]
                self.assertTrue(fake_width(more) > room or len(more) > LABEL - 1, (room, p, label))

    def test_no_room_gives_the_mark_only(self):
        self.assertEqual(self.clip((3, '~T NAV/~T fnm-01-0.1.elemod')), [('4', '<')])


FCR_TREE = ['~T FCR/', '~T FCR/~T fnm-01-0.1.elemod', '~T FCR/~T SRC/', '~T FCR/~T SRC/~T fnm-02-0.1.elemod',
            '~T FCR/~T SRC/~T SUB/', '~T FCR/~T SRC/~T SUB/~T fnm-03-0.1.elemod', '~T FCR/~T MKA/',
            '~T FCR/~T MKA/~T MKB/']
FCR_ACTIVE = ['~T FCR/~T SRC/~T fnm-02-0.1.elemod', '~T FCR/~T SRC/~T SUB/~T fnm-03-0.1.elemod',
              '~T FCR/~T fnm-01-0.1.elemod']


class BuildRows(unittest.TestCase):
    """The build list (page.c build_rows) after the page's folder changes and the route's moves:
    tests/drive_build_rows_host.c runs page.c's read_folder, follow_route and labels on a made-up
    +Drive. The FCR emulator run (2026-11-09) drew one mod three times in VIEW BUILD after the page
    had changed folder."""

    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.exe = os.path.join(cls.work.name, 'drive_build_rows_host')
        subprocess.run(['cc', '-O1', '-w', '-no-pie', '-o', cls.exe, os.path.join(HERE, 'drive_build_rows_host.c'),
                        '-Wl,--unresolved-symbols=ignore-all'], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def run_page(self, requests, tree=FCR_TREE, active=FCR_ACTIVE):
        r = subprocess.run([self.exe], capture_output=True, text=True, check=True,
                           input='\n'.join(tree + ['--'] + active + ['--'] + requests) + '\n')
        return [l[len('BUILD '):].split('|') for l in r.stdout.splitlines() if l.startswith('BUILD')]

    def test_a_folder_opened_keeps_the_build_rows(self):
        self.assertEqual(self.run_page(['build', 'open ~T FCR', 'build', 'open ~T FCR/~T SRC', 'build', 'open ~T FCR',
                                        'build']), [FCR_ACTIVE] * 4)

    def test_the_fcr_run_moves_then_the_folders_up(self):
        moved = ['~T FCR/~T REN/~T fnm-02-0.1.elemod', '~T FCR/~T MKA/~T SUB/~T fnm-03-0.1.elemod',
                 '~T FCR/~T MKA/~T MKB/~T fnm-01r-0.1.elemod']
        got = self.run_page([
            'open ~T FCR',
            'mv ~T FCR/~T SRC|~T FCR/~T REN',
            'mv ~T FCR/~T fnm-01-0.1.elemod|~T FCR/~T fnm-01r-0.1.elemod',
            'open ~T FCR/~T REN', 'open ~T FCR/~T REN/~T SUB',
            'mv ~T FCR/~T REN/~T SUB|~T FCR/~T MKA/~T SUB',
            'mv ~T FCR/~T fnm-01r-0.1.elemod|~T FCR/~T MKA/~T MKB/~T fnm-01r-0.1.elemod',
            'build',
            'open ~T FCR/~T MKA', 'open ~T FCR/~T MKA/~T MKB', 'open ~T FCR/~T MKA', 'open ~T FCR',   # as FCR B3
            'build'])
        self.assertEqual(got, [moved, moved])


if __name__ == '__main__':
    unittest.main()
