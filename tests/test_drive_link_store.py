# SPDX-License-Identifier: GPL-2.0-or-later
"""+Drive storage through the whole boot link: the Drive Loader's linker (mods/drive-loader-ar2/dlink.c,
dstore.c) built for the computer and run on a made-up Analog Rytm MKII build whose core keeps +Drive
storage, step by step with /mods/.store carried from one boot to the next, against the twin
(elekloader/drive.py, `elekloader link --device-layout --store`): the same refusals, area bytes (each
storage import relocated to its field's descriptor), tables, core_store_gens, core_lfo_dsts and .store
bytes.

    python tests/test_drive_link_store.py

Needs a C compiler (cc). The mods are duck, fx-sends, lfo-rnd and voice-dac's storage declarations
(js/test/drivestore-vectors.json), whose only imports are their storage fields."""
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

from elekloader import drive, link, store as mstore                # noqa: E402
from test_link_refusals import Bench                               # noqa: E402
import test_drive as TD                                            # noqa: E402
from test_drive_link import build_host                             # noqa: E402

VECTORS = os.path.join(ROOT, 'js', 'test', 'drivestore-vectors.json')
TABLES = drive.STORE_TABLES


def storage_mod(B, doc, version='1'):
    """A +Drive mod with doc's storage: .run holds one word per field, each an abs32 to the field."""
    syms = [f['sym'] for f in doc['storage']]
    n = 4 * len(syms)
    return {'elemod': 2, 'id': doc['id'], 'version': version, 'target': B.target, 'requires': ['core'],
            'sections': {'.run': {'align': 4, 'len': n, 'parts': [['hex', '00' * n]]}},
            'symbols': {}, 'exports': [], 'imports': syms, 'storage': doc['storage'],
            'relocs': [['.run', 4 * i, 'abs32', 'sym:' + s, 0] for i, s in enumerate(syms)]}


class Built:
    def __init__(self, B):
        out = link.link([link.Mod2(d, d['id']) for d in TD.flashed(B, store=True)], B.img)
        self.B, self.out = B, out
        self.build = drive.load_build(out.image)
        ddr_base, run_end = out.layout['ddr']
        o = out.layout['run_load'] - B.dev.main_load
        self.ram_base = ddr_base
        self.ram = bytes(out.image[o:o + run_end - ddr_base])
        self.desc_at = out.map['core_desc']


def boot(exe, F, mods, prev, work):
    """One boot of the C linker: (summary, RAM after, .store after)."""
    d = tempfile.mkdtemp(dir=work)
    lines = []
    for m in mods:
        name = m['id'] + '.elemod'
        data = (json.dumps(m, indent=1) + '\n').encode()
        with open(os.path.join(d, name), 'wb') as fh:
            fh.write(data)
        lines.append('%s %08x' % (name, zlib.crc32(data) & 0xFFFFFFFF))
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
                        d, outf], capture_output=True, text=True)
    if r.returncode:
        raise AssertionError('host linker failed: %s %s' % (r.stdout, r.stderr))
    with open(outf, 'rb') as fh:
        ram = fh.read()
    st = None
    if os.path.exists(outf + '.store'):
        with open(outf + '.store', 'rb') as fh:
            st = fh.read()
    return json.loads(r.stdout), ram, st


def twin(F, mods, prev):
    files = [(m['id'] + '.elemod', (json.dumps(m, indent=1) + '\n').encode()) for m in mods]
    return drive.link(F.build, files, prev)


def compare(F, sm, ram, st, r):
    """The C boot against the twin: -> [what differs]."""
    bad = []
    states = [(x['state'], x['code'], x['detail']) for x in sm['status']]
    want = [(c.state, c.code, drive._cut(c.detail, drive.DETAIL).decode('utf-8', 'replace')) for c in r.choices]
    if states != want:
        bad.append('status %s, twin %s' % (states, want))

    def at(a, n):
        return ram[a - F.ram_base:a - F.ram_base + n]
    if at(F.build.lo, len(r.area)) != r.area:
        bad.append('area')
    for name, t in r.tables.items():
        if at(t['address'], len(t['bytes'])) != t['bytes']:
            bad.append('table %s' % name)
    if r.placed is None or st != r.placed.bytes:
        bad.append('.store')
    if r.gens is None or at(F.build.gens_at, 6) != r.gens:
        bad.append('core_store_gens')
    if r.placed is not None:                    # the core's LFO fields the +Drive placed; else the .bss's 0
        for i, s in enumerate(('sound', 'kit')):
            d = r.placed.lfo_dsts.get(s, 0)
            if at(F.build.dsts_at + 4 * i, 4) != d.to_bytes(4, 'big'):
                bad.append('core_lfo_dsts %s' % s)
    return bad


