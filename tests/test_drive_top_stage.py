# SPDX-License-Identifier: GPL-2.0-or-later
"""Top-staging (decided 2026-11-11): the boot linker (mods/drive-loader-ar2/dlink.c with dl_stage_top) reads
each file into the +Drive area itself, pass 1 from its start and pass 2 at its top, and places the
mods by file length, the longest first, so that each file fits above the code placed before it.
Built for the computer and run against its twin (elekloader/drive.py, Build.stage_top): the same
refusals, area bytes and tables, every .bss and the free tail zero after the link.

    python tests/test_drive_top_stage.py

Needs a C compiler (cc). The voice fixture is the size of Sidecar's MD voice .elemod: a 1.06 MB
file holding about 0.52 MB of code (its .run as hex)."""
import hashlib
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

from elekloader import drive, link                                 # noqa: E402
from test_link_refusals import Bench                               # noqa: E402
import test_drive as TD                                            # noqa: E402
from test_drive_link import build_host                             # noqa: E402

KB = 1024
VOICE_RUN = 520 * KB                     # ~0.52 MB of code: a 1.06 MB file


def code_mod(B, ident, run_len, bss=8, pad=0):
    """A +Drive mod with run_len bytes of .run (a pattern after TD.dmod's 12 bytes), `bss` bytes of
    .bss and `pad` bytes of description."""
    d = TD.dmod(B, ident)
    body = bytearray()
    seed = ident.encode()
    while len(body) < run_len - 12:
        seed = hashlib.sha256(seed).digest()
        body += seed
    run = bytes.fromhex(d['sections']['.run']['parts'][0][1]) + bytes(body[:run_len - 12])
    d['sections'] = {'.run': {'align': 4, 'len': run_len, 'parts': [['hex', run.hex()]]},
                     '.bss': {'size': bss}}
    d['contribute'] = [{'to': 'ev_tick', 'order': 50, 'data': '00000000',
                        'relocs': [[0, 'abs32', 'sym:fn_' + ident, 0]]}]
    if pad:
        d['description'] = 'x' * pad
    return d


def raw(d):
    return (json.dumps(d, indent=1) + '\n').encode()


class Built:
    """The made-up flashed build with an area of about `area` bytes, as RAM for the host linker and
    as the twin's Build, both with the stage at the top."""

    def __init__(self, B, area):
        out, b = TD.built(B, area=area)
        self.B, self.out, self.build = B, out, b
        b.stage_top = True
        ddr_base, run_end = out.layout['ddr']
        o = out.layout['run_load'] - B.dev.main_load
        self.ram_base = ddr_base
        self.ram = bytes(out.image[o:o + run_end - ddr_base])
        self.desc_at = out.map['core_desc']


def boot(exe, F, mods, work, check=False):
    """The C linker (or with check, the MODS page's dl_check) on mods, in this /mods/active order,
    with the stage at the top. -> (summary, RAM after)."""
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
    env = dict(os.environ, DL_HOST_TOP='1', **({'DL_HOST_CHECK': '1'} if check else {}))
    r = subprocess.run([exe, ramf, '0x%x' % F.ram_base, stockf, '0x%x' % F.B.dev.main_load, '0x%x' % F.desc_at,
                        d, outf], capture_output=True, text=True, env=env)
    if r.returncode:
        raise AssertionError('host linker failed: %s %s' % (r.stdout, r.stderr))
    with open(outf, 'rb') as fh:
        return json.loads(r.stdout.splitlines()[0]), fh.read()


def twin(F, mods, deep=True):
    files = [(m['id'] + '.elemod', raw(m)) for m in mods]
    return drive.link(F.build, files) if deep else drive.check(F.build, files)


def states(sm):
    return [(x['state'], x['code'], x['arg']) for x in sm['status']]


def compare(F, sm, ram, r):
    """The C boot against the twin: -> [what differs]."""
    bad = []
    want = [(c.state, c.code, c.arg) for c in r.choices]
    if states(sm) != want:
        bad.append('status %s, twin %s' % (states(sm), want))

    def at(a, n):
        return ram[a - F.ram_base:a - F.ram_base + n]
    if at(F.build.lo, len(r.area)) != r.area:
        bad.append('area')
    for name, t in r.tables.items():
        if t['bytes'] is not None and at(t['address'], len(t['bytes'])) != t['bytes']:
            bad.append('table %s' % name)
    if r.live and sm['used'] != [F.build.lo, r.end]:
        bad.append('used %s, twin %s' % (sm['used'], [F.build.lo, r.end]))
    return bad


def peak(F, mods, order):
    """The highest point a file reaches with the mods' .run laid out in `order` (ids), each file
    read just under the area's end: > the area's end means it does not fit."""
    by = {m['id']: m for m in mods}
    a, worst = F.build.lo, 0
    for i in order:
        a = link._align(a, 4) + by[i]['sections']['.run']['len']
        worst = max(worst, a + len(raw(by[i])))
    return worst


def first_cut(F, mods):
    """The first cut's rule: every .run and .bss, then the largest file above them all."""
    code = sum(link._align(m['sections']['.run']['len'], 4) + link._align(m['sections']['.bss']['size'], 4)
               for m in mods)
    return F.build.lo + code + max(len(raw(m)) for m in mods)


