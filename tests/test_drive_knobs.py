# SPDX-License-Identifier: GPL-2.0-or-later
"""+Drive knob ids (docs/DRIVE.md, "Knob ids on the +Drive"): the A4 loader's boot linker
(mods/drive-loader-a4mk2/dlink.c) gives each +Drive mod's "knobs" a run of ids in 0x4000..0x7fff, kept
from boot to boot in /mods/.store (format 3), as store.knobs keeps them in the lockfile, and refuses a
mod whose entries name ids the core's, a flashed module's or an earlier mod's (link.id_problems). Its
descriptor says so (F_KNOBS, the loader's export drive_knobs; the flashed ids as claims of kind 4), so
the host twin (elekloader/drive.py) does the same on that build only.

Built for the computer and run on a made-up Analog Four MKII build against the twin, with /mods/.store
carried from one boot to the next: the same records, area bytes, tables and .store.

    python tests/test_drive_knobs.py

Needs a C compiler (cc)."""
import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
sys.path.insert(0, HERE)

from elekloader import desc, drive, drive_store as ds, link                 # noqa: E402
from test_link_refusals import Bench                                         # noqa: E402
import test_drive as TD                                                      # noqa: E402
from test_drive_abs16 import A4, build_host                                  # noqa: E402


def flashed(B, knobs=True):
    """The A4 loader's kind of build, keeping +Drive storage, with core_params and core_knob_looks,
    a flashed module whose entry takes ids 0x300..0x306, and (knobs) a loader exporting drive_knobs."""
    mods = TD.flashed(B, store=True, stage_top=True, payload_skip=True, abs16=True, collections=True)
    core, loader = mods[0], mods[1]
    core['collections'].update({'core_params': {'entry': 4}, 'core_knob_looks': {'entry': 4}})
    if knobs:
        loader['symbols']['drive_knobs'] = ['abs', 1]
        loader['exports'].append('drive_knobs')
    fx = B.other('fixed-ae')
    fx['sections']['.run'] = {'align': 4, 'len': 8, 'parts': [['hex', '%08x%08x' % (0x300, 0x306)]]}
    fx['symbols'] = {'e': ['.run', 0]}
    fx['contribute'] = [{'to': 'core_params', 'data': '00000000', 'relocs': [[0, 'abs32', 'sym:e', 0]]}]
    return mods + [fx]


class Built:
    def __init__(self, B, knobs=True):
        out = link.link([link.Mod2(d, d['id']) for d in flashed(B, knobs)], B.img)
        self.B, self.out = B, out
        self.build = drive.load_build(out.image)
        ddr_base, run_end = out.layout['ddr']
        o = out.layout['run_load'] - B.dev.main_load
        self.ram_base = ddr_base
        self.ram = bytes(out.image[o:o + run_end - ddr_base])
        self.desc_at = out.map['core_desc']


def km(B, ident, n=0, fixed=None, table='core_params', refs=None, entry=True):
    """A +Drive mod with 24 bytes of .run and an entry in `table` pointing at .run+12: with n, "knobs": n
    and .run+12..+24 relocated to __knobs (first, last, and an abs16 of first + 1), so the entry names its
    run; with fixed (a, b), its own ids a..b there."""
    d = TD.dmod(B, ident)
    d['sections']['.run'] = {'align': 4, 'len': 24, 'parts': [['hex', '4e714e714e750000' + '00' * 16]]}
    d['symbols']['e_' + ident] = ['.run', 12]
    if n:
        d['knobs'] = n
        d['imports'] = d['imports'] + ['__knobs']
        d['relocs'] = d['relocs'] + (refs if refs is not None else
                                     [['.run', 12, 'abs32', 'sym:__knobs', 0], ['.run', 16, 'abs32', 'sym:__knobs', n - 1],
                                      ['.run', 20, 'abs16', 'sym:__knobs', 1]])
    if fixed:
        a, b = fixed
        d['sections']['.run']['parts'] = [['hex', '4e714e714e750000' + '00000000'
                                           + struct.pack('>ii', a, b).hex() + '00' * 4]]
    d['contribute'] = [{'to': table, 'order': 50, 'data': '00000000', 'relocs': [[0, 'abs32', 'sym:e_' + ident, 0]]}
                       ] if entry else []
    return d


def raw(m):
    return (json.dumps(m, indent=1) + '\n').encode()


