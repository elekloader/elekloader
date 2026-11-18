# SPDX-License-Identifier: GPL-2.0-or-later
"""The A4 Drive Loader's MODS page (mods/drive-loader-a4mk2/page.c, PAGE.md), built for the computer
by tests/drive_a4_page_host.c with a made-up /mods and checks that pass every file: what it draws
with std1's helpers, which keys and turns it takes, and its LOAD TO BUILD save.

    python tests/test_drive_a4_page.py

Needs a C compiler (cc). Skipped while the loader's headers (dl.h, fs.h, park.h) are not in the
mod's folder."""
import os
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
MOD = os.path.join(os.path.dirname(HERE), 'mods', 'drive-loader-a4mk2')
SDK = os.path.join(os.path.dirname(HERE), 'elekloader', 'sdk', 'include')
HEADERS = ('dl.h', 'fs.h', 'park.h')
MISSING = [h for h in HEADERS if not os.path.exists(os.path.join(MOD, h))]


@unittest.skipIf(MISSING, 'the loader headers are not there yet: %s' % ', '.join(MISSING))
class Page(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.exe = os.path.join(cls.work.name, 'drive_a4_page_host')
        subprocess.run(['cc', '-O1', '-Wall', '-Wno-unused-function', '-Wno-pointer-to-int-cast',
                        '-Wno-format-truncation', '-I', SDK, '-I', MOD, '-include', os.path.join(MOD, 'dl.h'),
                        '-o', cls.exe, os.path.join(HERE, 'drive_a4_page_host.c')], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def run_page(self, script, *files):
        out = subprocess.run([self.exe, script] + list(files), capture_output=True, text=True,
                             check=True).stdout.splitlines()
        frames, frame, takes, states = [], None, [], []
        self.restarts = [line[8:] for line in out if line.startswith('RESTART ')]
        for line in out:
            if line.startswith('F '):
                body = line[2:]
                if frame is None or body.isdigit():
                    frame = []
                    frames.append(frame)
                    if body.isdigit():
                        continue
                x, y, rest = body[:3], body[4:6], body[7:]
                frame.append((int(x), int(y), rest[0] == '*', rest[1:]))
            elif line.startswith('TAKE '):
                takes.append(line[5:])
            elif line.startswith('S '):
                states.append(dict(kv.split('=', 1) for kv in line[2:].split(' ')))
        return frames, takes, states

    @staticmethod
    def texts(frame):
        return [t for _x, _y, _inv, t in frame]

    def test_an_empty_folder_shows_the_placeholder_row_and_opens_no_panel(self):
        frames, takes, states = self.run_page('ofrlyfsns')
        for f in frames:
            self.assertEqual(f, [(8, 0, True, 'NO MODS ON +DRIVE')])   # no title: row 0 at the top
        self.assertEqual(states[0]['panel'], '0')
        self.assertEqual(states[0]['open'], '1')
        self.assertEqual(states[1]['open'], '0')          # NO closes the page
        self.assertTrue(all(t.endswith('ret=1') for t in takes), takes)   # every key was the page's

    def test_keys_and_turns_go_to_stock_again_once_the_page_has_closed(self):
        _frames, takes, _states = self.run_page('ka+onk+')
        self.assertEqual(takes[:4], ['key=30 ret=0', 'key=30 up ret=0', 'enc=0 ret=0', 'enc=10 ret=0'])
        self.assertEqual(takes[4:6], ['key=45 ret=1', 'key=45 up ret=1'])   # NO's release: still the page's
        self.assertEqual(takes[6:], ['key=30 ret=0', 'key=30 up ret=0', 'enc=10 ret=0'])

    def test_rows_folders_sizes_and_ticks(self):
        frames, _takes, states = self.run_page('oTfdyTf', 'drums/', 'a.elemod:2000', 'b.txt:10',
                                                'drums/k.elemod:1000')
        self.assertEqual(self.texts(frames[0]), ['(D)', 'drums', '1KB', 'a'])   # no .txt
        ticked = frames[1]
        self.assertIn((4, 10, True, 'TICK'), ticked)       # row 1 ticked, the cursor's (light on the bar)
        self.assertIn((8, 8, True, 'a'), ticked)

    def test_a_folder_opens_and_dot_dot_goes_back_to_it(self):
        frames, _takes, states = self.run_page('oyfyfs', 'drums/', 'a.elemod', 'drums/k.elemod')
        self.assertEqual(self.texts(frames[0])[:4], ['(D)', '..', 'k'])
        self.assertEqual(frames[1][0:2], [(106, 0, True, '(D)'), (8, 0, True, 'drums')])   # back on it; (D) right-aligned to x 117
        self.assertEqual(states[0]['cur'], '0')

    def test_load_to_build_saves_the_ticked_files_after_its_confirm(self):
        frames, _takes, states = self.run_page('oyrddyfsyTs', 'a.elemod', 'b.elemod')
        confirm = self.texts(frames[-1])
        self.assertEqual(confirm[-2:], ['LOAD 1 MOD?', 'CONFIRM Y/N'])
        self.assertIn('LOAD TO BUILD', confirm)
        self.assertEqual(states[0]['pending'], '1')
        self.assertEqual(states[1]['active'], "#_chosen_on_the_unit's_MODS_page\\na.elemod_12345678\\n")
        self.assertEqual(states[1]['staging'], '0')        # the buffer went back to the route

    def test_no_on_the_confirm_window_saves_nothing(self):
        _frames, _takes, states = self.run_page('oyrddynTs', 'a.elemod')
        self.assertEqual(states[0]['pending'], '0')
        self.assertEqual(states[0]['active'], '')

    def test_the_build_list_after_a_load(self):
        frames, _takes, _states = self.run_page('oyrddyyTnlyf', 'a.elemod', 'b.elemod')
        self.assertEqual(self.texts(frames[-1]), ['ADD', 'a'])

    def test_saved_data_marks_a_build_row_with_a_star_in_its_own_column(self):
        frames, _takes, _states = self.run_page('oyrddyyTnlyf', 'a.elemod', '@store=a', '@park=dist')
        rows = [(x, y, t) for x, y, _inv, t in frames[-1]]
        self.assertIn((106, 0, 'ADD'), rows)                # the state word as before
        star = [(x, y) for x, y, t in rows if t == '*']
        self.assertEqual(star, [(95, 0), (95, 8)])          # "* ADD", then the parked mod: one column
        self.assertIn((8, 8, 'dist'), rows)
        self.assertNotIn('PARK', [t for _x, _y, t in rows])

    def test_no_star_without_saved_data(self):
        frames, _takes, _states = self.run_page('oyrddyyTnlyf', 'a.elemod', '@store=b')
        self.assertEqual(self.texts(frames[-1]), ['ADD', 'a'])

    def test_a_parked_mod_back_in_the_build_has_one_row_and_forget_data_reaches_it(self):
        frames, _takes, states = self.run_page('oyrddyyTnlyfyrddddys', 'a.elemod', '@park=a')
        self.assertEqual(self.texts(frames[-1]), ['ADD', '*', 'a'])   # no second row for its parked data
        self.assertEqual(states[-1]['pending'], '6')        # FORGET DATA? on the ticked build row

    def test_the_cursor_scrolls_with_up_down_func_and_the_data_encoder(self):
        files = ['m%02d.elemod' % i for i in range(10)]
        _frames, _takes, states = self.run_page('odddddddsUs++-sus', *files)
        self.assertEqual((states[0]['cur'], states[0]['top']), ('7', '0'))   # eight rows
        self.assertEqual((states[1]['cur'], states[1]['top']), ('9', '2'))   # FUNC: a page, to the end
        self.assertEqual((states[2]['cur'], states[2]['top']), ('8', '2'))   # +, + at the end, then -
        self.assertEqual((states[3]['cur'], states[3]['top']), ('7', '2'))

    def test_a_message_box_takes_the_next_key_and_closes_by_itself(self):
        _frames, _takes, states = self.run_page('orddysntsorddyTs', 'a.elemod')
        self.assertEqual(states[0]['box'], 'NO_MODS_SELECTED')
        self.assertEqual(states[1]['open'], '1')           # that NO closed the box, not the page
        self.assertEqual(states[2]['box'], '-')            # after 3 s

    def test_apply_and_restart_shows_only_while_the_saved_set_differs(self):
        frames, _takes, _states = self.run_page('olyrf', 'a.elemod')          # nothing saved or loaded
        self.assertNotIn('APPLY & RESTART', self.texts(frames[-1]))
        self.assertIn('FORGET DATA', self.texts(frames[-1]))
        frames, _takes, _states = self.run_page('oyrddyyTnlyrf', 'a.elemod')  # a.elemod saved: ADD
        self.assertEqual(self.texts(frames[-1])[-1], 'APPLY & RESTART')

    def test_no_on_the_restart_confirm_does_nothing(self):
        frames, _takes, states = self.run_page('oyrddyyTnlyrdddddyfntTs', 'a.elemod')
        self.assertEqual(self.texts(frames[-1])[-4:], ['RESTART NOW?', 'UNSAVED PROJECT', 'CHANGES ARE LOST',
                                                       'CONFIRM Y/N'])
        self.assertEqual(states[-1]['pending'], '0')
        self.assertEqual(self.restarts, [])

    def test_yes_on_the_restart_confirm_says_so_then_restarts_without_the_guard(self):
        frames, takes, states = self.run_page('oyrddyyTnlyrdddddyystnfstt', 'a.elemod')
        self.assertEqual(states[-2]['box'], 'RESTARTING')   # 1 s on: not yet
        self.assertEqual(states[-2]['panel'], '0')
        self.assertEqual(states[-1]['box'], 'RESTARTING')   # NO after YES: the box stays
        self.assertIn('RESTARTING', self.texts(frames[-1]))
        self.assertEqual(self.restarts, ['locked=1 guard_off=1 box=RESTARTING'])


if __name__ == '__main__':
    unittest.main()
