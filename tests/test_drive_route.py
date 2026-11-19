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
FLOOR = 0x10000                            # fs.h DL_STAGING_SIZE: the stage's floor
TOO_LARGE = 'Too large for the free room (see /mods/.status, stage)'   # route.c dl_pre


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

    def test_the_store_is_a_name_only_at_the_top_and_never_writable(self):
        # /mods/.store (+Drive storage's places): a build reads it (patch --drive-store); writable() keeps it
        # read only, and dl_entry gives it P_READ alone
        self.assertEqual(self.entry('.store'), [(0, False, '.store')])
        self.assertEqual(self.entry('~T NAV/.store'), [None])

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

    def test_a_leading_space_is_refused(self):
        # decided 2026-11-09: /mods/active's reader trims a line's leading blanks (route.c), so a
        # top-level " x" would save and not load; every segment is refused alike, here and in the CLI
        for bad in (' x.elemod', ' Jazz Kit/x.elemod', '~T NAV/ A/x.elemod', ' Jazz Kit'):
            self.assertEqual(self.entry(bad), [None], bad)
        self.assertTrue(self.put('Jazz Kit /x .elemod'))   # a space elsewhere is fine
        mods = datarpc.Mods(None)
        for call in (lambda: mods.path(' x.elemod'), lambda: mods.path('a/ b/x.elemod'),
                     lambda: mods.folder_path(' Jazz Kit')):
            with self.assertRaises(datarpc.ModError):
                call()


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


