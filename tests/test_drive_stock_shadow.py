# SPDX-License-Identifier: GPL-2.0-or-later
"""Stock parts over a flashed site, on the +Drive. A built .elemod carries no stock bytes: the build
swaps each run that matches the stock OS for a stock part (an address and a length), which the boot
linker copies from the running main OS. Where the flashed build wrote over that address (a core's
site), the running OS holds the site's bytes, not stock's. The A4's loader, and the Rytm's from
drive-loader-ar2 0.8, export drive_stock_shadow, so the descriptor carries the shadow (stock's bytes
of every flashed site) and F_STOCK_SHADOW (flags bit 6), and dlink.c (shadow_over) and the host twin
(Build.stock_bytes) lay the shadow over each stock part. (Without it the Rytm's boot linked
sidecar 0.7 from the +Drive with core-ar2 0.14's jumps in six of its stock parts, and the boot hung.)

Built for the computer and run against the twin, on a made-up image (no real stock bytes): a stock
part over the flashed jmp site places the image's own bytes in C and twin alike, from a running OS
that holds the site, with each loader's dlink.c (the A4's, and the Rytm's on a build of its kind,
no abs16); the build stays no TELOS (sites keep the A4's checks); a build without the export
carries no shadow and copies the running OS's bytes.

    python tests/test_drive_stock_shadow.py

Needs a C compiler (cc)."""
import os
import sys
import tempfile
from types import SimpleNamespace

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
sys.path.insert(0, HERE)

from elekloader import desc, drive, link                           # noqa: E402
from test_link_refusals import Bench                               # noqa: E402
import test_drive as TD                                            # noqa: E402
from test_drive_top_stage import boot, compare, states, twin      # noqa: E402
from test_drive_abs16 import build_host                            # noqa: E402

A4 = os.path.join(ROOT, 'mods', 'drive-loader-a4mk2')
AR2 = os.path.join(ROOT, 'mods', 'drive-loader-ar2')
SITE = 0x2000                                   # flash-a's jmp site (test_drive.flashed), 6 bytes


class Built:
    """The made-up flashed build of the A4's loader kind (stage at the top, payload skip, abs16),
    with or without drive_stock_shadow; RAM for the host linker, the running main OS (the built
    image, its sites written) and the twin's Build."""

    def __init__(self, B, shadow=True, abs16=True):
        mods = TD.flashed(B, stage_top=True, payload_skip=True, abs16=abs16)
        if shadow:
            loader = next(d for d in mods if d['id'] == 'drive-loader')
            loader['symbols'][desc.STOCK_SHADOW] = ['abs', 1]
            loader['exports'].append(desc.STOCK_SHADOW)
        out = link.link([link.Mod2(d, d['id']) for d in mods], B.img)
        self.out, self.build = out, drive.load_build(out.image)
        ddr_base, run_end = out.layout['ddr']
        o = out.layout['run_load'] - B.dev.main_load
        self.ram_base = ddr_base
        self.ram = bytes(out.image[o:o + run_end - ddr_base])
        self.desc_at = out.map['core_desc']
        # the host linker reads stock parts from "stock.bin": give it the OS as it runs, sites written
        self.B = SimpleNamespace(img=bytes(out.image[:len(B.img)]), dev=B.dev)


def mod(B, ident, lo, n):
    """test_drive.dmod with a stock part of n bytes at image offset lo after its 12 bytes of .run."""
    d = TD.hello(B)
    d['id'] = ident
    d['symbols'] = {'fn_' + ident: ['.run', 0], ident + '_count': ['.bss', 0]}
    d['exports'] = [ident + '_count']
    d['relocs'] = [['.run', 8, 'abs32', 'sym:core_fn', 0], ['.run', 6, 'pc16', 'sym:fn_' + ident, 0]]
    d['contribute'] = [{'to': 'ev_tick', 'order': 50, 'data': '00000000',
                        'relocs': [[0, 'abs32', 'sym:fn_' + ident, 0]]}]
    d['sections']['.run'] = {'align': 4, 'len': 12 + n,
                             'parts': [['hex', '4e714e714e750000' + '00000000'],
                                       ['stock', '0x%08x' % B.at(lo), n]]}
    return d


def main():
    work = tempfile.mkdtemp()
    exes = (('', build_host(work, A4, 'drive_link_host_a4'), True),
            ('drive-loader-ar2: ', build_host(work, AR2, 'drive_link_host_ar2'), False))
    fails = []

    def check(name, cond, info=''):
        print('%s %s %s' % ('ok  ' if cond else 'FAIL', name, info if not cond else ''))
        if not cond:
            fails.append(name)

    with Bench('analog-rytm-mk2') as B:
        for who, exe, abs16 in exes:
            F = Built(B, abs16=abs16)
            fl = F.build.d['flags']
            check(who + 'a loader exporting drive_stock_shadow sets F_STOCK_SHADOW (bit 6)',
                  desc.F_STOCK_SHADOW == 64 and fl & desc.F_STOCK_SHADOW, fl)
            check(who + 'the descriptor carries the shadow: the flashed site, as the image had it',
                  F.build.shadow == [(B.at(SITE), B.img[SITE:SITE + 6])], F.build.shadow)
            check(who + 'with F_STOCK_SHADOW the build is no TELOS', not F.build.telos)
            check(who + 'the descriptor\'s text names it', 'stock parts through the shadow' in desc.dump(F.build.d))
            check(who + 'the running OS holds the site there, not the image\'s bytes',
                  F.B.img[SITE:SITE + 6] != B.img[SITE:SITE + 6])

            def place(F, name, lo, n):
                m = mod(B, name, lo, n)
                sm, ram = boot(exe, F, [m], work)
                r = twin(F, [m])
                check(who + name + ': loads', states(sm) == [(1, 0, 0)], sm['status'])
                bad = compare(F, sm, ram, r)
                check(who + name + ': C and twin agree', not bad, bad)
                a = F.build.lo - F.ram_base + 12
                return ram[a:a + n]

            # a stock part over the whole site, one across its start, one inside it, one beside it
            for name, lo, n in (('over', SITE, 6), ('across', SITE - 4, 8), ('inside', SITE + 2, 2),
                                ('beside', SITE + 8, 8)):
                got = place(F, 'stock-' + name, lo, n)
                check(who + 'stock-%s: the image\'s own bytes' % name, got == B.img[lo:lo + n])

            # without the export: no shadow, no flag, and the copy is the running OS's (as before)
            R = Built(B, shadow=False, abs16=abs16)
            check(who + 'without drive_stock_shadow: no flag, no shadow', not R.build.d['flags'] & desc.F_STOCK_SHADOW
                  and not R.build.shadow and not R.build.telos)
            got = place(R, 'plain-over', SITE, 6)
            check(who + 'plain-over: the running OS\'s bytes (the site)', got == R.B.img[SITE:SITE + 6])

    print('all passed' if not fails else '%d failed' % len(fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
