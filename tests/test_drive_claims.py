# SPDX-License-Identifier: GPL-2.0-or-later
"""Contributions' claims on the +Drive: the A4's and the Rytm's boot linkers (mods/drive-loader-a4mk2/
dlink.c, and drive-loader-ar2's from 0.9) take
a contribution's "claims" ([[lo, hi], ...], byte ranges of the main OS) on a build whose descriptor
has F_CLAIMS (the loader's export drive_claims), and the descriptor's site map then carries the
flashed contributions' claims (kind 7, SITE_CLAIM). A claim writes nothing; it is refused `clash`
as a site is (link.py check(): claims and sites must not share a byte): over the flashed build's
site map (any kind, the flashed claims too) or a pre-link range, and the later in id order of a
claim and another mod's site or claim. A copied block may share bytes with a claim.

Each loader is built for the computer and run against the twin (elekloader/drive.py) on
tests/test_drive.py's made-up build with a flashed claim: the same states, refusals, details, area
bytes and tables.

    python tests/test_drive_claims.py

Needs a C compiler (cc)."""
import dataclasses
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
sys.path.insert(0, HERE)

from elekloader import desc, drive                                  # noqa: E402
from test_link_refusals import Bench                               # noqa: E402
import test_drive as TD                                            # noqa: E402
from test_drive_std import Std, boot, build_host, compare, raw     # noqa: E402
from test_drive_abs16 import AR2, build_host as build_host_at      # noqa: E402

C = drive.CODE['clash']
CORE, MOD, PRE = drive.SITE_CORE << 8, drive.SITE_MOD << 8, drive.SITE_PRELINK << 8


def cm(B, ident, claims, **extra):
    """A +Drive mod whose ev_tick entry claims `claims`."""
    return TD.dmod(B, ident, contribute=[{'to': 'ev_tick', 'order': 50, 'data': '00000000',
                                          'relocs': [[0, 'abs32', 'sym:fn_' + ident, 0]], 'claims': claims}],
                   **extra)


def sets(B):
    a = B.at
    block = {'lo': a(0x1000), 'hi': a(0x1100), 'to': 0}
    big = a(len(B.img))                         # the main OS's end (the boot copier follows)
    # (name, mods in /mods/active order, [(state, code, arg, detail)] by line)
    return [
        ('a claim loads', [cm(B, 'a-mod', [[a(0x1000), a(0x1010)]])], [(1, 0, 0, '')]),
        ('two claims, hex strings', [cm(B, 'a-mod', [['0x%08x' % a(0x1000), '0x%08x' % a(0x1004)],
                                                     [a(0x1100), a(0x1104)]])], [(1, 0, 0, '')]),
        ('over a flashed site', [cm(B, 'a-mod', [[a(0x2002), a(0x2003)]])],
         [(2, C, CORE, '%08x flash-a' % a(0x2002))]),
        ('over a flashed claim', [cm(B, 'a-mod', [[a(0x2808), a(0x2820)]])],
         [(2, C, CORE, '%08x flash-a' % a(0x2808))]),
        ('next to the flashed claim', [cm(B, 'a-mod', [[a(0x2810), a(0x2820)]])], [(1, 0, 0, '')]),
        ('in a pre-link range', [cm(B, 'a-mod', [[a(0x300c), a(0x3010)]])], [(2, C, PRE, '%08x' % a(0x300c))]),
        ('past the pre-link range', [cm(B, 'a-mod', [[a(0x3010), a(0x3014)]])], [(1, 0, 0, '')]),
        ('a +Drive site over a flashed claim', [TD.hello(B, sites=[B.jmp_site(a(0x280a), 'fn_drive-hello')])],
         [(2, C, CORE, '%08x flash-a' % a(0x280a))]),
        ('two claims share a byte: the later by id goes',
         [cm(B, 'z-mod', [[a(0x1008), a(0x1010)]]), cm(B, 'a-mod', [[a(0x1000), a(0x1009)]])],
         [(2, C, MOD | 1, '%08x' % a(0x1008)), (1, 0, 0, '')]),
        ('a claim over an earlier mod\'s site',
         [cm(B, 'z-mod', [[a(0x1004), a(0x1008)]]), TD.dmod(B, 'a-mod', sites=[B.jmp_site(a(0x1000), 'fn_a-mod')])],
         [(2, C, MOD | 1, '%08x' % a(0x1004)), (1, 0, 0, '')]),
        ('a site over an earlier mod\'s claim',
         [cm(B, 'a-mod', [[a(0x1004), a(0x1008)]]), TD.dmod(B, 'z-mod', sites=[B.jmp_site(a(0x1000), 'fn_z-mod')])],
         [(1, 0, 0, ''), (2, C, MOD, '%08x' % a(0x1000))]),
        ('a claim and a copied block share bytes',
         [TD.dmod(B, 'a-mod', copied=[block]), cm(B, 'z-mod', [[a(0x1000), a(0x1010)]])],
         [(1, 0, 0, ''), (1, 0, 0, '')]),
        ('a copied block and a claim share bytes',
         [cm(B, 'a-mod', [[a(0x1000), a(0x1010)]]), TD.dmod(B, 'z-mod', copied=[block])],
         [(1, 0, 0, ''), (1, 0, 0, '')]),
        ('"claims": [] is none', [cm(B, 'a-mod', [])], [(1, 0, 0, '')]),
        ('"claims": null is none', [cm(B, 'a-mod', None)], [(1, 0, 0, '')]),
        ('lo == hi', [cm(B, 'a-mod', [[a(0x1000), a(0x1000)]])], [(2, 5, 0, '')]),
        ('lo > hi', [cm(B, 'a-mod', [[a(0x1004), a(0x1000)]])], [(2, 5, 0, '')]),
        ('a negative lo', [cm(B, 'a-mod', [[-4, a(0x1000)]])], [(2, 5, 0, '')]),
        ('below the main OS', [cm(B, 'a-mod', [[a(0) - 4, a(0x10)]])], [(2, 5, 0, '')]),
        ('past the main OS', [cm(B, 'a-mod', [[big - 4, big + 4]])], [(2, 5, 0, '')]),
        ('one number', [cm(B, 'a-mod', [[a(0x1000)]])], [(2, 5, 0, '')]),
        ('three numbers', [cm(B, 'a-mod', [[a(0x1000), a(0x1004), 0]])], [(2, 5, 0, '')]),
        ('not a list', [cm(B, 'a-mod', {'lo': a(0x1000)})], [(2, 5, 0, '')]),
    ]