def main():
    work = tempfile.mkdtemp()
    exe = build_host(work)
    fails = []

    def check(name, cond, info=''):
        print('%s %s %s' % ('ok  ' if cond else 'FAIL', name, info if not cond else ''))
        if not cond:
            fails.append(name)

    with Bench('analog-rytm-mk2') as B:
        F = Built(B, 4700 * KB)
        A = F.build.hi - F.build.lo
        check('the area is about 4.7 MB', abs(A - 4700 * KB) < 64 * KB, A)
        voice = code_mod(B, 'md-voice', VOICE_RUN, bss=256 * KB)
        check('the voice fixture: a 1.06 MB file, 0.52 MB of code',
              1040 * KB <= len(raw(voice)) <= 1090 * KB, len(raw(voice)))

        # the voice with two small mods either side of it by id: it goes to the area's start
        small = [code_mod(B, 'aa-small', 4 * KB), code_mod(B, 'zz-small', 4 * KB)]
        mods = [small[0], voice, small[1]]
        sm, ram = boot(exe, F, mods, work)
        r = twin(F, mods)
        check('voice and two small: all load', [x['state'] for x in sm['status']] == [1, 1, 1], sm['status'])
        check('voice and two small: C and twin agree', not compare(F, sm, ram, r), compare(F, sm, ram, r))
        check('voice and two small: the longest file is placed first, at the area start',
              r.base[('md-voice', '.run')] == F.build.lo
              and r.base[('aa-small', '.run')] > r.base[('md-voice', '.run')], r.base)
        o = r.base[('md-voice', '.bss')] - F.ram_base
        check("voice and two small: the voice's .bss is zero (a file was read over it)",
              not any(ram[o:o + 256 * KB]))
        lo, hi = r.end - F.ram_base, F.build.hi - F.ram_base
        check('voice and two small: the free tail is zero after the link', not any(ram[lo:hi]))
        check('voice and two small: inits and exports keep id order',
              [c.id for c in r.live] == ['aa-small', 'md-voice', 'zz-small'], [c.id for c in r.live])

        # a set that fits only when the longest file goes first
        n = 1
        while True:
            fill = [code_mod(B, 'fill-%02d' % i, 300 * KB) for i in range(n + 1)]
            if peak(F, fill + [voice], [voice['id']] + [m['id'] for m in fill]) > F.build.hi:
                break
            n += 1
        fill = [code_mod(B, 'fill-%02d' % i, 300 * KB) for i in range(n)]
        mods = fill + [voice]
        ids = [m['id'] for m in mods]
        check('%d fillers of 300 KB: id order would not fit, nor the first cut' % n,
              peak(F, mods, sorted(ids)) > F.build.hi and first_cut(F, mods) > F.build.hi,
              (peak(F, mods, sorted(ids)), first_cut(F, mods), F.build.hi))
        sm, ram = boot(exe, F, mods, work)
        r = twin(F, mods)
        check('%d fillers and the voice: all load with the longest file first' % n,
              all(x['state'] == 1 for x in sm['status']), sm['status'])
        check('%d fillers and the voice: C and twin agree' % n, not compare(F, sm, ram, r), compare(F, sm, ram, r))
        lo = r.end - F.ram_base
        check('%d fillers and the voice: the free tail is zero' % n, not any(ram[lo:hi]))

        # one filler more: room, the last living mod by id refused, as today
        more = code_mod(B, 'fill-%02d' % n, 300 * KB)
        mods = fill + [more, voice]
        sm, ram = boot(exe, F, mods, work)
        r = twin(F, mods)
        why = {c.id: (drive.CODES[c.code][0], c.arg) for c in r.choices if c.code}
        check('one filler more: md-voice (last by id) refused for room', list(why) == ['md-voice']
              and why['md-voice'][0] == 'room' and why['md-voice'][1] > 0, why)
        check('one filler more: C and twin agree', not compare(F, sm, ram, r), compare(F, sm, ram, r))

        # a file larger than the whole area: too-big in pass 1
        huge = code_mod(B, 'huge', 4 * KB, pad=A)
        mods = [huge, small[0]]
        sm, ram = boot(exe, F, mods, work)
        r = twin(F, mods)
        check('a file over the area: too-big, the other loads',
              states(sm)[0][1] == drive.CODE['too-big'] and states(sm)[1][0] == 1, sm['status'])
        check('a file over the area: C and twin agree', not compare(F, sm, ram, r), compare(F, sm, ram, r))

    # the MODS page's check lays out the same way (its own 512 KB buffer for pass 1): a 1 MB area,
    # scaled down so that every file fits the page's buffer
    with Bench('analog-rytm-mk2') as B:
        F = Built(B, 1024 * KB)
        big = code_mod(B, 'mm-big', 150 * KB)
        n = 1
        while True:
            fill = [code_mod(B, 'af-%02d' % i, 60 * KB) for i in range(n + 1)]
            if peak(F, fill + [big], [big['id']] + [m['id'] for m in fill]) > F.build.hi:
                break
            n += 1
        fill = [code_mod(B, 'af-%02d' % i, 60 * KB) for i in range(n)]
        for name, mods in (('fits only longest first', fill + [big]),
                           ('one more: room', fill + [code_mod(B, 'af-%02d' % n, 60 * KB), big])):
            sl, ram = boot(exe, F, mods, work)
            sc, _ = boot(exe, F, mods, work, check=True)
            rc = twin(F, mods, deep=False)
            check('1 MB area, %s: the check says what the boot does' % name,
                  [(x['state'], x['code'], x['arg']) for x in sc['status']] == states(sl)
                  == [(c.state, c.code, c.arg) for c in rc.choices], (states(sl), sc['status']))
            check('1 MB area, %s: C and twin agree' % name, not compare(F, sl, ram, twin(F, mods)))

    print('all passed' if not fails else '%d failed' % len(fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