def boot(exe, F, mods, prev, work):
    """One boot of the C linker, the stage at the top: (summary, RAM after, .store after)."""
    d = tempfile.mkdtemp(dir=work)
    lines = []
    for m in mods:
        name = m['id'] + '.elemod'
        with open(os.path.join(d, name), 'wb') as fh:
            fh.write(raw(m))
        lines.append('%s %08x' % (name, zlib.crc32(raw(m)) & 0xFFFFFFFF))
    with open(os.path.join(d, 'active'), 'w') as fh:
        fh.write('\n'.join(lines) + '\n')
    if prev is not None:
        with open(os.path.join(d, '.store'), 'wb') as fh:
            fh.write(prev)
    ramf, stockf, outf = (os.path.join(d, x) for x in ('ram.bin', 'stock.bin', 'out.bin'))
    with open(ramf, 'wb') as fh:
        fh.write(F.ram)
    with open(stockf, 'wb') as fh:
        fh.write(F.B.img)
    r = subprocess.run([exe, ramf, '0x%x' % F.ram_base, stockf, '0x%x' % F.B.dev.main_load, '0x%x' % F.desc_at,
                        d, outf], capture_output=True, text=True, env=dict(os.environ, DL_HOST_TOP='1'))
    if r.returncode:
        raise AssertionError('host linker failed: %s %s' % (r.stdout, r.stderr))
    with open(outf, 'rb') as fh:
        ram = fh.read()
    st = None
    if os.path.exists(outf + '.store'):
        with open(outf + '.store', 'rb') as fh:
            st = fh.read()
    return json.loads(r.stdout.splitlines()[0]), ram, st


def compare(F, sm, ram, st, r):
    """The C boot against the twin: -> [what differs]."""
    bad = []
    got = [(x['state'], x['code'], x['detail']) for x in sm['status']]
    want = [(c.state, c.code, drive._cut(c.detail, drive.DETAIL).decode('utf-8', 'replace')) for c in r.choices]
    if got != want:
        bad.append('status %s, twin %s' % (got, want))

    def at(a, n):
        return ram[a - F.ram_base:a - F.ram_base + n]
    if at(F.build.lo, len(r.area)) != r.area:
        bad.append('area')
    for name, t in r.tables.items():
        if t['bytes'] is not None and at(t['address'], len(t['bytes'])) != t['bytes']:
            bad.append('table %s' % name)
    if r.placed is None or st != r.placed.bytes:
        bad.append('.store')
    return bad


