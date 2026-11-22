# SPDX-License-Identifier: GPL-2.0-or-later
"""The A4's Drive payload table (drive-loader-a4mk2 0.2; docs/DRIVE.md, "Payloads on the +Drive"):
the versioned API for sidecar-core (analog-four-mk2/drivepay.h), the files in use the route and the
MODS page protect, the lost entries .status shows, and the boot's swap of an update that waited as
<name>.new, which on the A4 is one directory write of the loader's own store (store.c ds_swap).
tests/drive_a4_payload_host.c builds payload.c, boot.c, skip.c and store.c for the computer over a
card in RAM. The table's logic is the Rytm's (tests/test_drive_payload.py); what differs is the
store under it.

    python tests/test_drive_a4_payload.py

Needs a C compiler (cc)."""
import hashlib
import os
import subprocess
import tempfile
import unittest
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
A4 = os.path.join(ROOT, 'mods', 'drive-loader-a4mk2')


def fnv(s):
    h = 0x811C9DC5
    for b in s.encode():
        h = ((h ^ b) * 0x01000193) & 0xffffffff
    return h


def crc(t):
    return '%08x' % zlib.crc32(t.encode())


class Table(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        store = os.path.join(cls.work.name, 'store.o')
        cls.exe = os.path.join(cls.work.name, 'drive_a4_payload_host')
        # the store's ds_swap is built under another name, so the host's can fail one path
        subprocess.run(['cc', '-O1', '-Wall', '-DDL_HOST', '-Dds_swap=store_ds_swap', '-I', A4, '-c',
                        os.path.join(A4, 'store.c'), '-o', store], check=True)
        subprocess.run(['cc', '-O1', '-w', '-no-pie', '-DDL_HOST', '-DDL_HOST_HEAP', '-I', A4, '-o', cls.exe,
                        os.path.join(HERE, 'drive_a4_payload_host.c'), os.path.join(A4, 'payload.c'),
                        os.path.join(A4, 'boot.c'), os.path.join(A4, 'skip.c'), store,
                        '-Wl,--unresolved-symbols=ignore-all'], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def run_(self, *requests):
        r = subprocess.run([self.exe], capture_output=True, text=True, check=True,
                           input='\n'.join(requests) + '\n')
        self.assertNotIn('FAILED', r.stdout)
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

    def test_in_place_decode_reads_every_stretch_whole(self):
        """0.2's faster read (the Rytm's 0.12): the hex read into the caller's buffer and decoded in
        place, reads of a sector or more cut to end on a card sector, a byte's first hex character
        carried when the hex sits at an odd place, the last 64 bytes or fewer through the stack.
        Every length and start, at every parity and place in a sector, decodes right and writes
        nothing past n."""
        cases = [(at, n) for at in (0, 1, 2, 63, 64, 65, 333) for n in list(range(0, 140)) + [255, 256, 257, 367]]
        cases += [(at, n) for at in (0, 1, 190, 191, 255, 256, 257) for n in (511, 512, 513, 1023, 1024, 1500, 2600)]
        for pad in (0, 1, 447, 448, 449, 511):
            reqs = ['pad %d' % pad, 'hexfile a.elemod 3000', 'note a.elemod 3000', 'keep a.elemod=modA=L']
            reqs += ['read 0 %d %d' % c for c in cases]
            out = self.run_(*reqs)
            for (at, n), o in zip(cases, out):
                want = max(0, min(n, 3000 - at))
                self.assertTrue(o.startswith('READ %d ok GUARD ok' % want), (pad, at, n, o))

    def test_a_4k_read_takes_a_few_card_reads_not_64(self):
        """0.1 read 128 hex bytes a time, each through a whole sector: 64 card reads a 4 KB read on
        a sector, 80 off one. From 0.2: 7 on a sector, 10 off one, odd places too; a ~530 KB
        payload read in 4 KB calls moves at least 400 bytes a card read."""
        size = 530 * 1024
        for pad, first, most in ((448, 7, 7), (449, 10, 10), (0, 10, 10), (1, 10, 10)):
            reqs = ['pad %d' % pad, 'hexfile b.elemod %d' % size, 'note b.elemod %d' % size,
                    'keep b.elemod=modB=L', 'read 0 0 4096']
            reqs += ['read 0 %d 4096' % at for at in range(0, size, 4096)]
            out = self.run_(*reqs)
            self.assertEqual(out[0], 'READ 4096 ok GUARD ok READS %d' % first, pad)
            self.assertTrue(all(' ok GUARD ok' in o for o in out[1:]), pad)
            self.assertLessEqual(max(int(o.split()[-1]) for o in out[1:]), most, pad)
            self.assertGreaterEqual(size / sum(int(o.split()[-1]) for o in out[1:]), 400, pad)

    def test_bad_hex_anywhere_in_a_long_read_is_refused(self):
        """a bad character in a sector read straight into the buffer, in the carried character, or
        in the stack's tail: -3 each time, at both parities"""
        for pad in (448, 449):
            for bad in (0, 1, 1023, 1024, 3000, 4095, 7000, 8100, 8191, 8192, 8193, 9999):
                out = self.run_('pad %d' % pad, 'hexfile a.elemod 5000', 'badhex a.elemod %d' % bad,
                                'note a.elemod 5000', 'keep a.elemod=modA=L', 'read 0 0 4096', 'read 0 4096 904')
                want = ['READ -3 ok', 'READ 904 ok'] if bad < 8192 else ['READ 4096 ok', 'READ -3 ok']
                self.assertEqual([o.split(' GUARD')[0] for o in out], want, (pad, bad))
                self.assertTrue(all('GUARD ok' in o for o in out), (pad, bad))

    def test_a_changed_file_or_bad_hex_is_refused(self):
        """bad hex where the file stays is -3; the file written again (a new run and size) is -2"""
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

    def test_status_after_a_boot_with_updates_counts_them(self):
        out = self.run_('file a.elemod OLDA', 'file a.elemod.new NEWA', 'updates a.elemod=' + crc('OLDA'),
                        'status')
        self.assertEqual(out[2], 'STATUS payloads: 0 kept, 0 lost~updates at boot: 1 applied, 0 failed~')

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

    def test_the_boot_swaps_each_lines_update_and_keeps_both_on_failure(self):
        out = self.run_('file a.elemod OLDA', 'file a.elemod.new NEWA', 'file b.elemod OLDB',
                        'file c.elemod OLDC', 'file c.elemod.new NEWC', 'file x.elemod.new STRAY',
                        'failswap c.elemod',
                        'updates a.elemod=%s b.elemod=%s c.elemod=%s' % (crc('OLDA'), crc('OLDB'), crc('OLDC')),
                        'tree')
        # a swapped line takes the new file's CRC, in the set and in /mods/active; no <name>.old
        # step on the A4: one directory write swaps the pair
        self.assertEqual(out, ['UPDATES 1 1',
                               'CRCS a.elemod=%s b.elemod=%s c.elemod=%s' % (crc('NEWA'), crc('OLDB'), crc('OLDC')),
                               'TREE a.elemod=NEWA ; active=# updates applied at boot|a.elemod %s|'
                               'b.elemod %s|c.elemod %s| ; b.elemod=OLDB ; c.elemod=OLDC ; '
                               'c.elemod.new=NEWC ; x.elemod.new=STRAY' % (crc('NEWA'), crc('OLDB'), crc('OLDC'))])

    def test_a_failed_swap_counts_and_active_names_the_old_file_again(self):
        """active takes the new CRC before the swap (a cut can't leave it behind the files), so a
        swap that fails writes it back with the old file's CRC"""
        out = self.run_('file a.elemod OLDA', 'file a.elemod.new NEWA', 'file active a.elemod 1',
                        'failswap a.elemod', 'updates a.elemod=1', 'tree')
        self.assertEqual(out, ['UPDATES 0 1', 'CRCS a.elemod=00000001',
                               'TREE a.elemod=OLDA ; a.elemod.new=NEWA ; '
                               'active=# updates applied at boot|a.elemod 00000001|'])

    def test_a_cut_anywhere_in_the_swap_leaves_the_old_pair_or_the_new_file(self):
        """a power cut after any number of sector writes: after the restart the store holds the old
        file and its .new (swapped at the next boot, which then completes), or the new file alone;
        never neither, and the old run never under the new name"""
        seen = set()
        for k in range(0, 8):
            out = self.run_('file a.elemod OLDA', 'file a.elemod.new NEWA', 'file active a.elemod ' + crc('OLDA'),
                            'cut %d' % k, 'updates a.elemod=' + crc('OLDA'), 'remount', 'tree',
                            'updates a.elemod=' + crc('OLDA'), 'tree')
            files = dict(e.split('=', 1) for e in out[2][len('TREE '):].split(' ; '))
            if files.get('a.elemod') == 'OLDA':
                self.assertEqual(files.get('a.elemod.new'), 'NEWA', (k, out))
                seen.add('old pair')
            else:
                self.assertEqual(files.get('a.elemod'), 'NEWA', (k, out))
                self.assertNotIn('a.elemod.new', files, (k, out))
                seen.add('new')
            after = dict(e.split('=', 1) for e in out[-1][len('TREE '):].split(' ; '))
            self.assertEqual(after['a.elemod'], 'NEWA', (k, out))
            self.assertNotIn('a.elemod.new', after, (k, out))
            self.assertIn('a.elemod ' + crc('NEWA'), after['active'], (k, out))
        self.assertEqual(seen, {'old pair', 'new'})

    def test_an_update_with_no_old_file_is_renamed_and_its_crc_taken(self):
        c = crc('FIRST')
        out = self.run_('file n.elemod.new FIRST', 'updates n.elemod', 'tree')
        self.assertEqual(out, ['UPDATES 1 0', 'CRCS n.elemod=' + c,
                               'TREE active=# updates applied at boot|n.elemod %s| ; n.elemod=FIRST' % c])

    def test_an_active_already_naming_the_new_crc_is_left_as_it_is(self):
        c = crc('NEWA')
        out = self.run_('file a.elemod OLDA', 'file a.elemod.new NEWA', 'file active a.elemod ' + c,
                        'updates a.elemod=' + c, 'tree')
        self.assertEqual(out, ['UPDATES 1 0', 'CRCS a.elemod=' + c,
                               'TREE a.elemod=NEWA ; active=a.elemod ' + c])

    def test_a_payload_files_crc_is_its_staged_ones(self):
        """the new file's CRC skips its payload's hex (skip.h), as the page's and the host's do"""
        body = bytes(range(40))
        head = '{"id": "p", "sections": {".payload": {"len": %d, "sha256": "%s", "parts": [["hex", "' % (
            len(body), hashlib.sha256(body).hexdigest())
        tail = '"]]}}}'
        out = self.run_('file p.elemod.new ' + head + body.hex() + tail, 'updates p.elemod')
        self.assertEqual(out[1], 'CRCS p.elemod=%08x' % zlib.crc32((head + tail).encode()))


if __name__ == '__main__':
    unittest.main()
