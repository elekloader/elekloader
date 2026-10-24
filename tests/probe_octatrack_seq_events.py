# SPDX-License-Identifier: GPL-2.0-or-later
"""Check mods/seq-events-ot in octabam's ColdFire port (ot_emu), with the Octatrack's own sequencer running.

What it shows (docs: mods/seq-events-ot/README.md, "Checked"):
  - alone it is stock: the pending step records of every track, 80 KB of engine and sequencer
    memory, the pattern's memory and the MIDI out are the same with stock, with core alone, with
    seq-events, and with seq-events and a probe whose handlers change nothing;
  - each stage does what it says (a probe mod, tests/fixtures/octatrack-seq-events, with a handler
    for every stage and a configuration poked into its RAM): trigs added, dropped, demoted and
    promoted, the step read, the condition, the notes, velocity and locks, all in the pending
    records and, for a MIDI track, in the note-on bytes the unit sends;
  - every byte it writes into the pattern is put back, also while the firmware's edit path builds
    steps from a task all through the run (the tick builds, and nests inside them);
  - the step clock's parity, sq_track_info, and the real EUCLID example.

Run it where ot_emu runs (Linux or WSL), from a built octabam checkout (make emu-cf), with the m68k
cross tools on PATH (ELEKLOADER_CROSS) as for the SDK's tests:

    python tests/probe_octatrack_seq_events.py --emulator /path/to/octabam \\
        --stock OCTATRACK_OS1.40C.syx [--card CARD.img] [--out DIR] [--only NAME ...]

CARD is an image with a set and a project on it (the port boots from it); without one, the script
makes one: an empty card (octabam's emu_card) and the key presses that create a set and a project.
Nothing of the stock OS is written beside OUT.
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from elekloader import devices, elemod, formats, link   # noqa: E402
from elekloader.sdk import build                         # noqa: E402

DEV = [d for d in devices.devices() if d.key == 'octatrack'][0]
BANK0 = 0x400e21e0
PATTERN = 0x8ed8                    # a pattern's bytes in RAM
A_STRIDE, M_BASE, M_STRIDE = 0x91a, 0x48d0, 0x8b0
STEP = 345                          # one step at 120 BPM, in audio frames
DUMPS = [100 + STEP * i for i in range(8)]       # the pending record of step 1..8 is there by then
END = DUMPS[-1] + 30

# the pending slot 0 of the sequencer, as its tick stages the next step
REGIONS = [('akind', 0x46c7e998, 32), ('atime', 0x80001904, 32), ('alock', 0x46c7ab30, 256),
           ('mkind', 0x46c769c0, 32), ('mtime', 0x46c76a26, 32), ('mlock', 0x46c78960, 256)]
WIDE = REGIONS + [('eng', 0x80000000, 0x7000), ('stg', 0x46c75000, 0xd000)]


# ---- the pattern ------------------------------------------------------------------------------

class Pat:
    """Byte pokes into bank 0, pattern 0 (the project's first pattern)."""

    def __init__(self):
        self.base = BANK0
        self.b = {}

    def rec(self, t):
        return self.base + (A_STRIDE * t if t < 8 else M_BASE + M_STRIDE * (t - 8))

    def mask(self, t, kind, steps):
        r = self.rec(t) + kind
        for s in steps:
            a = r + 7 - (s >> 3)
            self.b[a] = self.b.get(a, 0) | (1 << (s & 7))

    def lock(self, t, step, slot, value):
        self.b[self.rec(t) + (0x59 if t < 8 else 0x30) + 32 * step + slot] = value

    def timing(self, t, step, hi, lo):
        a = self.rec(t) + (0x89a if t < 8 else 0x830) + 2 * step
        self.b[a], self.b[a + 1] = hi, lo

    def length(self, t, n):
        self.b[self.rec(t) + (0x50 if t < 8 else 0x28)] = n

    def spec(self):
        return ';'.join('%#x=%#x' % kv for kv in sorted(self.b.items()))


def fixture(cond=False):
    """T1-T5 and M1, M2: every kind of trig, some locks, a slide."""
    p = Pat()
    p.mask(0, 0, [0, 2, 4, 8, 12]); p.lock(0, 4, 0, 70); p.lock(0, 4, 15, 100)
    p.mask(1, 8, [1, 5]); p.mask(1, 0x10, [3]); p.lock(1, 3, 6, 77)
    p.mask(2, 0x18, [6])
    p.mask(3, 0, [0, 3, 6, 9, 12, 15]); p.timing(3, 6, 0x20, 0x00)
    p.mask(4, 0, [2, 10]); p.mask(4, 0x48, [2]); p.lock(4, 2, 1, 33); p.lock(4, 3, 1, 55)
    p.mask(8, 0, [0, 4, 8, 12]); p.lock(8, 4, 0, 60); p.lock(8, 4, 1, 90); p.lock(8, 8, 3, 71)
    p.mask(9, 0x10, [2]); p.mask(9, 8, [6])
    if cond:
        p.timing(3, 3, 0x00, 0x01)      # T4 step 3: condition 1 (it fails in stock)
        p.timing(3, 6, 0x20, 0x02)      # T4 step 6: condition 2 (it passes)
    return p


# the fixture's pending records as stock stages them, steps 1-8: {step: {track: (kind, {slot: byte})}}
BASE = {1: {1: (0x19, {})}, 2: {0: (0x1d, {}), 4: (0x1d, {1: 33}), 9: (1, {})},
        3: {1: (1, {6: 77}), 3: (0x1d, {})}, 4: {0: (0x1d, {0: 70, 15: 100}), 8: (0x1d, {0: 60, 1: 90})},
        5: {1: (0x19, {})}, 6: {2: (0x3d, {}), 3: (0x1d, {}), 9: (0x19, {})}, 7: {},
        8: {0: (0x1d, {}), 8: (0x1d, {3: 71})}}
NONE = (0, {})


# ---- the emulator -------------------------------------------------------------------------------

class Emu:
    def __init__(self, emulator, out, card):
        self.dir = Path(emulator)
        self.bin = self.dir / 'out' / 'emu' / 'ot_emu'
        self.out, self.card = Path(out), Path(card)
        assert self.bin.exists(), '%s: build it (make emu-cf)' % self.bin

    def run_many(self, raw, scen, regions=REGIONS, tag='run', jobs=3):
        """One boot of RAW, one forked child per scenario {name: dict(pat, pre, extra, midi, dumps)}."""
        d = self.out / tag
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)
        shutil.copy(self.card, d / 'card.img')
        cmd = [str(self.bin), '--image', str(raw), '--card', str(d / 'card.img'), '--load-ms', '90000',
               '--scenario-jobs', str(jobs)]
        for name, sc in scen.items():
            sd = d / name
            sd.mkdir()
            frames = sc.get('dumps', DUMPS)
            a = [str(sd / 'run.log'), '--sequencer', '--internal-clock', '--frames', str(sc.get('frames', frames[-1] + 60))]
            for p in sc.get('pre', ()):
                a += ['--step', '-:poke:' + p]
            if sc.get('pat') is not None:
                a += ['--step', '-:poke:' + sc['pat'].spec()]
            for f in frames:
                a += ['--step', '%d:dump:%s' % (f, ';'.join('%#x,%d=%s/f%d_%s.bin' % (ad, n, sd, f, nm) for nm, ad, n in regions))]
            for s in sc.get('extra', ()):
                a += ['--step', s.replace('{D}', str(sd))]
            if sc.get('midi'):
                a += ['--midi-out', str(sd / 'midi.out')]
            cmd += ['--scenario', ' '.join(a)]
        with open(d / 'parent.log', 'w') as log:
            rc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, cwd=self.dir, timeout=6000).returncode
        out = {}
        for name, sc in scen.items():
            sd = d / name
            o = {'rc': rc, 'dir': sd, 'log': (sd / 'run.log').read_text(errors='replace') if (sd / 'run.log').exists() else ''}
            for f in sc.get('dumps', DUMPS):
                for nm, _a, _n in regions:
                    p = sd / ('f%d_%s.bin' % (f, nm))
                    o[(f, nm)] = p.read_bytes() if p.exists() else None
            for nm in ('midi.out', 'pat.bin', 'log.bin', 'calls.bin', 'info.bin'):
                p = sd / nm
                o[nm] = p.read_bytes() if p.exists() else None
            out[name] = o
        return out