def main():
    work = tempfile.mkdtemp()
    exe = build_host(work)
    M = json.load(open(VECTORS))['mods']
    fails = []

    def check(name, cond, info=''):
        print('%s %s %s' % ('ok  ' if cond else 'FAIL', name, info if not cond else ''))
        if not cond:
            fails.append(name)
    with Bench('analog-rytm-mk2') as B:
        F = Built(B)
        check('the made-up build keeps +Drive storage', F.build.store, F.build.d['storage'])
        mods = {k: storage_mod(B, M[k]) for k in ('duck', 'fx', 'lfo', 'vdac')}
        duck2 = storage_mod(B, M['duck2'], '1.2')
        steps = [
            ('all four: sound and kit run short', [mods['duck'], mods['fx'], mods['lfo'], mods['vdac']]),
            ('duck out: its fields park', [mods['lfo'], mods['vdac']]),
            ('duck back to its bits', [mods['duck'], mods['lfo']]),
            ('nothing chosen: everything parks', []),
            ('duck with a wider field', [duck2, mods['lfo']]),
            ('fx-sends alone: kit repacks', [mods['fx']]),
        ]
        prev = None
        for name, chosen in steps:
            sm, ram, st = boot(exe, F, chosen, prev, work)
            r = twin(F, chosen, prev)
            bad = compare(F, sm, ram, st, r)
            check(name + ': C and twin agree', not bad, '; '.join(bad))
            if name.startswith('all four'):
                why = {c.id: (drive.CODES[c.code][0], c.detail) for c in r.choices}
                check('all four: voice-dac then fx-sends refused for room',
                      why == {'duck': ('ok', ''), 'lfo-rnd': ('ok', ''), 'voice-dac': ('store', 'sound 69 56'),
                              'fx-sends': ('store', 'kit 81 64')}, why)
                d = r.placed.lfo_dsts.get('sound', 0)
                check('all four: core_lfo_dst placed for lfo-rnd, and its code an LFO destination',
                      set(r.placed.lfo_dsts) == {'sound'} and d >> 22 & 63 == 7
                      and all(int.from_bytes(e[4:], 'big') & mstore.LFO_FLAG for e in r.placed.code_entries
                              if e[:4] == r.placed.descriptors[('lfo-rnd', 'lr_a')].to_bytes(4, 'big')),
                      r.placed.lfo_dsts)
                check('all four: the host message', drive.message(29, 'sound 62 56')
                      == 'storage: sound needs 62 of 56', drive.message(29, 'sound 62 56'))
                at = F.build.tables[F.build.table['core_store_ids']]['address'] - F.ram_base
                check('all four: a new layout ID, the flashed one its ancestor',
                      ram[at:at + 4] != F.ram[at:at + 4] and ram[at + 4:at + 8] == F.ram[at:at + 4])
                d = r.placed.descriptors[('duck', 'duck_src')]
                lo = F.build.lo - F.ram_base
                check('all four: duck_src relocated to its descriptor',
                      ram[lo:lo + 4] == d.to_bytes(4, 'big') and d >> 28 == 2 and (d >> 22) & 63 == 4, hex(d))
            if name.startswith('duck out'):
                check('duck out: two parked fields', len(r.placed.state['parked']) == 2, r.placed.state['parked'])
            if name.startswith('nothing chosen'):
                check('nothing chosen: core_lfo_dst parks with lfo-rnd, and the core keeps none',
                      r.placed.lfo_dsts == {} and any(f['sym'] == mstore.LOCK_DST for f in r.placed.state['parked']))
            prev = st
        # a refusal in pass 1: a field the Rytm can't keep
        bad = storage_mod(B, {'id': 'cv', 'storage': [{'sym': 'cv1', 'scope': 'kit', 'bits': 4, 'lock': True,
                                                        'track': 'cv'}]})
        sm, ram, st = boot(exe, F, [bad, mods['lfo']], None, work)
        r = twin(F, [bad, mods['lfo']], None)
        check('a CV field is a format refusal, in C and twin',
              (sm['status'][0]['code'], r.choices[0].code) == (drive.CODE['format'],) * 2
              and not compare(F, sm, ram, st, r), sm['status'])
        # a pinned field ("offset"): the boot allocator places +Drive fields itself, so pass 1 refuses it
        pinned = storage_mod(B, {'id': 'pin', 'storage': [{'sym': 'pn1', 'scope': 'sound', 'bits': 4, 'offset': 40}]})
        sm, ram, st = boot(exe, F, [pinned, mods['lfo']], None, work)
        r = twin(F, [pinned, mods['lfo']], None)
        check('a pinned field is a format refusal, in C and twin',
              (sm['status'][0]['code'], r.choices[0].code) == (drive.CODE['format'],) * 2
              and not compare(F, sm, ram, st, r), sm['status'])
        # two mods with one storage symbol: an export clash
        clash = storage_mod(B, {'id': 'zz', 'storage': [{'sym': 'lr_a', 'scope': 'global', 'bits': 1}]})
        sm, ram, st = boot(exe, F, [mods['lfo'], clash], None, work)
        r = twin(F, [mods['lfo'], clash], None)
        check('one storage symbol in two mods: the later is refused (export)',
              sm['status'][1]['code'] == drive.CODE['export'] and not compare(F, sm, ram, st, r), sm['status'])
        # a bad .store reads as none
        sm, ram, st = boot(exe, F, [mods['lfo']], b'DLST' + bytes(80), work)
        r = twin(F, [mods['lfo']], b'DLST' + bytes(80))
        check('a bad .store is a fresh placement', not compare(F, sm, ram, st, r))
    # a build without +Drive storage still refuses it as a feature
    with Bench('analog-rytm-mk2') as B:
        out = link.link([link.Mod2(d, d['id']) for d in TD.flashed(B)], B.img)
        b = drive.load_build(out.image)
        r = drive.check(b, [('lfo.elemod', json.dumps(storage_mod(B, M['lfo'])).encode())])
        check('no +Drive storage: storage is a feature refusal', not b.store and
              (r.choices[0].code, r.choices[0].detail) == (drive.CODE['feature'], 'storage'), r.choices[0].why())
    if fails:
        print('%d failed' % len(fails))
        return 1
    print('all passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
