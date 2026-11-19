# SPDX-License-Identifier: GPL-2.0-or-later
"""Parked data on the A4's MODS page: mods/drive-loader-a4mk2/park.c, built for the computer by
tests/drive_a4_park_host.c, reads /mods/.store in the A4's formats 2 and 3 (lock codes per track kind,
elekloader/drive_store.py FORMAT_KINDS) as drive_store.decode does, and FORGET DATA rewrites it byte
for byte as drive_store.forget then encode. A format 1 file is one the A4's boot drops (dstore.c
DS_FORMAT), so nothing in it is parked. tests/drive_a4_page_host.c stubs park.c; this is its test.

    python tests/test_drive_a4_park.py

Needs a C compiler (cc)."""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from elekloader import drive_store as ds  # noqa: E402
from elekloader import store  # noqa: E402

MOD = os.path.join(os.path.dirname(HERE), 'mods', 'drive-loader-a4mk2')
SDK = os.path.join(os.path.dirname(HERE), 'elekloader', 'sdk', 'include')


def field(mod, sym, scope, offset, bits, init=0):
    f = {'mod': mod, 'sym': sym, 'scope': scope, 'offset': offset, 'bits': bits}
    if init:
        f['init'] = init
    return f


def state(kinded=True):
    """A store with live fields and codes, and three parked mods: one with fields in two scopes and a
    synth code, one with only an FX and a CV code kept, one with a field and a CC."""
    return {
        'flashed_id': 0x1dc23152, 'id': 0x0badcafe,
        'next_code': {'synth': 0x30, 'fx': 0x12, 'cv': 0x05} if kinded else 0x30,
        'scopes': {s: {'fgen': 1, 'gen': 2, 'low': 64} for s in store.SCOPES},
        'ids': [0x11111111, 0x22222222],
        'fields': [field('~T live', 'gain', 'sound', 0, 7)],
        'parked': [field('~T ae-cover', 'depth', 'sound', 8, 7, 3), field('~T rand', 'seed', 'kit', 0, 16),
                   field('~T ae-cover', 'mode', 'global', 4, 2), field('~T cc-mod', 'amt', 'track', 2, 5)],
        'codes': [dict({'mod': '~T live', 'sym': 'gain', 'code': 0x20}, **({'kind': 'synth'} if kinded else {}))],
        'freed_codes': [dict({'mod': m, 'sym': s, 'code': c}, **({'kind': k} if kinded else {}))
                        for m, s, c, k in (('~T ae-cover', 'depth', 0x21, 'synth'),
                                           ('~T rand', 'seed', 0x10, 'fx'), ('~T rand', 'seed', 0x03, 'cv'))],
        'ccs': [{'mod': '~T live', 'sym': 'gain', 'cc': 70}],
        'freed_ccs': [{'mod': '~T cc-mod', 'sym': 'amt', 'cc': 71}],
    }


def parked_mods(st):
    out = []
    for k in ('parked', 'freed_codes', 'freed_ccs'):
        for f in st[k]:
            if f['mod'] not in out:
                out.append(f['mod'])
    return out