def make_card(emulator, empty, out, stock_raw):
    """A card with a set and a project: the empty card booted from, nine YES presses and a STOP."""
    script, t = [], 0
    for _ in range(9):
        t += 1150; script.append('%d key 0x31 down' % t)
        t += 150; script.append('%d key 0x31 up' % t)
    t += 2000; script.append('%d key 0x2a down' % t)
    t += 100; script.append('%d key 0x2a up' % t)
    t += 2000; script.append('%d enc 0 0' % t)
    t += 1000; script.append('%d quit' % t)
    d = Path(out) / 'card-setup'
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    (d / 'script.txt').write_text('\n'.join(script) + '\n')
    shutil.copy(empty, d / 'card.img')
    cmd = [str(Path(emulator) / 'out' / 'emu' / 'ot_emu'), '--image', str(stock_raw), '--card', str(d / 'card.img'),
           '--load-ms', '90000', '--live-script', str(d / 'script.txt')]
    subprocess.run(cmd, cwd=emulator, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, timeout=3000)
    return d / 'card.img'


def decode(o, f):
    """The pending slot 0 at frame f -> {track: (kind, {slot: byte})}; a kind of 0 has no record."""
    u = lambda b: struct.unpack('>%dI' % (len(b) // 4), b)
    ak, mk, al, ml = u(o[(f, 'akind')]), u(o[(f, 'mkind')]), o[(f, 'alock')], o[(f, 'mlock')]
    res = {}
    for t in range(16):
        kind = ak[t] if t < 8 else mk[t - 8]
        lk = (al[32 * t:32 * t + 32] if t < 8 else ml[32 * (t - 8):32 * (t - 8) + 32])
        res[t] = (kind, {i: b for i, b in enumerate(lk) if b != 255}) if kind else NONE
    return res


def records(o):
    return {s + 1: decode(o, f) for s, f in enumerate(DUMPS)}


def expect_records(changes):
    """BASE for steps 1-8 and every track, with `changes` {(step, track): record} applied"""
    out = {s: {t: BASE.get(s, {}).get(t, NONE) for t in range(16)} for s in range(1, 9)}
    for (s, t), rec in changes.items():
        out[s][t] = rec
    return out


# ---- the notes in a MIDI stream ------------------------------------------------------------------

def notes_of(b):
    out, st, i = [], 0, 0
    while i < len(b):
        if b[i] & 0x80:
            st = b[i] if b[i] < 0xf0 else 0
            i += 1
        elif st & 0xf0 in (0x80, 0x90) and i + 1 < len(b):
            out.append('%s%d/%d' % ('on ' if st & 0xf0 == 0x90 and b[i + 1] else 'off ', b[i], b[i + 1]))
            i += 2
        else:
            i += 1
    return out


# ---- the run ---------------------------------------------------------------------------------------

class Report:
    def __init__(self):
        self.rows = []

    def check(self, name, ok, detail=''):
        self.rows.append((name, bool(ok), detail))
        print('%-4s %s%s' % ('ok' if ok else 'FAIL', name, ('  ' + detail) if detail and not ok else ''), flush=True)

    @property
    def failed(self):
        return [r for r in self.rows if not r[1]]


def link_image(mods, img, path):
    ms = [elemod.load_any(p) for p in mods]
    probs = link.check(ms, img)
    assert not probs, probs
    res = link.link(ms, img)
    Path(path).write_bytes(res.image)
    Path(str(path) + '.map.json').write_text(json.dumps(res.map))
    return res


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--emulator', required=True, help="octabam checkout with out/emu/ot_emu (make emu-cf)")
    ap.add_argument('--stock', required=True, help='OCTATRACK_OS1.40C.syx')
    ap.add_argument('--card', help='a card image with a set and a project (default: made from an empty one)')
    ap.add_argument('--empty-card', help='an empty card image to make the project card from')
    ap.add_argument('--out', help='where the images and dumps go (default: a temporary folder)')
    ap.add_argument('--only', nargs='*', help='run only these checks (alone, stages, midi, nesting, clock, info, example)')
    a = ap.parse_args(argv)
    out = Path(a.out or tempfile.mkdtemp(prefix='seqot-'))
    out.mkdir(parents=True, exist_ok=True)
    want = lambda n: not a.only or n in a.only
    rep = Report()

    # ---- the images
    st, dev, rel = formats.load(open(a.stock, 'rb').read())
    img = formats.main_image(st, dev)
    mods = out / 'mods'
    built = {}
    for key, d in (('core', 'mods/core-ot'), ('seq', 'mods/seq-events-ot'), ('probe', 'tests/fixtures/octatrack-seq-events'),
                   ('euclid', 'examples/euclid-ot')):
        built[key] = build.build(str(ROOT / d), a.stock, str(mods))[0]
    (out / 'stock.raw').write_bytes(img)
    link_image([built['core']], img, out / 'core.raw')
    link_image([built['core'], built['seq']], img, out / 'seq.raw')
    pr = link_image([built['core'], built['seq'], built['probe']], img, out / 'probe.raw')
    link_image([built['core'], built['seq'], built['euclid']], img, out / 'euclid.raw')
    sym = pr.map

    # ---- the card
    card = a.card
    if not card:
        empty = a.empty_card
        if not empty:
            sys.path.insert(0, str(Path(a.emulator) / 'tools' / 'emu'))
            import emu_card
            (out / 'empty-tree').mkdir(exist_ok=True)
            empty = out / 'empty.img'
            empty.write_bytes(emu_card.build_image(str(out / 'empty-tree')))
        card = make_card(a.emulator, empty, out, out / 'stock.raw')
    emu = Emu(a.emulator, out, card)
    pat_dump = ['%d:dump:%#x,%d={D}/pat.bin' % (END, BANK0, PATTERN)]
    pat0 = fixture().b

    def cfg(**kw):
        fields = ['mask', 'gen', 'drop', 'remap', 'cond', 'notes', 'vel', 'locks', 'gnotes']
        base = sym['sp_cfg']
        s = []
        for i, k in enumerate(fields):
            v = kw.get(k, 0) & 0xffffffff
            s += ['%#x=%#x' % (base + 4 * i + j, (v >> (24 - 8 * j)) & 0xff) for j in range(4)]
        return ';'.join(s)

    chan = '0x46c76de0=0x1;0x40171442=0x1;0x4017770c=0x1'      # M1 on channel 1: the live copy, the working and saved Parts

    # ---- alone it is stock
    if want('alone'):
        scen = {'a': dict(pat=fixture(), pre=[chan], extra=pat_dump, midi=True)}
        res = {k: emu.run_many(out / (k + '.raw'), scen, WIDE, jobs=1, tag='alone-' + k)['a']
               for k in ('stock', 'core', 'seq')}
        scen['a']['pre'] = [chan, cfg()]
        res['inert'] = emu.run_many(out / 'probe.raw', scen, WIDE, jobs=1, tag='alone-inert')['a']
        for k, o in res.items():
            rep.check('alone/%s: the fixture stages as stock does' % k, records(o) == expect_records({}))
        same = lambda x, y: all(res[x][(f, nm)] == res[y][(f, nm)] for f in DUMPS for nm, _a, _n in WIDE)
        rep.check('alone: core with seq-events = core alone, 80 KB of memory at 8 steps', same('core', 'seq'))
        # (the probe's own RAM takes a second page of the reserve, which the engine page counts: the records are compared)
        rep.check('alone: with an inert probe = core alone, in the pending records', all(
            res['core'][(f, nm)] == res['inert'][(f, nm)] for f in DUMPS for nm, _a, _n in REGIONS))
        rep.check('alone: the pending records are stock\'s', all(
            res[k][(f, nm)] == res['stock'][(f, nm)] for k in ('core', 'seq', 'inert') for f in DUMPS for nm, _a, _n in REGIONS))
        rep.check('alone: the pattern memory and the MIDI out are stock\'s', all(
            res[k]['pat.bin'] == res['stock']['pat.bin'] and res[k]['midi.out'] == res['stock']['midi.out']
            for k in ('core', 'seq', 'inert')))
        rep.check('alone: stock plays M1\'s two notes', notes_of(res['stock']['midi.out']) == ['on 48/100', 'off 48/0', 'on 60/90', 'off 60/0'],
                  str(notes_of(res['stock']['midi.out'])))

    # ---- the stages, on audio tracks
    if want('stages'):
        stock_cond = emu.run_many(out / 'stock.raw', {'c': dict(pat=fixture(True), extra=pat_dump)}, tag='stages-stock')['c']
        sc = {
            'base': dict(pat=fixture(), pre=[cfg()], extra=pat_dump),
            'euclid': dict(pat=fixture(), pre=[cfg(mask=1, gen=1)], extra=pat_dump),
            'drop': dict(pat=fixture(), pre=[cfg(mask=8, drop=1)], extra=pat_dump),
            'remap': dict(pat=fixture(), pre=[cfg(mask=1, remap=1)], extra=pat_dump),
            'condpass': dict(pat=fixture(True), pre=[cfg(mask=8, cond=1)], extra=pat_dump),
            'condfail': dict(pat=fixture(), pre=[cfg(mask=8, cond=2)], extra=pat_dump),
            'locks': dict(pat=fixture(), pre=[cfg(mask=1, locks=1)], extra=pat_dump),
            'lockgen': dict(pat=fixture(), pre=[cfg(mask=4, gen=2, locks=1)], extra=pat_dump),
            'demote': dict(pat=fixture(), pre=[cfg(mask=1, gen=3)], extra=pat_dump),
            'promote': dict(pat=fixture(), pre=[cfg(mask=2, gen=4)], extra=pat_dump),
            'mdemote': dict(pat=fixture(), pre=[cfg(mask=0x100, gen=3)], extra=pat_dump),
        }
        r = emu.run_many(out / 'probe.raw', sc, tag='stages')
        want_changes = {
            'euclid': ('a Euclidean rhythm on T1: a trig on step 7 with no locks, the stored ones on 2 and 8 gone',
                       {(2, 0): NONE, (7, 0): (0x1d, {}), (8, 0): NONE}),
            'drop': ('a dropped trig on T4 step 3', {(3, 3): NONE}),
            'remap': ('T1 reading the mirrored step', {(2, 0): NONE, (3, 0): (0x1d, {}), (4, 0): NONE, (7, 0): (0x1d, {}), (8, 0): NONE}),
            'condfail': ('a failed condition ends T4 step 6', {(6, 3): NONE}),
            'locks': ('locks on every step T1 fires, over a stored one', {(2, 0): (0x1d, {0: 99, 15: 52}), (4, 0): (0x1d, {0: 99, 15: 54}),
                                                                       (8, 0): (0x1d, {0: 99, 15: 58})}),
            'lockgen': ('lock trigs made on T3 take locks; its one-shot too', {(1, 2): (1, {0: 99, 15: 51}), (5, 2): (1, {0: 99, 15: 55}),
                                                                           (6, 2): (0x3d, {0: 99, 15: 56})}),
            'demote': ('T1\'s trigs turned to lock trigs, their locks kept', {(2, 0): (1, {}), (4, 0): (1, {0: 70, 15: 100}), (8, 0): (1, {})}),
            'promote': ('T2\'s trigless and lock trigs turned to trigs', {(1, 1): (0x1d, {}), (3, 1): (0x1d, {6: 77}), (5, 1): (0x1d, {})}),
            'mdemote': ('M1\'s trigs turned to lock trigs', {(4, 8): (1, {0: 60, 1: 90}), (8, 8): (1, {3: 71})}),
            'base': ('inert handlers', {}),
        }
        for name, (what, ch) in want_changes.items():
            rep.check('stages/%s: %s' % (name, what), records(r[name]) == expect_records(ch),
                      str({k: v for k, v in records(r[name]).items() if v != expect_records(ch)[k]})[:300])
        cond0 = records(stock_cond)
        rep.check('stages/cond: a stock condition 1 ends T4 step 3', cond0[3][3] == NONE and cond0[6][3] == (0x1d, {}))
        rep.check('stages/cond: forced to pass, it fires', records(r['condpass'])[3][3] == (0x1d, {}))
        rep.check('stages: the pattern is put back after every run',
                  all(r[k]['pat.bin'] == r['base']['pat.bin'] for k in sc if k not in ('condpass',)) and
                  r['condpass']['pat.bin'] == stock_cond['pat.bin'])

    # ---- a MIDI track's notes, velocity and note-on bytes
    if want('midi'):
        sc = {
            'm_base': dict(pat=fixture(), pre=[cfg(), chan], extra=pat_dump, midi=True),
            'm_notes': dict(pat=fixture(), pre=[cfg(mask=0x100, notes=1), chan], extra=pat_dump, midi=True),
            'm_vel': dict(pat=fixture(), pre=[cfg(mask=0x100, vel=1), chan], extra=pat_dump, midi=True),
            'm_gen': dict(pat=fixture(), pre=[cfg(mask=0x100, gen=1, gnotes=1), chan], extra=pat_dump, midi=True),
            'm_silent': dict(pat=fixture(), pre=[cfg(mask=0x100, notes=2), chan], extra=pat_dump, midi=True),
        }
        r = emu.run_many(out / 'probe.raw', sc, tag='midi')
        n = {k: notes_of(v['midi.out']) for k, v in r.items()}
        rep.check('midi/base: as stock', n['m_base'] == ['on 48/100', 'off 48/0', 'on 60/90', 'off 60/0'], str(n['m_base']))
        rep.check('midi/notes: a fifth up and an octave, the first step too',
                  n['m_notes'] == ['on 60/100', 'on 67/100', 'off 60/0', 'off 67/0', 'on 72/90', 'on 79/90', 'off 72/0', 'off 79/0'], str(n['m_notes']))
        rep.check('midi/velocity: 100 + step', n['m_vel'] == ['on 48/100', 'off 48/0', 'on 60/104', 'off 60/0'], str(n['m_vel']))
        rep.check('midi/generated: a Euclidean rhythm with the note 48 + step',
                  n['m_gen'] == ['on 48/100', 'off 48/0', 'on 52/90', 'off 52/0', 'on 55/100'], str(n['m_gen']))
        rep.check('midi/silent: a step given no note sends none', n['m_silent'] == [], str(n['m_silent']))
        rep.check('midi: the pattern is put back', all(v['pat.bin'] == r['m_base']['pat.bin'] for v in r.values()))

    # ---- builds from a task, nested with the tick
    if want('nesting'):
        everything = dict(mask=0xffff, gen=1, notes=1, vel=1, locks=1)
        calls = ['%d:call:0x4009da20,0xffffffff' % f for f in range(105, 2700, 11)]       # all 16 tracks, from a task
        sc = {'quiet': dict(pat=fixture(), pre=[cfg(**everything)], extra=pat_dump),
              'busy': dict(pat=fixture(), pre=[cfg(**everything)], extra=pat_dump + calls)}
        r = emu.run_many(out / 'probe.raw', sc, tag='nesting')
        done = r['busy']['log'].count('returned')
        rep.check('nesting: %d builds of all 16 tracks from a task ran' % done, done >= 200)
        rep.check('nesting: the tick\'s pending records are the same with them',
                  all(r['quiet'][(f, nm)] == r['busy'][(f, nm)] for f in DUMPS for nm, _a, _n in REGIONS))
        rep.check('nesting: the pattern is put back', r['busy']['pat.bin'] == r['quiet']['pat.bin'] ==
                  emu.run_many(out / 'stock.raw', {'p': dict(pat=fixture(), extra=pat_dump)}, tag='nesting-stock')['p']['pat.bin'])

    # ---- the clock and the pattern's facts
    if want('clock') or want('info'):
        p = fixture()
        p.b[p.base + 0x8e53] = 4                                  # a 4-step pattern: a wrap every 4 steps
        ti = sym['sq_track_info']
        q = fixture()
        q.b[q.base + 0x8e55] = 1                                  # per-track scale
        q.length(1, 12); q.length(8, 6); q.length(9, 32)
        q.b[q.base + 0x8e50], q.b[q.base + 0x8e51] = 0, 64        # master length 64
        buf = sym['sp_log'] + 0x200
        sc = {'clock': dict(pat=p, pre=[cfg(mask=1, gen=1)], dumps=[100 + STEP * i for i in range(0, 24, 3)], frames=STEP * 24 + 220,
                            extra=['%d:dump:%#x,768={D}/log.bin;%#x,32={D}/calls.bin' % (100 + STEP * 24, sym['sp_log'], sym['sp_calls'])]),
              'info': dict(pat=q, pre=[cfg()], dumps=[100], extra=['-:call:%#x,%d,%#x,16' % (ti, t, buf + 16 * i) for i, t in enumerate((0, 1, 8, 9))]
                           + ['100:dump:%#x,64={D}/info.bin' % buf])}
        r = emu.run_many(out / 'probe.raw', sc, tag='clock')
        rows = [struct.unpack('bbbbb3x', r['clock']['log.bin'][8 * i:8 * i + 8]) for i in range(96)]
        i1 = next(i for i, r_ in enumerate(rows) if r_[0] == 1 and r_[1] == 1)       # the tick's first build: step 1
        built = [(s, par) for s, first, trig, par, last in rows[i1:] if first == 1 and s >= 0]
        flips = [built[i][1] != built[i - 1][1] for i in range(1, len(built))]
        wraps = [built[i][0] == 0 for i in range(1, len(built))]
        rep.check('clock: the parity flips exactly at the step after the last (%d builds)' % len(built),
                  len(built) > 12 and flips == wraps, str(built[:24]))
        inf = [struct.unpack('>iiBBbB', r['info']['info.bin'][16 * i:16 * i + 12]) for i in range(4)]
        rep.check('info: sq_track_info of T1, T2 (12), M1 (6), M2 (32): length, master, note, no key',
                  inf == [(16, 64, 60, 255, -2, 0), (12, 64, 60, 255, -2, 0), (6, 64, 48, 255, -2, 0), (32, 64, 48, 255, -2, 0)], str(inf))

    # ---- the real example
    if want('example'):
        sc = {'a': dict(pat=fixture(), pre=[chan], extra=pat_dump, midi=True)}
        e = emu.run_many(out / 'euclid.raw', sc, tag='example')['a']
        s = emu.run_many(out / 'stock.raw', sc, tag='example-stock')['a']
        re_, rs = records(e), records(s)
        rep.check('example: EUCLID fires T1 on steps 4 and 7 of 1-8 (5 of 16)',
                  [st for st in range(1, 9) if re_[st][0][0]] == [4, 7])
        rep.check('example: every other track is stock\'s, and the MIDI out and the pattern',
                  all(re_[st][t] == rs[st][t] for st in range(1, 9) for t in range(1, 16)) and e['midi.out'] == s['midi.out']
                  and e['pat.bin'] == s['pat.bin'])

    bad = rep.failed
    print('\n%d checks, %d failed' % (len(rep.rows), len(bad)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
