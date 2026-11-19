# SPDX-License-Identifier: GPL-2.0-or-later
"""abs16 relocations on the +Drive: the A4's and the Rytm's boot linkers (mods/drive-loader-a4mk2/
dlink.c, and drive-loader-ar2's from 0.9) take them as the build's linker does (link.put_reloc: two
big-endian bytes, -0x8000 to 0xFFFF), and their descriptors say so (F_ABS16, the loader's export
drive_abs16), so the host twin (elekloader/drive.py, Build.rsize) takes them on such a build only.

Built for the computer and run against the twin, for each loader: abs16 in .run and in a
contribution places the same bytes in C and twin; one out of range is refused alike, in .run and
at the merge. On a build without F_ABS16 (drive-loader-ar2 0.8's) the loader refuses them as a
format error, and so does the twin.

    python tests/test_drive_abs16.py

Needs a C compiler (cc)."""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
sys.path.insert(0, HERE)

from elekloader import desc                                        # noqa: E402
from test_link_refusals import Bench                               # noqa: E402
import test_drive as TD                                            # noqa: E402
from test_drive_top_stage import boot, compare, states, twin      # noqa: E402

A4 = os.path.join(ROOT, 'mods', 'drive-loader-a4mk2')
AR2 = os.path.join(ROOT, 'mods', 'drive-loader-ar2')


def build_host(work, src, name):
    exe = os.path.join(work, name)
    subprocess.run(['cc', '-O1', '-Wall', '-DDL_HOST', '-DDL_PAYLOAD_SKIP', '-o', exe,
                    os.path.join(HERE, 'drive_link_host.c'), os.path.join(src, 'dlink.c'),
                    os.path.join(src, 'json.c'), os.path.join(src, 'dstore.c'), os.path.join(src, 'sha256.c'),
                    os.path.join(A4, 'skip.c')], check=True)
    return exe


class Built:
    """The made-up flashed build of the A4's loader kind (stage at the top, payload skip, and with
    `abs16` F_ABS16), as RAM for the host linker and as the twin's Build."""

    def __init__(self, B, abs16=True, kind=None):
        out, b = TD.built(B, kind or ('a4' if abs16 else 'skip'))
        self.B, self.out, self.build = B, out, b
        ddr_base, run_end = out.layout['ddr']
        o = out.layout['run_load'] - B.dev.main_load
        self.ram_base = ddr_base
        self.ram = bytes(out.image[o:o + run_end - ddr_base])
        self.desc_at = out.map['core_desc']


def mod(B, ident, relocs=(), contrib=()):
    """TD.dmod with 4 more bytes of .run for abs16 at 12 and 14, an abs symbol `k` (0x20), and an
    ev_tick entry with its own relocations after its abs32 to fn_<ident>."""
    d = TD.dmod(B, ident)
    d['sections']['.run'] = {'align': 4, 'len': 16, 'parts': [['hex', '4e714e714e750000' + '00' * 8]]}
    d['symbols']['k'] = ['abs', 0x20]
    d['relocs'] = d['relocs'] + [list(r) for r in relocs]
    d['contribute'] = [{'to': 'ev_tick', 'order': 50, 'data': '00000000',
                        'relocs': [[0, 'abs32', 'sym:fn_' + ident, 0]] + [list(r) for r in contrib]}]
    return d


