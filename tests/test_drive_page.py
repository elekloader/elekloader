# SPDX-License-Identifier: GPL-2.0-or-later
"""The MODS page's build list label (mods/drive-loader-ar2/clip.inc), built for the computer with a
made-up proportional font: a mod's path below /mods is clipped to the room left of the state
column, keeping its tail (the file's name) behind the cut mark, so it never runs into KEEP / ADD.

    python tests/test_drive_page.py

Needs a C compiler (cc)."""
import json
import os
import subprocess
import sys
import tempfile
import unittest
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SDK = os.path.join(os.path.dirname(HERE), 'elekloader', 'sdk', 'include')
sys.path.insert(0, os.path.dirname(HERE))
from elekloader import drive_store as ds   # noqa: E402
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
NAMES = ['~T fnm-02-0.1', '~T fnm-03-0.1', '~T fnm-01-0.1']   # no id known: the file names


class BuildRows(unittest.TestCase):
    """The build list (page.c build_rows) after the page's folder changes and the route's moves:
    tests/drive_build_rows_host.c runs page.c's read_folder, follow_route and labels on a made-up
    +Drive. The FCR emulator run (2026-11-09) drew one mod three times in VIEW BUILD after the page
    had changed folder."""

    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.exe = os.path.join(cls.work.name, 'drive_build_rows_host')
        subprocess.run(['cc', '-O1', '-w', '-no-pie', '-I', SDK, '-o', cls.exe, os.path.join(HERE, 'drive_build_rows_host.c'),
                        '-Wl,--unresolved-symbols=ignore-all'], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def run_page(self, requests, tree=FCR_TREE, active=FCR_ACTIVE):
        r = subprocess.run([self.exe], capture_output=True, text=True, check=True,
                           input='\n'.join(tree + ['--'] + active + ['--'] + requests) + '\n')
        return [l[len('BUILD '):].split('|') for l in r.stdout.splitlines() if l.startswith('BUILD')]

    def lines(self, requests, kind, tree=FCR_TREE, active=FCR_ACTIVE):
        r = subprocess.run([self.exe], capture_output=True, text=True, check=True,
                           input='\n'.join(tree + ['--'] + active + ['--'] + requests) + '\n')
        return [l[len(kind) + 1:] for l in r.stdout.splitlines() if l.startswith(kind + ' ')]

    def test_a_folder_opened_keeps_the_build_rows(self):
        self.assertEqual(self.run_page(['build', 'open ~T FCR', 'build', 'open ~T FCR/~T SRC', 'build', 'open ~T FCR',
                                        'build']), [NAMES] * 4)

    def test_the_fcr_run_moves_then_the_folders_up(self):
        moved = ['~T fnm-02-0.1', '~T fnm-03-0.1', '~T fnm-01r-0.1']
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
        info = self.lines(['open ~T FCR', 'mv ~T FCR/~T SRC|~T FCR/~T REN', 'info 0', 'info 1', 'info 2'], 'INFO')
        self.assertEqual(info, ['/mods/~T FCR/~T REN~~T fnm-02-0.1.elemod', '/mods/~T FCR/~T REN/~T SUB~~T fnm-03-0.1.elemod',
                                '/mods/~T FCR~~T fnm-01-0.1.elemod'])

    # ---- the labels (decided 2026-11-09) ----

    def test_build_rows_show_the_id_from_the_boot_or_the_save(self):
        got = self.run_page(['boot ~T FCR/~T SRC/~T fnm-02-0.1.elemod|fnm-02',
                             'boot ~T FCR/~T fnm-01-0.1.elemod|' + FCR_ACTIVE[2][:27],   # dlink's path prefix: no id
                             'id ~T FCR/~T SRC/~T SUB/~T fnm-03-0.1.elemod|fnm-03', 'build'])
        self.assertEqual(got, [['fnm-02', 'fnm-03', '~T fnm-01-0.1']])
        got = self.run_page(['boot ~T FCR/~T SRC/~T fnm-02-0.1.elemod|' + FCR_ACTIVE[0][:27], 'build'])
        self.assertEqual(got, [NAMES])   # a long path's prefix is no id either

    def test_files_in_use_show_their_update_and_lost_payload_and_refuse_delete(self):
        """drive-loader 0.12: a loaded mod whose payload the table keeps is in use. An upload over it
        waits as <name>.new (UPDATE in both lists, a line in INFO), one whose payload found the table
        full reads LOST, and DELETE refuses a file in use even after it left the saved build."""
        tree, active = ['a.elemod', 'b.elemod', 'c.elemod'], ['a.elemod', 'c.elemod']
        setup = ['boot a.elemod|a', 'boot b.elemod|b', 'boot c.elemod|c', 'inuse a.elemod', 'inuse b.elemod',
                 'update a.elemod', 'lost c.elemod', 'open']
        self.assertEqual(self.lines(setup + ['drive'], 'DRIVE', tree, active), ['a=UPDATE|b=|c='])
        self.assertEqual(self.lines(setup + ['bcols'], 'BCOLS', tree, active), ['a=UPDATE|c=LOST|b=DROP'])
        self.assertEqual(self.lines(setup + ['delwhy a.elemod', 'delwhy b.elemod', 'delwhy c.elemod'], 'DELWHY',
                                    tree, active), ['MOD IN USE', 'IN USE: RESTART FIRST', 'MOD IN USE'])
        self.assertEqual(self.lines(setup + ['binfo 0', 'binfo 1'], 'INFO', tree, active),
                         ['/mods~a.elemod~UPDATE AT NEXT RESTART', '/mods~c.elemod~PAYLOAD LOST: TABLE FULL'])
        self.assertEqual(self.lines(['boot b.elemod|b', 'open', 'delwhy b.elemod'], 'DELWHY', tree, active),
                         ['ok'])         # loaded, no payload kept: deletable as before

    def test_a_short_path_prefix_is_not_an_id(self):
        got = self.run_page(['boot x.elemod|x.elemod', 'build'], tree=['x.elemod'], active=['x.elemod'])
        self.assertEqual(got, [['x']])

    def test_apply_and_restart_shows_only_while_the_saved_set_differs(self):
        acts = ['SELECT ALL', 'DESELECT ALL', 'UNLOAD', 'INFO', 'FORGET DATA']
        tree, active = ['a.elemod', 'b.elemod'], ['a.elemod']
        self.assertEqual(self.lines(['acts'], 'ACTS', tree, active), ['|'.join(acts + ['APPLY & RESTART'])])
        self.assertEqual(self.lines(['boot a.elemod|a', 'acts'], 'ACTS', tree, active), ['|'.join(acts)])

    def test_yes_on_apply_and_restart_keeps_the_project_then_restarts_without_the_guard(self):
        tree, active = ['a.elemod'], ['a.elemod']
        self.assertEqual(self.lines(['restart 39'], 'KEY', tree, active), ['1'])     # keys taken
        self.assertEqual(self.lines(['restart 39'], 'WAITING', tree, active),
                         ['kept=0 locked=0 guard_off=0'])   # 2 s not yet up: nothing done
        self.assertEqual(self.lines(['restart 40'], 'RESTART', tree, active), ['kept=1 locked=1 guard_off=1'])

    def test_file_rows_show_the_file_name_never_the_id(self):
        got = self.lines(['id ~T FCR/~T fnm-01-0.1.elemod|fnm-01', 'open ~T FCR', 'drive'], 'DRIVE')
        rows = [r.split('=') for r in got[0].split('|')]
        self.assertEqual([r[0] for r in rows], ['..', '~T SRC', '~T MKA', '~T fnm-01-0.1'])

    def test_folders_show_d_and_the_count_of_ticks_below(self):
        got = self.lines(['open ~T FCR', 'drive', 'open ~T FCR/~T MKA', 'drive'], 'DRIVE')
        self.assertEqual(got, ['..=(D)|~T SRC=(2)|~T MKA=(D)|~T fnm-01-0.1=', '..=(D)|~T MKB=(D)'])

    def test_a_file_shows_its_size_once_checked(self):
        # T230 (2026-11-09): the folder's read opens no file, so the size waits for the check
        got = self.lines(['open ~T FCR', 'drive', 'checked ~T FCR/~T fnm-01-0.1.elemod', 'drive'], 'DRIVE')
        self.assertEqual([g.split('|')[-1] for g in got], ['~T fnm-01-0.1=', '~T fnm-01-0.1=0KB'])

    def test_the_check_count_goes_to_the_status_not_the_screen(self):
        # decided 2026-11-09: /mods/.status's "check: n/N FOLDER", nothing drawn
        got = self.lines(['check', 'open ~T FCR', 'check', 'checked ~T FCR/~T fnm-01-0.1.elemod', 'check',
                          'open ~T FCR/~T MKA', 'check', 'drive'], 'CHECK')
        self.assertEqual(got, ['0/0 ', '0/1 ~T FCR', '1/1 ~T FCR', '0/0 ~T FCR/~T MKA'])

    def test_info_clips_a_long_folder_from_the_left(self):
        tree = ['%s/' % ('a' * 40), '%s/%s.elemod' % ('a' * 40, 'n' * 50)]
        info = self.lines(['info 0'], 'INFO', tree=tree, active=[tree[1]])[0].split('~')
        self.assertEqual(info, ['<' + ('/mods/' + 'a' * 40)[-29:], 'n' * 30, 'n' * 20 + '.elemod'])


class Parked(unittest.TestCase):
    """Parked data on the MODS page (decided 2026-11-10, +Drive v2 item 6): park.c reads /mods/.store
    as elekloader/drive_store.py writes it and FORGET DATA rewrites it byte for byte as
    drive_store.forget then encode; the build list shows each parked mod after the build's rows,
    and the "!" box spells out storage-room and site refusals (dl.h DL_R_STORE, DL_R_CLASH)."""

    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.exe = os.path.join(cls.work.name, 'drive_build_rows_host')
        subprocess.run(['cc', '-O1', '-w', '-no-pie', '-I', SDK, '-o', cls.exe, os.path.join(HERE, 'drive_build_rows_host.c'),
                        '-Wl,--unresolved-symbols=ignore-all'], check=True)
        with open(os.path.join(HERE, '..', 'js', 'test', 'drivestore-vectors.json'), encoding='utf-8') as fh:
            runs = json.load(fh)['runs']
        cls.stores = []
        for run in runs:
            for res in run['results']:
                if isinstance(res, dict) and res.get('store') and res['store'] not in cls.stores:
                    cls.stores.append(res['store'])

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def lines(self, requests, kind, active=()):
        r = subprocess.run([self.exe], capture_output=True, text=True, check=True,
                           input='\n'.join(['--'] + list(active) + ['--'] + requests) + '\n')
        return [l[len(kind) + 1:] for l in r.stdout.splitlines() if l.startswith(kind + ' ')]

    @staticmethod
    def parked_mods(st):
        out = []
        for k in ('parked', 'freed_codes', 'freed_ccs'):
            for f in st[k]:
                if f['mod'] not in out:
                    out.append(f['mod'])
        return out

    def test_the_vectors_have_parked_data(self):
        self.assertTrue(any(ds.decode(bytes.fromhex(h))['parked'] for h in self.stores))

    def test_forget_matches_drive_store(self):
        for h in self.stores:
            st = ds.decode(bytes.fromhex(h))
            mods = self.parked_mods(st) + ['nobody']
            asks = [[m] for m in mods] + [mods]
            got = self.lines(['store ' + h] + ['forget ' + '|'.join(a) for a in asks], 'FORGET')
            for a, g in zip(asks, got):
                want = st
                for m in a:
                    want, _ch = ds.forget(want, m)
                self.assertEqual(g, ds.encode(want).hex(), (a, h))

    def test_parked_rows_and_their_bits(self):
        for h in self.stores:
            st = ds.decode(bytes.fromhex(h))
            mods = self.parked_mods(st)
            rows = self.lines(['store ' + h, 'build'], 'BUILD')
            got = rows[0].split('|') if rows and rows[0] else []
            self.assertEqual(got, ['%s=*' % m for m in mods], h)
            for i, m in enumerate(mods):
                info = self.lines(['store ' + h, 'parkinfo %d' % i], 'PARKINFO')[0].split('~')
                self.assertEqual(info[0], 'PARKED DATA')
                bits = {}
                for f in st['parked']:
                    if f['mod'] == m:
                        bits[f['scope']] = bits.get(f['scope'], 0) + f['bits']
                text = ', '.join(info[1:])
                for scope, b in bits.items():
                    self.assertIn('%s: %d bits' % (scope, b), text)
                self.assertTrue(all(len(l) <= 30 for l in info), info)

    def test_a_bad_store_parks_nothing_and_forgets_nothing(self):
        h = next(h for h in self.stores if ds.decode(bytes.fromhex(h))['parked'])
        raw = bytearray(bytes.fromhex(h))
        raw[-1] ^= 1                                       # the CRC no longer matches
        self.assertEqual(self.lines(['store ' + raw.hex(), 'build'], 'BUILD', active=['x.elemod']), ['x'])
        self.assertEqual(self.lines(['store ' + raw.hex(), 'forget duck'], 'FORGET'), ['-1'])
        self.assertEqual(self.lines(['store ' + h[:-8], 'forget duck'], 'FORGET'), ['-1'])   # cut short

    def test_parked_rows_follow_the_build(self):
        h = next(h for h in self.stores if ds.decode(bytes.fromhex(h))['parked'])
        mods = self.parked_mods(ds.decode(bytes.fromhex(h)))
        got = self.lines(['store ' + h, 'build'], 'BUILD', active=['x.elemod'])[0].split('|')
        self.assertEqual(got, ['x'] + ['%s=*' % m for m in mods])

    def test_a_parked_mod_back_in_the_build_has_one_row_with_a_star(self):
        """decided 2026-11-18 (the A4's): no PARK row; a build row whose mod has saved data shows
        "* " before its state word, and a parked mod back in the build has no second row."""
        h = next(h for h in self.stores if ds.decode(bytes.fromhex(h))['parked'])
        mods = self.parked_mods(ds.decode(bytes.fromhex(h)))
        got = self.lines(['id x.elemod|' + mods[0], 'store ' + h, 'build'], 'BUILD', active=['x.elemod'])
        self.assertEqual(got[0].split('|'), [mods[0] + '=* ADD'] + ['%s=*' % m for m in mods[1:]])

    def test_live_data_stars_a_build_row_and_none_without_data(self):
        h, live = next((h, f['mod']) for h in self.stores for f in ds.decode(bytes.fromhex(h))['fields'])
        mods = self.parked_mods(ds.decode(bytes.fromhex(h)))
        got = self.lines(['id x.elemod|' + live, 'store ' + h, 'build'], 'BUILD', active=['x.elemod'])
        self.assertEqual(got[0].split('|')[0], live + '=* ADD')
        got = self.lines(['id x.elemod|nobody', 'store ' + h, 'build'], 'BUILD', active=['x.elemod'])
        self.assertEqual(got[0].split('|'), ['nobody'] + ['%s=*' % m for m in mods])

    def test_storage_room_refusals_in_full(self):
        got = self.lines(['reason 29 0 sound 62 56', 'reason 29 0 codes 3 1', 'reason 29 0 core_handles 3 2'],
                         'REASON')
        self.assertEqual(got, ['NO STORAGE ROOM~sound: 62 of 56 bits', 'NO STORAGE ROOM~codes: 3 of 1 free',
                               'NO STORAGE ROOM~core_handles: 3 of 2 spare'])

    def test_site_refusals_name_the_site_and_who_has_it(self):
        got = self.lines(['setid 3 fx-sends',
                          'reason 30 0x100 400a005e', 'reason 30 0x100 400a005e core-ar2',
                          'reason 30 0x203 40123456', 'reason 30 0x300 40000100', 'reason 30 0x400 4009febc'],
                         'REASON')
        self.assertEqual(got, ['SITE 0x400a005e~TAKEN BY THE BUILD', 'SITE 0x400a005e~TAKEN BY core-ar2',
                               'SITE 0x40123456~TAKEN BY fx-sends', 'SITE 0x40000100~IN A PRE-LINK RANGE',
                               'SITE 0x4009febc~NOT STOCK BYTES'])
        self.assertTrue(all(len(l) <= 30 for g in got for l in g.split('~')))


class CheckTick(unittest.TestCase):
    """The background check tick by tick on a made-up card whose calls take bus time
    (tests/drive_check_tick_host.c). T230 (2026-11-09, the unit, 257 tiny files): the first DOWN
    after YES into the folder was refused, and closing the page partway left /mods Busy."""

    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.exe = os.path.join(cls.work.name, 'drive_check_tick_host')
        subprocess.run(['cc', '-O1', '-w', '-no-pie', '-I', SDK, '-o', cls.exe, os.path.join(HERE, 'drive_check_tick_host.c'),
                        '-Wl,--unresolved-symbols=ignore-all'], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def run_check(self, files=257, open_us=10000, read_us=5000, check_us=5000, show=100000, ticks=2000, keys=0):
        r = subprocess.run([self.exe] + [str(a) for a in (files, open_us, read_us, check_us, show, ticks, keys)],
                           capture_output=True, text=True, check=True)
        return dict(l.split(' ', 1) for l in r.stdout.splitlines())

    def test_reading_a_folder_opens_no_file(self):
        self.assertEqual(self.run_check(ticks=0)['OPENS_AT_FOLDER'], '0')

    def test_the_check_finishes_while_the_page_shows(self):
        got = self.run_check(ticks=4000, keys=30)
        self.assertEqual((got['CHECKED'], got['STAGING'], got['READING']), ('257/257', '0', '0'))

    def test_the_check_takes_a_quarter_of_the_ui_task_on_a_slow_card(self):
        # each file: a 40 ms open, two 30 ms reads, a 20 ms check; at most 1/4 of any second
        # (one step past it allowed), where whole-tick work took more than the second (1.2 s)
        got = self.run_check(open_us=40000, read_us=30000, check_us=20000)
        self.assertLessEqual(int(got['BUSIEST_US']), 250000 + 70000)

    def test_closing_mid_read_gives_the_staging_buffer_back(self):
        # NO x4 at the close starts QUIET while a file is half read (T230: 32/257, /mods Busy)
        for close in range(100, 106):
            got = self.run_check(show=close, ticks=600)
            self.assertEqual((got['STAGING'], got['READING']), ('0', '0'), close)
            self.assertNotEqual(got['CHECKED'], '257/257')



class Reader(unittest.TestCase):
    """boot.c's dl_read_more over a made-up file (tests/drive_reader_host.c): a file larger than the
    live stage reads to its end for its size and CRC alone, never past the stage, so the MODS page
    can leave its check to the boot (page.c inspect, "big"; STAGE.md)."""

    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.exe = os.path.join(cls.work.name, 'drive_reader_host')
        subprocess.run(['cc', '-O1', '-w', '-no-pie', '-I', SDK, '-o', cls.exe, os.path.join(HERE, 'drive_reader_host.c'),
                        '-Wl,--unresolved-symbols=ignore-all'], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def read(self, size, max_):
        out = subprocess.run([self.exe, str(size), str(max_)], capture_output=True, text=True,
                             check=True).stdout.split()
        crc = zlib.crc32(bytes((i * 7 + 3) & 0xff for i in range(size)))
        return int(out[1]), int(out[3]), int(out[5], 16) == crc, out[7]

    def test_a_file_that_fits_reads_whole(self):
        self.assertEqual(self.read(1000, 0x10000), (1, 1000, True, 'ok'))
        self.assertEqual(self.read(0x10000, 0x10000), (1, 0x10000, True, 'ok'))

    def test_a_file_over_the_stage_is_measured_not_kept(self):
        self.assertEqual(self.read(0x10001, 0x10000), (-2, 0x10001, True, 'ok'))
        self.assertEqual(self.read(1060000, 0x10000), (-2, 1060000, True, 'ok'))

    def test_a_pending_update_is_read_in_place_of_the_file(self):
        """drive-loader 0.12: an upload over a file in use waits as <name>.new, which the next boot
        renames over it; the page checks and CRCs that one, so a save names the file the boot links."""
        out = subprocess.run([self.exe, '1000', str(0x10000), '700'], capture_output=True, text=True,
                             check=True).stdout.split()
        self.assertEqual((int(out[1]), int(out[3])), (1, 700))
        self.assertEqual(int(out[5], 16), zlib.crc32(bytes((i * 11 + 5) & 0xff for i in range(700))))
        self.assertTrue(out[9].endswith('/x.elemod.new'), out[9])
        out = subprocess.run([self.exe, '1000', str(0x10000)], capture_output=True, text=True,
                             check=True).stdout.split()
        self.assertTrue(out[9].endswith('/x.elemod'), out[9])

if __name__ == '__main__':
    unittest.main()
