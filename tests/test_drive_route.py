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
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from elekloader import datarpc                                   # noqa: E402

SRC = os.path.join(os.path.dirname(HERE), 'mods', 'drive-loader-ar2')
DEPTH = 3                                  # fs.h DL_DEPTH: the route's patterns go to /mods/*/*/*/*


# A /mods with a folder at every level to one past DL_DEPTH, each holding a file
TREE = ['~T NAV/', '~T NAV/~T a-0.1.elemod', '~T NAV/~T A/', '~T NAV/~T A/~T b-0.1.elemod',
        '~T NAV/~T A/~T B/', '~T NAV/~T A/~T B/~T c-0.1.elemod',
        '~T NAV/~T A/~T B/~T C/', '~T NAV/~T A/~T B/~T C/~T d-0.1.elemod']
LEVELS = ['', '~T NAV', '~T NAV/~T A', '~T NAV/~T A/~T B']      # the open folder at 0..DEPTH


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


    def test_an_upload_to_dot_dir_makes_its_folder(self):
        # `elekloader mods mkdir`, the web view's New folder: an empty upload to FOLDER/.dir
        r = subprocess.run([self.exe], capture_output=True, text=True, check=True,
                           input='~T NAV/.dir\n~T NAV/~T A/~T B/.dir\n')
        self.assertEqual(r.stdout.splitlines(), ['ok 1 1 ~T NAV/.dir mkdir ~T NAV',
                                                 'ok 3 1 ~T NAV/~T A/~T B/.dir mkdir ~T NAV/~T A/~T B'])
        # a fourth-level folder has no pattern (/mods/*/*/*/*/* is not registered)
        self.assertEqual(self.entry('~T NAV/~T A/~T B/~T C/.dir'), [None])
        # .dir alone (no folder), inside a folder name, after a dot folder, or a .dir folder
        for bad in ('.dir', '.hidden/.dir', '~T NAV/.dir/x.elemod', '~T NAV/.dir/.dir', '~T NAV/x.dir'):
            got = self.entry(bad)[0]
            self.assertTrue(got is None or not got[1], (bad, got))
        self.assertEqual(self.entry('~T NAV/x.dir'), [(1, False, '~T NAV/x.dir')])

    def test_mkdir_checks_the_name_then_reads_the_parent(self):
        calls = []

        class Client:
            def put(self, path, data):
                calls.append(('put', path, data))

            def ls(self, path):
                calls.append(('ls', path))
                return [('~T B', 1, 0)] if path == '/mods/~T A' else []
        mods = datarpc.Mods(Client())
        head = {'family': 2, 'device': 5, 'release': 0x30313736}
        mods.mkdir('~T A/~T B', head)
        self.assertEqual(calls[0][:2], ('put', '/mods/~T A/~T B/.dir'))
        self.assertEqual(datarpc.container.parse(calls[0][2])[1], b'')
        self.assertEqual(calls[1], ('ls', '/mods/~T A'))
        with self.assertRaises(datarpc.ModError):       # not in the parent's listing afterwards
            mods.mkdir('~T C', head)
        calls.clear()
        for bad in ('', '.x', '~T A/.x', 'a//b', 'a/b/c/d', '../x'):
            with self.assertRaises(datarpc.ModError, msg=bad):
                mods.mkdir(bad, head)
        self.assertEqual(calls, [])                     # refused before anything is sent

    def test_a_folder_with_entries_is_not_cleared(self):
        # FN02 (53d8c12) in the emulator: `mods rm` of a folder holding a file printed DELETED and
        # the folder stayed (stock's rmdir refused after the route had said yes)
        r = subprocess.run([self.exe], input='rmdir 0\nrmdir 1\nrmdir 3\n', capture_output=True,
                           text=True, check=True)
        self.assertEqual(r.stdout.splitlines(), ['cleared', 'Folder not empty', 'Folder not empty'])