def main():
    work = tempfile.mkdtemp()
    a4_exe = build_host(work, A4, 'drive_link_host_a4')
    ar2_exe = build_host(work, AR2, 'drive_link_host_ar2')
    fails = []

    def check(name, cond, info=''):
        print('%s %s %s' % ('ok  ' if cond else 'FAIL', name, info if not cond else ''))
        if not cond:
            fails.append(name)

    with Bench('analog-rytm-mk2') as B:
      for kind, exe, who in (('a4', a4_exe, ''), ('ar2', ar2_exe, 'drive-loader-ar2: ')):
        F = Built(B, kind=kind)

        def check_(name, cond, info=''):
            check(who + name, cond, info)
        check_('a loader exporting drive_abs16 sets the descriptor\'s F_ABS16 (bit 2)',
              F.build.d['flags'] & 7 == desc.F_STAGE_TOP | desc.F_PAYLOAD_SKIP | desc.F_ABS16 == 7
              and 'abs16' in F.build.rsize, F.build.d['flags'])
        check_('the descriptor\'s text names it', 'abs16 relocations' in desc.dump(F.build.d))

        def alike(name, mods, want_states):
            sm, ram = boot(exe, F, mods, work)
            r = twin(F, mods)
            check_(name + ': ' + str(want_states), states(sm) == want_states, sm['status'])
            bad = compare(F, sm, ram, r)
            check_(name + ': C and twin agree', not bad, bad)
            return sm, ram, r

        # abs16 in .run: an abs symbol + addend, and a negative value
        m = mod(B, 'k16', relocs=[['.run', 12, 'abs16', 'sym:k', 0x1000], ['.run', 14, 'abs16', 'abs', -2]])
        sm, ram, r = alike('abs16 in .run', [m], [(1, 0, 0)])
        run = ram[F.build.lo - F.ram_base:F.build.lo - F.ram_base + 16]
        check_('abs16 in .run: 0x1020 and 0xfffe, big-endian', struct.unpack('>HH', run[12:16]) == (0x1020, 0xFFFE),
              run.hex())

        # abs16 in a contribution: placed at the merge
        m = mod(B, 'k16c', contrib=[[2, 'abs16', 'sym:k', 0x7F00]])
        sm, ram, r = alike('abs16 in a contribution', [m], [(1, 0, 0)])
        t = r.tables['ev_tick']
        check_('abs16 in a contribution: the entry\'s low half is 0x7f20',
              any(t['bytes'][i:i + 4][2:] == b'\x7f\x20' for i in range(0, len(t['bytes']), 4)))

        # out of range: a .run address (the area is far above 0xFFFF), a value past 0xFFFF, one below -0x8000
        alike('abs16 to sec:.bss (an address) is refused', [mod(B, 'far', relocs=[['.run', 12, 'abs16', 'sec:.bss', 0]])],
              [(2, 18, 0)])
        alike('abs16 of 0x10000 is refused', [mod(B, 'big', relocs=[['.run', 12, 'abs16', 'abs', 0x10000]])],
              [(2, 18, 0)])
        alike('abs16 of -0x8001 is refused', [mod(B, 'low', relocs=[['.run', 12, 'abs16', 'abs', -0x8001]])],
              [(2, 18, 0)])
        alike('abs16 of -0x8000 loads', [mod(B, 'edge', relocs=[['.run', 12, 'abs16', 'abs', -0x8000]])], [(1, 0, 0)])
        # one out of range in a contribution: the merge can't place it, so the set is laid out again without it
        # (as a pc16 out of range there; 19, the loader's room)
        alike('abs16 out of range in a contribution is refused', [mod(B, 'cfar', contrib=[[2, 'abs16', 'sec:.run', 0]])],
              [(2, 19, 0)])
        alike('abs16 past the end of .run is refused', [mod(B, 'end', relocs=[['.run', 15, 'abs16', 'abs', 0]])],
              [(2, 5, 0)])

      # a build without drive_abs16 (an older loader, drive-loader-ar2 0.8's, whose dlink.c had no abs16):
      # the twin refuses it as that loader did, a format error
      R = Built(B, abs16=False)
      check('without drive_abs16: no F_ABS16, no abs16', not R.build.d['flags'] & desc.F_ABS16
          and 'abs16' not in R.build.rsize)
      for name, m in (('in .run', mod(B, 'r16', relocs=[['.run', 12, 'abs16', 'sym:k', 0]])),
                    ('in a contribution', mod(B, 'r16c', contrib=[[2, 'abs16', 'sym:k', 0]]))):
        r = twin(R, [m])
        got = [(c.state, c.code, c.arg) for c in r.choices]
        check('without F_ABS16: abs16 %s refused (format)' % name, got == [(2, 5, 0)], got)

    print('all passed' if not fails else '%d failed' % len(fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