def main():
    ok = failed = 0

    def check(name, bad):
        nonlocal ok, failed
        if bad:
            failed += 1
            print('FAIL   ', name, bad)
        else:
            ok += 1
            print('ok     ', name)

    with Bench('analog-rytm-mk2') as B, tempfile.TemporaryDirectory() as work:
        exes = (('', build_host(work)), ('drive-loader-ar2: ', build_host_at(work, AR2, 'drive_link_host_ar2')))
        B.dev.releases['SYN'] = B.rel = dataclasses.replace(
            B.rel, prelink=((B.at(0x3000), B.at(0x3010), desc.PRE_CODE, 'test'),))
        F = Std(B, 'claims')
        d = F.build.d
        check('drive_claims sets F_CLAIMS, and the site map has the flashed claim',
              [] if d['flags'] & desc.F_CLAIMS and F.build.take_claims
              and (B.at(0x2800), B.at(0x2810), desc.SITE_CLAIM, 2) in d['sites'] else [d['flags'], d['sites']])
        check('the descriptor\'s text names them',
              [] if 'claims taken' in desc.dump(d) and 'claim by module 2' in desc.dump(d) else [desc.dump(d)])
        G = Std(B, None)
        for who, exe in exes:
            for name, mods, want in sets(B):
                sm, ram = boot(exe, F, mods, work)
                r = drive.link(F.build, [(m['id'] + '.elemod', raw(m)) for m in mods])
                bad = compare(F, sm, ram, r)
                got = [(x['state'], x['code'], x['arg'], x.get('detail', '')) for x in sm['status']]
                if got != want:
                    bad.append('got %s, want %s' % (got, want))
                check(who + name, bad)
            # a claim writes nothing: the area and tables match the same mod without it
            m = cm(B, 'a-mod', [[B.at(0x1000), B.at(0x1010)]])
            plain = dict(m, contribute=[dict(m['contribute'][0], claims=[])])
            _sm, ram1 = boot(exe, F, [m], work)
            _sm, ram0 = boot(exe, F, [plain], work)
            check(who + 'a claim writes nothing', [] if ram1 == ram0 else ['RAM differs'])
            # without F_CLAIMS (an older build, drive-loader-ar2 0.8's): refused as a feature, the
            # flashed claim not in the site map
            sm, ram = boot(exe, G, [m], work)
            r = drive.link(G.build, [('a-mod.elemod', raw(m))])
            bad = compare(G, sm, ram, r)
            if [(x['code'], x.get('detail', '')) for x in sm['status']] != [(7, 'claims')]:
                bad.append(sm['status'])
            if G.build.d['flags'] & desc.F_CLAIMS or any(k == desc.SITE_CLAIM for _a, _b, k, _m in G.build.d['sites']):
                bad.append('flag or map')
            check(who + 'without F_CLAIMS: refused as the feature claims', bad)
    print('%d passed, %d failed' % (ok, failed))
    return 1 if failed else 0


def test_the_boot_linkers_take_claims_as_their_twin():
    assert main() == 0


if __name__ == '__main__':
    sys.exit(main())