def main():
    work = tempfile.mkdtemp()
    exe = build_host(work, A4, 'drive_link_host_a4')
    fails = []

    def check(name, cond, info=''):
        print('%s %s %s' % ('ok  ' if cond else 'FAIL', name, info if not cond else ''))
        if not cond:
            fails.append(name)

    with Bench('analog-four-mk2') as B:
        F = Built(B)
        b = F.build
        check('a loader exporting drive_knobs sets F_KNOBS (bit 5), and the flashed ids are claims of kind 4',
              b.d['flags'] & desc.F_KNOBS and b.knobs and (0x300, 0x306, 'fixed-ae') in b.knob_ids,
              (b.d['flags'], b.knob_ids))
        check('the descriptor\'s text names it', '+Drive knob ids' in desc.dump(b.d))

        def alike(name, mods, prev=None):
            sm, ram, st = boot(exe, F, mods, prev, work)
            r = drive.link(b, [(m['id'] + '.elemod', raw(m)) for m in mods], prev)
            bad = compare(F, sm, ram, st, r)
            check(name + ': C and twin agree', not bad, bad)
            return sm, ram, st, r

        def why(r):
            return {c.id: (drive.CODES[c.code][0] if c.code else 'ok', c.detail) for c in r.choices}

        def run_ids(r, ram, ident):
            """The first and last id, and the abs16, that ident's .run holds at +12."""
            o = r.base[(ident, '.run')] + 12 - F.ram_base
            return struct.unpack('>IIH', ram[o:o + 10])

        # boot 1: two runs from 0x4000, and a mod naming its own ids
        sm, ram, st1, r = alike('boot 1', [km(B, 'ka', 4), km(B, 'kb', 2), km(B, 'fx', fixed=(0x400, 0x405))])
        check('boot 1: all load', [x['state'] for x in sm['status']] == [1, 1, 1], why(r))
        check('boot 1: ka 0x4000..0x4003, kb 0x4004..0x4005', r.knobs == {'ka': [0x4000, 4], 'kb': [0x4004, 2]},
              r.knobs)
        check('boot 1: __knobs relocated in .run', run_ids(r, ram, 'ka') == (0x4000, 0x4003, 0x4001)
              and run_ids(r, ram, 'kb') == (0x4004, 0x4005, 0x4005), (run_ids(r, ram, 'ka'), run_ids(r, ram, 'kb')))
        check('boot 1: the .store is format 3 with the runs', st1 is not None and st1[4:6] == b'\x00\x03'
              and ds.decode(st1)['knobs'] == [{'mod': 'ka', 'first': 0x4000, 'count': 4},
                                              {'mod': 'kb', 'first': 0x4004, 'count': 2}], st1 and ds.decode(st1))

        # boot 2: kb keeps its run, ka's is freed and kept for it, kc takes new ids
        sm, ram, st2, r = alike('boot 2', [km(B, 'kb', 2), km(B, 'kc', 3)], st1)
        check('boot 2: kb keeps 0x4004, kc gets 0x4006', r.knobs == {'kb': [0x4004, 2], 'kc': [0x4006, 3]}, r.knobs)
        check('boot 2: ka\'s run freed', ds.decode(st2)['freed_knobs'] == [{'mod': 'ka', 'first': 0x4000, 'count': 4}],
              ds.decode(st2))

        # boot 3: ka back where it was; a count that changed gives a new run
        sm, ram, st3, r = alike('boot 3', [km(B, 'ka', 4), km(B, 'kb', 3), km(B, 'kc', 3)], st2)
        check('boot 3: ka back at 0x4000, kb past the rest', r.knobs == {'ka': [0x4000, 4], 'kb': [0x4009, 3],
                                                                          'kc': [0x4006, 3]}, r.knobs)

        # a run that clashes with ids a mod names itself moves
        sm, ram, st4, r = alike('a fixed id over a kept run', [km(B, 'ka', 4), km(B, 'fz', fixed=(0x4001, 0x4001))], st3)
        check('the run moves past it', r.knobs['ka'][0] not in range(0x3ffd, 0x4002), r.knobs)

        # refusals: the core's ids, below 0x200 in core_knob_looks, a flashed module's, an earlier mod's
        sm, ram, st, r = alike('fixed ids refused', [
            km(B, 'f1', fixed=(0x150, 0x151)), km(B, 'f2', fixed=(0x302, 0x302)),
            km(B, 'f3', fixed=(0x500, 0x501)), km(B, 'f4', fixed=(0x501, 0x502)),
            km(B, 'f5', fixed=(0x10, 0x11), table='core_knob_looks'), km(B, 'f6', fixed=(0x10, 0x11)),
            km(B, 'f7', fixed=(-2, 0x210))])
        check('fixed ids refused: details', why(r) == {
            'f1': ('ids', "0x150-0x151 are core's"), 'f2': ('ids', "0x302-0x302 are fixed-ae's"),
            'f3': ('ok', ''), 'f4': ('ids', "0x501-0x501 are f3's"), 'f5': ('ids', '0x10-0x11 are below 0x200'),
            'f6': ('ok', ''), 'f7': ('ids', "-0x2-0x210 are core's")}, why(r))

        # the range runs out: 16 runs of 1024 fill 0x4000..0x7fff, a 17th has none
        many = [km(B, 'n%02d' % i, 1024, entry=False) for i in range(17)]
        sm, ram, st, r = alike('the range runs out', many)
        check('the range runs out: the last refused', why(r)['n16'] == ('ids', 'run of 1024: none free')
              and all(why(r)['n%02d' % i] == ('ok', '') for i in range(16)), why(r))
        # a freed run goes out again once the never-used ids are gone
        sm, ram, st5, r = alike('freed runs out again',
                                [km(B, 'n%02d' % i, 1024, entry=False) for i in range(1, 16)] + [km(B, 'nzz', 1024, entry=False)], st)
        check('freed runs out again: n00\'s ids to nzz', r.knobs.get('nzz') == [0x4000, 1024], r.knobs)

        # form: __knobs only abs32/abs16 below "knobs"; "knobs" 0..1024; __knobs no mod's symbol
        bad_add = km(B, 'r1', 2, refs=[['.run', 12, 'abs32', 'sym:__knobs', 2]])
        bad_pc = km(B, 'r2', 2, refs=[['.run', 12, 'pc16', 'sym:__knobs', 0]])
        too_many = km(B, 'r3', 1025)
        own_sym = km(B, 'r4', 1)
        own_sym['symbols']['__knobs'] = ['abs', 5]
        no_knobs = km(B, 'r5')
        no_knobs['imports'] = no_knobs['imports'] + ['__knobs']
        sm, ram, st, r = alike('form', [bad_add, bad_pc, too_many, own_sym, no_knobs])
        check('form: refused', [why(r)[x][0] for x in ('r1', 'r2', 'r3', 'r4', 'r5')]
              == ['format', 'format', 'format', 'format', 'import'], why(r))

        # a bad .store reads as none: runs from 0x4000 again
        sm, ram, st, r = alike('a bad .store', [km(B, 'kb', 2)], st1[:-1] + bytes([st1[-1] ^ 1]))
        check('a bad .store: a fresh run', r.knobs == {'kb': [0x4000, 2]}, r.knobs)

        # nothing with knobs: every run is freed and kept for its mod
        sm, ram, st6, r = alike('no knobs mods', [km(B, 'fx', fixed=(0x400, 0x405))], st2)
        s6 = ds.decode(st6)
        check('no knobs mods: the runs freed, kept', s6['knobs'] == [] and s6['next_knob'] == 0x4009
              and [x['mod'] for x in s6['freed_knobs']] == ['ka', 'kb', 'kc'], s6)

        # a build without drive_knobs: no F_KNOBS; "knobs" mods' __knobs is an import nothing gives
        N = Built(B, knobs=False)
        check('without drive_knobs: no F_KNOBS', not N.build.d['flags'] & desc.F_KNOBS and not N.build.knobs)
        sm, ram, st = boot(exe, N, [km(B, 'ka', 4)], None, work)[:3]
        r = drive.link(N.build, [('ka.elemod', raw(km(B, 'ka', 4)))])
        check('without drive_knobs: refused as an import, C and twin agree', why(r)['ka'][0] == 'import'
              and not compare(N, sm, ram, st, r), (why(r), compare(N, sm, ram, st, r)))

    print('all passed' if not fails else '%d failed' % len(fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