class PastTheThirdLevel(unittest.TestCase):
    """A folder inside a third-level folder (made by USB or a host tool): the route's listing and
    the MODS page hide it, the computer's walk doesn't enter it, and nothing reaches its files.
    tests/drive_depth_host.c runs page.c's read_folder and route.c's walk on a made-up +Drive."""

    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.exe = {}
        for m in ('PAGE', 'ROUTE'):
            cls.exe[m] = os.path.join(cls.work.name, 'drive_depth_' + m.lower())
            subprocess.run(['cc', '-O1', '-w', '-no-pie', '-DDEPTH_' + m, '-o', cls.exe[m],
                            os.path.join(HERE, 'drive_depth_host.c'),
                            '-Wl,--unresolved-symbols=ignore-all'], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def ask(self, mode, queries, tree=TREE):
        r = subprocess.run([self.exe[mode]], capture_output=True, text=True, check=True,
                           input='\n'.join(tree + ['--'] + ['%d %s' % q for q in queries]) + '\n')
        return r.stdout.splitlines()

    def test_the_page_shows_no_folder_in_a_third_level_folder(self):
        rows = self.ask('PAGE', list(enumerate(LEVELS)))
        self.assertEqual(rows, ['~T NAV/',
                                '../|~T A/|~T NAV/~T a-0.1.elemod',
                                '../|~T B/|~T NAV/~T A/~T b-0.1.elemod',
                                '../|~T NAV/~T A/~T B/~T c-0.1.elemod'])     # no "~T C/" row
        self.assertNotIn('~T d-0.1.elemod', ''.join(rows))

    def test_the_route_lists_no_folder_in_a_third_level_folder(self):
        # each level: its folder and its file, the third its file only
        self.assertEqual(self.ask('ROUTE', list(enumerate(LEVELS))), ['1', '2', '2', '1'])

    def test_a_third_level_folder_of_folders_only_lists_empty(self):
        tree = [t for t in TREE if not t.endswith('~T c-0.1.elemod')]
        self.assertEqual(self.ask('ROUTE', [(3, LEVELS[3])], tree), ['0'])
        self.assertEqual(self.ask('PAGE', [(3, LEVELS[3])], tree), ['../'])

    def test_no_route_pattern_lists_or_reaches_the_fourth_level(self):
        with open(os.path.join(SRC, 'route.c'), encoding='utf-8') as fh:
            route = fh.read()
        self.assertIn('"/mods/*/*/*"};', route)          # the deepest listing: a third-level folder
        self.assertIn('"/mods/*/*/*/*"};', route)        # the deepest entry: a file in one
        self.assertNotIn('"/mods/*/*/*/*/*"', route)

    def test_the_computer_walk_does_not_enter_the_fourth_level(self):
        class Fake:                    # a route that did list the fourth-level folder
            def ls(self, path):
                rel = path[len('/mods/'):] if path != '/mods' else ''
                out = []
                for t in TREE:
                    d = t.rstrip('/')
                    parent = d.rpartition('/')[0]
                    if parent == rel:
                        out.append((d.rpartition('/')[2], 1 if t.endswith('/') else 2, 100))
                return out
        got = [p for p, _ in datarpc.Mods(Fake()).walk()]
        self.assertEqual(got, ['~T NAV/~T a-0.1.elemod', '~T NAV/~T A/~T b-0.1.elemod',
                               '~T NAV/~T A/~T B/~T c-0.1.elemod'])

    def test_put_into_a_fourth_level_folder_is_refused_before_sending(self):
        sent = []

        class Fake(datarpc.Mods):
            def build(self):
                return None, None

            def put(self, name, raw, head):
                sent.append(name)
        a = type('A', (), {'op': 'put', 'files': [], 'into': '~T NAV/~T A/~T B/~T C'})()
        with mock.patch.object(datarpc.drive, '_files', return_value=[('~T d-0.1.elemod', b'')]), \
                self.assertRaises(datarpc.ModError):
            datarpc.run(a, Fake(None), log=lambda *_: None)
        self.assertEqual(sent, [])


if __name__ == '__main__':
    unittest.main()