class Park(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.exe = os.path.join(cls.work.name, 'drive_a4_park_host')
        subprocess.run(['cc', '-O1', '-Wall', '-I', SDK, '-I', MOD, '-include', os.path.join(MOD, 'dl.h'),
                        '-o', cls.exe, os.path.join(HERE, 'drive_a4_park_host.c')], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def ask(self, raw, *requests):
        r = subprocess.run([self.exe], capture_output=True, text=True, check=True,
                           input='\n'.join(['store ' + raw.hex()] + list(requests)) + '\n')
        return r.stdout.splitlines()

    def test_the_store_is_format_2(self):
        raw = ds.encode(state())
        self.assertEqual(raw[4:6], bytes([0, ds.FORMAT_KINDS]))
        self.assertIsNotNone(ds.decode(raw))

    def test_a_format_2_store_lists_its_parked_mods(self):
        st = state()
        out = self.ask(ds.encode(st), 'count', 'list')
        mods = parked_mods(st)
        self.assertEqual(out[0], 'COUNT %d' % len(mods))
        self.assertEqual(out[1], 'LIST %d' % len(mods))
        for line, m in zip(out[2:], mods):
            bits = [sum(f['bits'] for f in st['parked'] if f['mod'] == m and f['scope'] == s)
                    for s in store.SCOPES]
            want = [sum(1 for f in st['parked'] if f['mod'] == m),
                    sum(1 for c in st['freed_codes'] if c['mod'] == m),
                    sum(1 for c in st['freed_ccs'] if c['mod'] == m)] + bits
            self.assertEqual(line, 'PARK %s %s' % (m, ' '.join(map(str, want))))

    def test_store_has_any_live_or_parked_mod(self):
        raw = ds.encode(state())
        out = self.ask(raw, 'has ~T live', 'has ~T ae-cover', 'has ~T rand', 'has ~T cc-mod', 'has ~T other',
                       'has ~T liv', 'hasp ~T liv', 'hasp ~T x')
        self.assertEqual(out, ['HAS 1', 'HAS 1', 'HAS 1', 'HAS 1', 'HAS 0', 'HAS 0', 'HAS 1', 'HAS 0'])
        self.assertEqual(self.ask(raw[:-1], 'has ~T live'), ['HAS 0'])   # a bad file keeps nothing

    def test_forget_matches_drive_store(self):
        st = state()
        raw = ds.encode(st)
        mods = parked_mods(st) + ['nobody']
        asks = [[m] for m in mods] + [mods]
        got = self.ask(raw, *['forget ' + '|'.join(a) for a in asks])
        for a, g in zip(asks, got):
            want = st
            for m in a:
                want, _ch = ds.forget(want, m)
            self.assertEqual(g, 'FORGET ' + ds.encode(want).hex(), a)
            self.assertIsNotNone(ds.decode(bytes.fromhex(g[7:])))

    def test_a_format_3_store_keeps_its_knob_runs(self):
        """With +Drive knob runs (format 3): the same mods listed, and FORGET DATA as drive_store's,
        the runs kept."""
        st = dict(state(), knobs=[{'mod': 'ka', 'first': 0x4000, 'count': 4}],
                  freed_knobs=[{'mod': 'a-gone-mod', 'first': 0x4004, 'count': 2}], next_knob=0x4006)
        raw = ds.encode(st)
        self.assertEqual(raw[4:6], bytes([0, ds.FORMAT_KNOBS]))
        self.assertEqual(self.ask(raw, 'count')[0], 'COUNT %d' % len(parked_mods(st)))
        mods = parked_mods(st)
        g = self.ask(raw, 'forget ' + '|'.join(mods))[0]
        want = st
        for m in mods:
            want, _ch = ds.forget(want, m)
        self.assertEqual(g, 'FORGET ' + ds.encode(want).hex())
        self.assertEqual(ds.decode(bytes.fromhex(g[7:]))['knobs'], st['knobs'])

    def test_nothing_parked_after_forgetting_all(self):
        st = state()
        g = self.ask(ds.encode(st), 'forget ' + '|'.join(parked_mods(st)))[0]
        self.assertEqual(self.ask(bytes.fromhex(g[7:]), 'count'), ['COUNT 0'])

    def test_a_format_1_store_is_dropped(self):
        raw = ds.encode(state(kinded=False))
        self.assertEqual(raw[4:6], bytes([0, ds.FORMAT]))
        self.assertEqual(self.ask(raw, 'count', 'forget ~T rand'), ['COUNT -1', 'FORGET -1'])

    def test_a_bad_store_parks_nothing(self):
        raw = bytearray(ds.encode(state()))
        raw[-1] ^= 1                                       # the CRC no longer matches
        self.assertEqual(self.ask(bytes(raw), 'count', 'forget ~T rand'), ['COUNT -1', 'FORGET -1'])
        self.assertEqual(self.ask(bytes(raw[:-4]), 'count'), ['COUNT -1'])   # cut short


if __name__ == '__main__':
    unittest.main()
