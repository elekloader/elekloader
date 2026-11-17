# SPDX-License-Identifier: GPL-2.0-or-later
"""std1 on the +Drive: the A4's boot linker (mods/drive-loader-a4mk2/dlink.c) sends a std1 mod's
"std": true contribution to the table the flashed core's std map names, which the build descriptor
carries as claims of kind 3 (desc.CLAIM_STD, with F_STD_MAP). Built for the computer and run against
its twin (elekloader/drive.py) on tests/test_drive.py's made-up build with a std map: the same
refusals, details, area bytes and tables.

    python tests/test_drive_std.py

Needs a C compiler (cc)."""
import json
import os
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
sys.path.insert(0, HERE)

from elekloader import drive                                       # noqa: E402
from test_link_refusals import Bench                               # noqa: E402
import test_drive as TD                                            # noqa: E402

A4 = os.path.join(ROOT, 'mods', 'drive-loader-a4mk2')


def build_host(work):
    exe = os.path.join(work, 'drive_link_host_a4')
    subprocess.run(['cc', '-O1', '-Wall', '-DDL_HOST', '-o', exe, os.path.join(HERE, 'drive_link_host.c'),
                    os.path.join(A4, 'dlink.c'), os.path.join(A4, 'json.c'), os.path.join(A4, 'dstore.c'),
                    os.path.join(A4, 'sha256.c'), os.path.join(A4, 'skip.c')], check=True)
    return exe


class Std:
    """TD.built(B, 'std') as RAM for the host linker, and the twin's Build."""

    def __init__(self, B, area):
        out, b = TD.built(B, area)
        self.B, self.build = B, b
        ddr_base, run_end = out.layout['ddr']
        o = out.layout['run_load'] - B.dev.main_load
        self.ram_base = ddr_base
        self.ram = bytes(out.image[o:o + run_end - ddr_base])
        self.desc_at = out.map['core_desc']


def raw(d):
    return (json.dumps(d, indent=1) + '\n').encode()


def boot(exe, F, mods, work):
    d = tempfile.mkdtemp(dir=work)
    lines = []
    for m in mods:
        name = m['id'] + '.elemod'
        data = raw(m)
        with open(os.path.join(d, name), 'wb') as fh:
            fh.write(data)
        lines.append('%s %08x' % (name, zlib.crc32(data) & 0xFFFFFFFF))
    with open(os.path.join(d, 'active'), 'w') as fh:
        fh.write('\n'.join(lines) + '\n')
    ramf, stockf, outf = (os.path.join(d, x) for x in ('ram.bin', 'stock.bin', 'out.bin'))
    with open(ramf, 'wb') as fh:
        fh.write(F.ram)
    with open(stockf, 'wb') as fh:
        fh.write(F.B.img)
    r = subprocess.run([exe, ramf, '0x%x' % F.ram_base, stockf, '0x%x' % F.B.dev.main_load, '0x%x' % F.desc_at,
                        d, outf], capture_output=True, text=True)
    if r.returncode:
        raise AssertionError('host linker failed: %s %s' % (r.stdout, r.stderr))
    with open(outf, 'rb') as fh:
        return json.loads(r.stdout.splitlines()[0]), fh.read()


def compare(F, sm, ram, r):
    """The C boot against the twin: -> [what differs]."""
    bad = []
    got = [(x['state'], x['code'], x['arg'], x.get('detail', '')) for x in sm['status']]
    want = [(c.state, c.code, c.arg, c.detail) for c in r.choices]
    if got != want:
        bad.append('status %s, twin %s' % (got, want))

    def at(a, n):
        return ram[a - F.ram_base:a - F.ram_base + n]
    if r.live and at(F.build.lo, len(r.area)) != r.area:
        bad.append('area')
    for name, t in r.tables.items():
        if t['bytes'] is not None and at(t['address'], len(t['bytes'])) != t['bytes']:
            bad.append('table %s' % name)
    return bad


def sets(B):
    s = TD.std_mod
    return [
        ('two events, each to its table', [s(B, 'ev_boot'), s(B, 'ev_tick', 'drive-std2')], [0, 0]),
        ('a std mod beside a native one', [s(B, 'ev_boot'), TD.hello(B)], [0, 0]),
        ('an event the map lacks', [s(B, 'ev_nope')], [31]),
        ('a table\'s name is no event', [s(B, 'ev_x')], [31]),
        ('mapped to a closed table', [s(B, 'ev_closed')], [15]),
        ('without "events"', [s(B, 'ev_boot', events=None)], [5]),
        ('"events" of another set', [s(B, 'ev_boot', events='std2')], [5]),
        ('"std": false', [s(B, 'ev_boot', contribute=[{'to': 'ev_boot', 'std': False, 'order': 50,
                                                        'data': '00000000'}])], [5]),
        ('"std": null is no subscription', [s(B, 'ev_x', contribute=[
            {'to': 'ev_x', 'std': None, 'order': 50, 'data': '00000000',
             'relocs': [[0, 'abs32', 'sym:fn_drive-std', 0]]}])], [0]),
        ('"std" before "to"', [s(B, 'ev_boot', contribute=[
            {'std': True, 'order': 50, 'data': '00000000', 'to': 'ev_boot',
             'relocs': [[0, 'abs32', 'sym:fn_drive-std', 0]]}])], [0]),
        ('ev_x\'s one spare: the second std mod is full', [s(B, 'ev_boot'), s(B, 'ev_boot', 'drive-std2')], [0, 16]),
    ]


def main():
    ok = failed = 0
    with Bench('analog-rytm-mk2') as B, tempfile.TemporaryDirectory() as work:
        exe = build_host(work)
        F = Std(B, 'std')
        for name, mods, codes in sets(B):
            sm, ram = boot(exe, F, mods, work)
            r = drive.link(F.build, [(m['id'] + '.elemod', raw(m)) for m in mods])
            bad = compare(F, sm, ram, r)
            if [x['code'] for x in sm['status']] != codes:
                bad.append('codes %s, want %s' % ([x['code'] for x in sm['status']], codes))
            if bad:
                failed += 1
                print('FAIL   ', name, bad)
            else:
                ok += 1
                print('ok     ', name)
    print('%d passed, %d failed' % (ok, failed))
    return 1 if failed else 0


def test_the_a4_boot_linker_maps_std1_as_its_twin():
    assert main() == 0


if __name__ == '__main__':
    sys.exit(main())
