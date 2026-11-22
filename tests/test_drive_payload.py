# SPDX-License-Identifier: GPL-2.0-or-later
"""The Drive payload table (drive-loader-ar2 0.12; docs/DRIVE.md, "Payloads on the +Drive"): its
versioned API for sidecar-core (analog-rytm-mk2/drivepay.h), the files in use the route and the
MODS page protect, the lost entries .status shows, and the boot's swap of an update that waited as
<name>.new. tests/drive_payload_host.c builds payload.c and boot.c for the computer over a made-up
ekFS.

    python tests/test_drive_payload.py

Needs a C compiler (cc)."""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
AR2 = os.path.join(ROOT, 'mods', 'drive-loader-ar2')


def fnv(s):
    h = 0x811C9DC5
    for b in s.encode():
        h = ((h ^ b) * 0x01000193) & 0xffffffff
    return h


class Table(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.exe = os.path.join(cls.work.name, 'drive_payload_host')
        subprocess.run(['cc', '-O1', '-w', '-no-pie', '-DDL_HOST', '-o', cls.exe,
                        os.path.join(HERE, 'drive_payload_host.c'), os.path.join(AR2, 'payload.c'),
                        os.path.join(AR2, 'boot.c'), '-Wl,--unresolved-symbols=ignore-all'], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def run_(self, *requests):
        r = subprocess.run([self.exe], capture_output=True, text=True, check=True,
                           input='\n'.join(requests) + '\n')
        self.assertNotIn('LOCK LEFT', r.stdout)
        return r.stdout.splitlines()

    SET = ['hexfile a.elemod 1000', 'hexfile b.elemod 10', 'hexfile d/e.elemod 40', 'file c.elemod plain',
           'note a.elemod 1000', 'note b.elemod 10', 'note d/e.elemod 40',
           'note a.elemod 1000',                  # pass 2 reads a file again: one entry
           'keep a.elemod=modA=L b.elemod=modB=R c.elemod=modC=L d/e.elemod=modE=L']

    def test_the_api_walks_the_loaded_mods_payloads(self):
        out = self.run_(*self.SET, 'count', 'at 0', 'at 1', 'at 2', 'find modA', 'find modE', 'find modB')
        self.assertEqual(out, ['COUNT 2 LOST 0',
                               'AT %08x 1000 /mods/a.elemod shaok' % fnv('modA'),
                               'AT %08x 40 /mods/d/e.elemod shaok' % fnv('modE'),
                               'AT none', 'FIND /mods/a.elemod', 'FIND /mods/d/e.elemod', 'FIND none'])

    def test_in_use_is_a_kept_payloads_file_or_a_folder_holding_one(self):
        out = self.run_(*self.SET, 'held a.elemod 0', 'held b.elemod 0', 'held c.elemod 0', 'held d 1',
                        'held d/e.elemod 0', 'held d 0', 'held a.elemod 1', 'held a 1')
        self.assertEqual(out, ['HELD 1', 'HELD 0', 'HELD 0', 'HELD 1', 'HELD 1', 'HELD 0', 'HELD 0', 'HELD 0'])

    def test_reads_decode_any_stretch_and_stop_at_the_end(self):
        out = self.run_(*self.SET, 'read 0 0 1000', 'read 0 999 5', 'read 0 3 17', 'read 0 1000 4',
                        'read 0 1001 4', 'read 1 0 1', 'read 2 0 1', 'read 1 39 1')
        self.assertEqual([o.split(' GUARD')[0] for o in out],
                         ['READ 1000 ok', 'READ 1 ok', 'READ 17 ok', 'READ 0 ok', 'READ -1 ok', 'READ 1 ok',
                          'READ -1 ok', 'READ 1 ok'])
        self.assertTrue(all('GUARD ok' in o for o in out))

    def test_in_place_decode_reads_every_stretch_whole_with_few_reads(self):
        """drive-loader-ar2 0.12's faster read: one seek, the hex read into the caller's buffer and
        decoded in place, the last 64 bytes or fewer through the stack. Every length and start,
        odd ones too, decodes right and writes nothing past n; a 4 KB read takes 7 ekFS reads, not 64,
        and a ~530 KB payload read in 4 KB calls moves at least 512 bytes a read."""
        reqs = ['hexfile a.elemod 700', 'note a.elemod 700', 'keep a.elemod=modA=L']
        cases = [(at, n) for at in (0, 1, 2, 63, 64, 65, 333) for n in list(range(0, 140)) + [255, 256, 257, 367]]
        reqs += ['read 0 %d %d' % c for c in cases]
        out = self.run_(*reqs)
        for (at, n), o in zip(cases, out):
            want = max(0, min(n, 700 - at))
            self.assertTrue(o.startswith('READ %d ok GUARD ok' % want), (at, n, o))
        size = 530 * 1024
        reqs = ['hexfile b.elemod %d' % size, 'note b.elemod %d' % size, 'keep b.elemod=modB=L', 'read 0 0 4096']
        reqs += ['read 0 %d 4096' % at for at in range(0, size, 4096)]
        out = self.run_(*reqs)
        self.assertEqual(out[0], 'READ 4096 ok GUARD ok READS 7')
        self.assertTrue(all(' ok GUARD ok' in o for o in out[1:]))
        reads = sum(int(o.split()[-1]) for o in out[1:])
        self.assertLessEqual(reads, 7 * len(out[1:]))
        self.assertGreaterEqual(size / reads, 512)

    def test_a_changed_file_or_bad_hex_is_refused(self):
        out = self.run_(*self.SET, 'badhex a.elemod 501', 'read 0 0 200', 'read 0 200 100', 'touch a.elemod',
                        'read 0 0 4')
        self.assertEqual([o.split(' GUARD')[0] for o in out], ['READ 200 ok', 'READ -3 ok', 'READ -2 ok'])

    def test_status_lists_entries_and_pending_updates_within_its_size(self):
        out = self.run_(*self.SET, 'status', 'file a.elemod.new NEW', 'status')
        self.assertEqual(out, [
            'STATUS payloads: 2 kept, 0 lost~payload %08x 1000 /mods/a.elemod~payload %08x 40 /mods/d/e.elemod~'
            % (fnv('modA'), fnv('modE')),
            'STATUS payloads: 2 kept, 0 lost~payload %08x 1000 /mods/a.elemod~payload %08x 40 /mods/d/e.elemod~'
            'pending /mods/a.elemod~' % (fnv('modA'), fnv('modE'))])

    def test_lost_entries_count_once_and_only_for_loaded_mods(self):
        names = ['m%02d.elemod' % i for i in range(70)]
        reqs = []
        for n in names:
            reqs += ['hexfile %s 4' % n, 'note %s 4' % n]
        reqs += ['note %s 4' % n for n in names]                     # pass 2: nothing counts twice
        reqs.append('count')
        keep = ['%s=id%02d=%s' % (n, i, 'R' if i in (0, 66) else 'L') for i, n in enumerate(names)]
        reqs += ['keep ' + ' '.join(keep), 'count', 'lostof m65.elemod', 'lostof m66.elemod',
                 'lostof m01.elemod', 'status', 'reset', 'count']
        out = self.run_(*reqs)
        self.assertEqual(out[0], 'COUNT 64 LOST 6')
        self.assertEqual(out[1], 'COUNT 63 LOST 5')        # m00 refused: 63 kept; m66 refused: 5 lost
        self.assertEqual(out[2:5], ['LOSTOF 1', 'LOSTOF 0', 'LOSTOF 0'])
        self.assertTrue(out[5].startswith('STATUS payloads: 63 kept, 5 lost~payload '))
        self.assertNotIn('OVER', out[5])
        self.assertEqual(out[6], 'COUNT 0 LOST 0')

    def test_the_boot_swaps_each_lines_update_and_keeps_the_old_on_failure(self):
        out = self.run_('file a.elemod OLDA', 'file a.elemod.new NEWA', 'file b.elemod OLDB',
                        'file c.elemod OLDC', 'file c.elemod.new NEWC', 'file x.elemod.new STRAY',
                        'failrename c.elemod.new', 'updates a.elemod b.elemod c.elemod', 'tree')
        self.assertEqual(out, ['UPDATES 1 1',
                               'TREE a.elemod=NEWA ; b.elemod=OLDB ; c.elemod=OLDC ; c.elemod.new=NEWC ; '
                               'x.elemod.new=STRAY'])
        out = self.run_('file n.elemod.new FIRST', 'updates n.elemod', 'tree')   # no old file: just renamed
        self.assertEqual(out, ['UPDATES 1 0', 'TREE n.elemod=FIRST'])


if __name__ == '__main__':
    unittest.main()