class Moves(unittest.TestCase):
    """Rename and move (an upload of "FROM\nTO" to /mods/.mv) and a new folder, on a made-up ekFS
    with stock 1.74's rename, mkdir and rmdir (tests/drive_move_host.c runs route.c's move and
    make_folder): a file moves to any folder; a folder moves within its parent by stock's rename
    and to another parent by a new folder its contents move into; /mods/active follows."""

    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.exe = os.path.join(cls.work.name, 'drive_move_host')
        subprocess.run(['cc', '-O1', '-w', '-no-pie', '-o', cls.exe, os.path.join(HERE, 'drive_move_host.c'),
                        '-Wl,--unresolved-symbols=ignore-all'], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    ACTIVE = 'active=~T A/x.elemod 1234abcd|# a note|~T AB/y.elemod 00000001|~T A/~T B/z.elemod 00000002|'
    TREE = ['~T A/x.elemod', '~T A/~T B/z.elemod', '~T AB/y.elemod', '~T C/', ACTIVE]

    def run_(self, *requests, tree=None):
        """-> [(answer, tree, active)] per request."""
        r = subprocess.run([self.exe], capture_output=True, text=True, check=True,
                           input='\n'.join((self.TREE if tree is None else tree) + ['--'] + list(requests)) + '\n')
        lines = r.stdout.splitlines()
        return [(lines[i], lines[i + 1][5:].split(' ; '), lines[i + 2][7:]) for i in range(0, len(lines), 3)]

    def test_a_file_moves_to_another_folder_and_active_follows(self):
        (why, tree, active), = self.run_('mv ~T A/x.elemod|~T C/x.elemod')
        self.assertEqual(why, 'ok')
        self.assertIn('~T C/x.elemod', tree)
        self.assertNotIn('~T A/x.elemod', tree)
        self.assertEqual(active, '~T C/x.elemod 1234abcd|# a note|~T AB/y.elemod 00000001|~T A/~T B/z.elemod 00000002|')

    def test_a_file_larger_than_staging_resolves_for_clear_and_listing(self):
        # dl_entry refused a file over DL_STAGING_SIZE for every use, Clear included, so one left by
        # a build with a larger buffer couldn't be deleted over Data. It resolves now without P_READ
        # (the read is served from the stage); an upload over it checks its own payload (dl_pre, dl_hdr).
        big = FLOOR + 1                   # one past the stage, here the floor
        tree = self.TREE + ['~T A/big.elemod=@%d' % big, '~T A/fit.elemod=@%d' % FLOOR,
                            '~T A/notes.txt=@%d' % big]
        got = [a for a, _, _ in self.run_('entry ~T A/big.elemod', 'entry ~T A/fit.elemod',
                                          'entry ~T A/notes.txt', 'entry ~T A/new.elemod',
                                          'entry ~T A/new.txt', tree=tree)]
        self.assertEqual(got, ['ok %d write clear' % big, 'ok %d read write clear' % FLOOR,
                               'ok %d clear' % big, 'ok 0 read write clear', 'Invalid name'])

    def test_a_read_follows_the_live_stage(self):
        # with the stage in the area's free tail (dl_stage_tail), a file the floor can't hold reads
        big = FLOOR * 3
        tree = self.TREE + ['~T A/big.elemod=@%d' % big]
        got = [a for a, _, _ in self.run_('stage %d' % (big - 1), 'entry ~T A/big.elemod',
                                          'stage %d' % big, 'entry ~T A/big.elemod', tree=tree)]
        self.assertEqual(got, ['ok', 'ok %d write clear' % big, 'ok', 'ok %d read write clear' % big])

    def test_an_upload_over_the_stage_is_refused_before_its_first_chunk(self):
        # stock 1.74 writes the first chunk to the entry's address before the +0x6c check
        # (FUN_4008c1c6) and has no size cap of its own: +0x1c refuses first, +0x6c is the second line
        n = 200000
        got = [a for a, _, _ in self.run_('stage %d' % n, 'pre %d' % n, 'pre %d' % (n + 1),
                                          'hdr %d' % n, 'hdr %d' % (n + 1))]
        self.assertEqual(got[1:], ['ok GUARD ok', TOO_LARGE + ' GUARD ok', 'ok GUARD ok',
                                   'Invalid payload size GUARD ok'])
        # at the floor (no free tail), the floor is the limit
        got = [a for a, _, _ in self.run_('pre %d' % FLOOR, 'pre %d' % (FLOOR + 1))]
        self.assertEqual(got, ['ok', TOO_LARGE])

    def test_reads_and_uploads_stay_inside_the_stage(self):
        # the stage sits next to the mods' live code: every byte the route writes is inside it
        n = 300001
        tree = self.TREE + ['~T A/full.elemod=@%d' % n, '~T A/small.elemod=@%d' % 1000]
        got = [a for a, _, _ in self.run_('stage %d' % n, 'put ~T A/full.elemod %d' % n, 'read ~T A/full.elemod',
                                          'put ~T A/small.elemod 1000', 'read ~T A/small.elemod',
                                          'put ~T A/over.elemod %d' % (n + 1), tree=tree)]
        self.assertEqual(got[1:], ['saved ~T A/full.elemod %d GUARD ok' % n, 'read %d GUARD ok' % n,
                                   'saved ~T A/small.elemod 1000 GUARD ok', 'read 1000 GUARD ok',
                                   'upload dropped: ~T A/over.elemod -1 GUARD ok'])

    def test_a_clear_of_a_file_over_the_stage_zeroes_nothing_outside_it(self):
        # DTS1 in the emulator (2026-11-11): a Clear of a /mods file over the stage froze the unit.
        # Stock's Clear (0x4002d0b4) zeroes [addr, addr + size) of a RAM entry before the +0x4c call,
        # so the entry's size is 0 for such a file (entry_size); the +0x4c call still deletes it.
        n = 100000
        tree = self.TREE + ['~T A/big.elemod=@%d' % (n + 1), '~T A/fit.elemod=@%d' % n]
        got = self.run_('stage %d' % n, 'clear ~T A/big.elemod', 'clear ~T A/fit.elemod', tree=tree)
        self.assertEqual([a for a, _, _ in got[1:]], ['0 deleted ~T A/big.elemod GUARD ok',
                                                      '%d deleted ~T A/fit.elemod GUARD ok' % n])
        self.assertNotIn('~T A/big.elemod', got[1][1])
        self.assertNotIn('~T A/fit.elemod', got[2][1])

    def test_a_read_of_a_file_over_the_stage_is_still_refused(self):
        # its entry's size is 0 (entry_size), so dl_pre refuses it from the closure's size
        n = 100000
        tree = self.TREE + ['~T A/big.elemod=@%d' % (n + 1), '~T A/fit.elemod=@%d' % n]
        got = [a for a, _, _ in self.run_('stage %d' % n, 'readpre ~T A/big.elemod',
                                          'readpre ~T A/fit.elemod', tree=tree)]
        self.assertEqual(got[1:], [TOO_LARGE + ' GUARD ok', 'ok GUARD ok'])

    def test_the_status_names_the_stage(self):
        got = [a for a, _, _ in self.run_('status', 'stage 4000000', 'status')]
        self.assertRegex(got[0], r'^stage: %d bytes at 0x[0-9a-f]{8}, floor$' % FLOOR)
        self.assertRegex(got[2], r'^stage: 4000000 bytes at 0x[0-9a-f]{8}, free tail GUARD ok$')

    def test_a_file_is_renamed(self):
        (why, tree, active), = self.run_('mv ~T A/x.elemod|~T A/w.elemod')
        self.assertEqual(why, 'ok')
        self.assertIn('~T A/w.elemod', tree)
        self.assertTrue(active.startswith('~T A/w.elemod 1234abcd|'))

    def test_a_folder_is_renamed_with_every_ticked_mod_below_it(self):
        (why, tree, active), = self.run_('mv ~T A|~T D')
        self.assertEqual(why, 'ok')
        self.assertEqual([t for t in tree if t.startswith('~T D')], ['~T D/', '~T D/x.elemod', '~T D/~T B/', '~T D/~T B/z.elemod'])
        self.assertFalse(any(t.startswith('~T A/') for t in tree))
        # ~T AB is not below ~T A: its line stays
        self.assertEqual(active, '~T D/x.elemod 1234abcd|# a note|~T AB/y.elemod 00000001|~T D/~T B/z.elemod 00000002|')

    def test_a_folder_moves_into_another_folder(self):
        # stock's rename keeps a folder in its parent: the route makes it there and moves what is in it
        (why, tree, active), = self.run_('mv ~T A|~T C/~T A')
        self.assertEqual(why, 'ok')
        self.assertEqual([t for t in tree if t.startswith('~T C')],
                         ['~T C/', '~T C/~T A/', '~T C/~T A/x.elemod', '~T C/~T A/~T B/', '~T C/~T A/~T B/z.elemod'])
        self.assertNotIn('~T A/', tree)
        self.assertIn('~T C/~T A/~T B/z.elemod 00000002', active)
        # and back up to /mods
        (why, tree, _a), = self.run_('mv ~T C/~T A|~T A', tree=[t for t in tree if t != 'active'] + [
            'active=x 0|'])
        self.assertEqual(why, 'ok')
        self.assertIn('~T A/~T B/z.elemod', tree)

    def test_a_new_folder_and_its_parents(self):
        (why, tree, _a), = self.run_('mkdir ~T N/~T O/~T P')
        self.assertEqual(why, 'ok')
        self.assertTrue({'~T N/', '~T N/~T O/', '~T N/~T O/~T P/'} <= set(tree))
        (why, _t, _a), = self.run_('mkdir ~T A')               # there already: kept
        self.assertEqual(why, 'ok')
        (why, _t, _a), = self.run_('mkdir ~T A/x.elemod')      # a file of that name
        self.assertEqual(why, 'failed')

    def test_a_folder_on_a_fresh_card_makes_mods_first(self):
        # the DLX2 emulator run (2026-11-09): a mkdir before any upload, with no /mods yet, failed
        (why, tree, _a), = self.run_('mkdir ~T N/~T O', tree=['!nomods'])
        self.assertEqual(why, 'ok')
        self.assertTrue({'~T N/', '~T N/~T O/'} <= set(tree), tree)

    def test_refusals_leave_everything_as_it_was(self):
        cases = {
            'mv ~T A/x.elemod|~T AB/y.elemod': 'Already there',       # no overwrite
            'mv ~T A|~T AB': 'Already there',
            'mv ~T A|~T A/~T B/~T A': 'Into itself',
            'mv ~T A|~T C/~T D/~T A': 'Too deep',                       # its ~T B would be 4 deep
            'mv ~T A/x.elemod|~T C/x.txt': 'Invalid name',              # a mod stays a .elemod
            'mv active|~T C/active.elemod': 'Invalid name',             # the route's own files never move
            'mv .build|x.elemod': 'Invalid name',
            'mv ~T A/x.elemod|.hidden/x.elemod': 'Invalid name',
            'mv ~T A/x.elemod|~T Q/x.elemod': 'No such folder',         # no folder is made for a move
            'mv ~T Z|~T Y': 'Not there',
            'mv ~T A|~T A': 'Same name',
            'mv ~T A|' + 'x' * 64: 'Invalid name',                     # a name past DL_SEG_MAX
            'mv ~T A|' + '/'.join(['y' * 60, 'z' * 60]): 'Name too long',   # ~T B/z.elemod past 127
            'mv ~T A/x.elemod': 'Invalid name',                         # no TO
        }
        for req, want in cases.items():
            (why, tree, active), = self.run_(req)
            self.assertEqual(why, want, req)
            self.assertEqual(sorted(tree), sorted(self.run_('mkdir ~T C')[0][1]), req)   # unchanged
            self.assertEqual(active, self.ACTIVE[7:], req)

    def test_mods_move_checks_names_then_reads_the_listings(self):
        calls = []
        listing = {'/mods': [('~T A', 1, 0), ('~T C', 1, 0)], '/mods/~T A': [('x.elemod', 2, 9)],
                   '/mods/~T C': []}

        class Client:
            def put(self, path, data):
                calls.append(('put', path, datarpc.container.parse(data)[1]))
                listing['/mods/~T C'] = [('x.elemod', 2, 9)]
                listing['/mods/~T A'] = []

            def ls(self, path):
                calls.append(('ls', path))
                return listing[path]
        mods = datarpc.Mods(Client())
        head = {'family': 2, 'device': 5, 'release': 0x30313736}
        mods.move('~T A/x.elemod', '~T C/x.elemod', head)
        self.assertEqual(calls[1], ('put', '/mods/.mv', b'~T A/x.elemod\n~T C/x.elemod'))
        listing['/mods/~T C'] = [('x.elemod', 2, 9)]          # put() leaves it where it was:
        with self.assertRaises(datarpc.ModError):              # the listings don't show it moved
            mods.move('~T C/x.elemod', '~T A/y.elemod', head, folder=False)
        calls.clear()
        for frm, to, folder in (('~T A/x.elemod', '~T A/x.txt', False), ('active', 'a.elemod', False),
                                ('~T A', '~T A/~T B', True), ('~T A', 'a/b/c/d', True), ('~T A', '.x', True),
                                ('~T A', '~T A', True), ('a.elemod', 'a/b/c/d/a.elemod', False)):
            with self.assertRaises(datarpc.ModError, msg=(frm, to)):
                mods.move(frm, to, head, folder=folder)
        self.assertEqual(calls, [])                             # refused before anything is sent


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
