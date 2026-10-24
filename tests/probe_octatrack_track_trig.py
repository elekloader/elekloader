# SPDX-License-Identifier: GPL-2.0-or-later
"""Check mods/trig-events-ot in octabam's ColdFire port (ot_emu), with the Octatrack's own sequencer running.

What it shows (docs: mods/trig-events-ot/README.md, "Checked"):
  - alone it is stock: with no handler, and with a probe whose handlers change nothing, every call of
    the machine's trig function has the registers and the stack stock has (ot_emu's --watch-pc on the
    call, 0x4000d49c), the pending step records, the frame records and the engine's memory are the same;
  - the handlers see every trig the stock machine call gets, in order, with the track, its flag byte
    and its machine, and the EMAC mode the interrupt has set;
  - a veto stops the machine's function being called for that trig and nothing else: the other
    trigs' calls are stock's, the sequencer's records are, and the trig's flag bit goes as for any trig;
    a handler later in the table does not see a vetoed trig, one earlier does.

Run it where ot_emu runs (Linux or WSL), from a built octabam checkout (make emu-cf), with the m68k
cross tools on PATH (ELEKLOADER_CROSS) as for the SDK's tests:

    python tests/probe_octatrack_track_trig.py --emulator /path/to/octabam \\
        --stock OCTATRACK_OS1.40C.syx [--card CARD.img] [--out DIR] [--only NAME ...]

CARD is an image with a set and a project on it, as for probe_octatrack_seq_events.py, which this
borrows its fixture and emulator driver from; without one, the script makes it the same way.
"""
import argparse
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tests'))

from elekloader import formats                           # noqa: E402
from elekloader.sdk import build                         # noqa: E402
import probe_octatrack_seq_events as base                # noqa: E402

DISPATCH, MACHINE, REBIND = 0x4000d49c, 0x4000f450, 0x4000f4dc   # the call, the machine's function, the voice rebind inside it
SITE = 0x4000d496               # trig-events' site: stock's three instructions before the call
FRAMES = base.DUMPS[-1] + 60
FRM = ('frm', 0x80000100, 0x3100)       # the engine's per-track records, both sides of the double buffer
FLAGS = ('flags', 0x46104d0c, 8)
HIT = re.compile(r'^\s*\[\s*(\d+)\] at (0x[0-9a-f]+) (.*)$')
NT = base.NONE


class Run:
    """One boot of an image, one forked child per scenario, with --watch-pc on the dispatch."""

    def __init__(self, emulator, out, card):
        self.dir, self.out, self.card = Path(emulator), Path(out), Path(card)
        self.bin = self.dir / 'out' / 'emu' / 'ot_emu'
        assert self.bin.exists(), '%s: build it (make emu-cf)' % self.bin

    def run_many(self, raw, scen, regions, tag, watch=(SITE, DISPATCH, MACHINE, REBIND)):
        d = self.out / tag
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)
        shutil.copy(self.card, d / 'card.img')
        cmd = [str(self.bin), '--image', str(raw), '--card', str(d / 'card.img'), '--load-ms', '90000',
               '--scenario-jobs', '1']
        if watch:
            cmd += ['--watch-pc', ','.join('%#x' % w for w in watch)]
        for name, sc in scen.items():
            sd = d / name
            sd.mkdir()
            a = [str(sd / 'run.log'), '--sequencer', '--internal-clock', '--frames', str(FRAMES)]
            for p in sc.get('pre', ()):
                a += ['--step', '-:poke:' + p]
            a += ['--step', '-:poke:' + base.fixture().spec()]
            for f in base.DUMPS:
                a += ['--step', '%d:dump:%s' % (f, ';'.join('%#x,%d=%s/f%d_%s.bin' % (ad, n, sd, f, nm)
                                                           for nm, ad, n in base.REGIONS))]
            end = [(nm, ad, n) for nm, ad, n in regions]
            a += ['--step', '%d:dump:%s' % (FRAMES - 5, ';'.join('%#x,%d=%s/end_%s.bin' % (ad, n, sd, nm) for nm, ad, n in end))]
            if sc.get('midi'):
                a += ['--midi-out', str(sd / 'midi.out')]
            cmd += ['--scenario', ' '.join(a)]
        with open(d / 'parent.log', 'w') as log:
            subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, cwd=self.dir, timeout=6000)
        out = {}
        for name, sc in scen.items():
            sd = d / name
            o = {'dir': sd, 'log': (sd / 'run.log').read_text(errors='replace') if (sd / 'run.log').exists() else ''}
            for f in base.DUMPS:
                for nm, _a, _n in base.REGIONS:
                    p = sd / ('f%d_%s.bin' % (f, nm))
                    o[(f, nm)] = p.read_bytes() if p.exists() else None
            for nm, _a, _n in regions:
                p = sd / ('end_%s.bin' % nm)
                o[nm] = p.read_bytes() if p.exists() else None
            p = sd / 'midi.out'
            o['midi.out'] = p.read_bytes() if p.exists() else None
            o['hits'] = hits(o['log'])
            out[name] = o
        return out


def hits(log):
    """The --watch-pc hits of a log: [(pc, the rest of the line without its timestamp)]"""
    out = []
    for line in log.splitlines():
        m = HIT.match(line)
        if m:
            out.append((int(m.group(2), 16), m.group(3), int(m.group(1))))
    return out


def tracks(h, pc=DISPATCH):
    """the track of each call at pc: at the dispatch the stack's first word, at the machine's entry its second"""
    out = []
    for p, rest, _t in h:
        if p == pc:
            m = re.search(r'\[sp 0x[0-9a-f]+: ((?:0x[0-9a-f]+|\d+) (?:0x[0-9a-f]+|\d+)) ', rest)
            out.append(int(m.group(1).split()[0 if pc == DISPATCH else 1], 0))
    return out


ALL = (DISPATCH, MACHINE, REBIND)


def lines(h, pcs=(DISPATCH, MACHINE)):
    return [(p, rest) for p, rest, _t in h if p in pcs]


def cost(h):
    """the instructions from the site to the call, at each trig (the hits' timestamps are instruction counts)"""
    out, site = [], None
    for p, _r, t in h:
        if p == SITE:
            site = t
        elif p == DISPATCH and site is not None:
            out.append(t - site)
            site = None
    return out


def u32s(b):
    return struct.unpack('>%dI' % (len(b) // 4), b)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--emulator', required=True, help="octabam checkout with out/emu/ot_emu (make emu-cf)")
    ap.add_argument('--stock', required=True, help='OCTATRACK_OS1.40C.syx')
    ap.add_argument('--card', help='a card image with a set and a project (default: made from an empty one)')
    ap.add_argument('--empty-card', help='an empty card image to make the project card from')
    ap.add_argument('--out', help='where the images and dumps go (default: a temporary folder)')
    ap.add_argument('--only', nargs='*', help='run only these checks (alone, seen, veto, order)')
    a = ap.parse_args(argv)
    out = Path(a.out or tempfile.mkdtemp(prefix='trigot-'))
    out.mkdir(parents=True, exist_ok=True)
    want = lambda n: not a.only or n in a.only
    rep = base.Report()

    # ---- the images
    st, dev, rel = formats.load(open(a.stock, 'rb').read())
    img = formats.main_image(st, dev)
    mods = out / 'mods'
    built = {}
    for key, d in (('core', 'mods/core-ot'), ('tt', 'mods/trig-events-ot'), ('probe', 'tests/fixtures/octatrack-track-trig')):
        built[key] = build.build(str(ROOT / d), a.stock, str(mods))[0]
    (out / 'stock.raw').write_bytes(img)
    base.link_image([built['core']], img, out / 'core.raw')
    base.link_image([built['core'], built['tt']], img, out / 'tt.raw')
    pr = base.link_image([built['core'], built['tt'], built['probe']], img, out / 'probe.raw')
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
        card = base.make_card(a.emulator, empty, out, out / 'stock.raw')
    emu = Run(a.emulator, out, card)
    chan = '0x46c76de0=0x1;0x40171442=0x1;0x4017770c=0x1'
    pv = [('tp_a', sym['tp_a'], 32), ('tp_b', sym['tp_b'], 32), ('tp_log', sym['tp_log'], 96 * 12), ('tp_n', sym['tp_n'], 4),
          ('tp_gap', sym['tp_gap'], 4), ('tp_gap_min', sym['tp_gap_min'], 4)]
    wide = [FRM, FLAGS] + [('eng', 0x80000000, 0x7000), ('stg', 0x46c75000, 0xd000)]

    def cfg(veto_a=0, veto_b=0):
        s = []
        for i, v in enumerate((veto_a, veto_b)):
            s += ['%#x=%#x' % (sym['tp_cfg'] + 4 * i + j, (v >> (24 - 8 * j)) & 0xff) for j in range(4)]
        return ';'.join(s)

    def probe_vars(o):
        a_, b_ = u32s(o['tp_a']), u32s(o['tp_b'])
        n = u32s(o['tp_n'])[0]
        ents = [struct.unpack('>BBBxII', o['tp_log'][12 * i:12 * i + 12]) for i in range(n)]
        return a_, b_, ents, u32s(o['tp_gap'])[0], u32s(o['tp_gap_min'])[0]

    # ---- alone it is stock
    stock_h = None
    if want('alone') or want('seen') or want('veto') or want('order'):
        runs = {}
        for k in ('stock', 'core', 'tt'):
            runs[k] = emu.run_many(out / (k + '.raw'), {'a': dict(pre=[chan], midi=True)}, wide, tag='alone-' + k)['a']
        runs['inert'] = emu.run_many(out / 'probe.raw', {'a': dict(pre=[chan, cfg()], midi=True)}, wide + pv, tag='alone-inert')['a']
        stock_h = runs['stock']['hits']
        st_tracks = tracks(stock_h)
        rep.check('alone: stock calls the machine %d times from the dispatch in 8 steps (tracks %s)' % (len(st_tracks), st_tracks),
                  len(st_tracks) >= 7 and tracks(stock_h, MACHINE) == st_tracks)
    if want('alone'):
        for k in ('core', 'tt', 'inert'):
            rep.check('alone/%s: every call of the machine has the registers and the stack stock has' % k,
                      lines(runs[k]['hits'], ALL) == lines(stock_h, ALL) and len(lines(stock_h)) == 2 * len(st_tracks))
        same = lambda x, y, regs: all(runs[x].get((f, nm)) == runs[y].get((f, nm)) for f in base.DUMPS for nm, _a, _n in regs)
        for k in ('core', 'tt', 'inert'):
            rep.check('alone/%s: the pending step records are stock\'s' % k, same('stock', k, base.REGIONS))
            rep.check('alone/%s: the engine\'s frame records and the flag bytes at the end are stock\'s' % k,
                      all(runs[k][nm] == runs['stock'][nm] for nm in ('frm', 'flags')))
        rep.check('alone: the MIDI out is stock\'s', all(runs[k]['midi.out'] == runs['stock']['midi.out'] for k in ('core', 'tt', 'inert')))
        rep.check('alone: core with trig-events = core alone, 80 KB of memory at the end', all(
            runs['core'][nm] == runs['tt'][nm] for nm in ('eng', 'stg')))

    # ---- what a handler sees
    if want('seen'):
        a_, b_, ents, gap, gmin = probe_vars(runs['inert'])
        rep.check('seen: handler A is given every trig the machine is, in order', [e[0] for e in ents] == st_tracks, str([e[0] for e in ents]))
        rep.check('seen: handler B too, none vetoed', sum(b_) == len(st_tracks) and list(b_) == list(a_))
        rep.check('seen: per-track counts', list(a_) == [st_tracks.count(t) for t in range(8)], str(list(a_)))
        rep.check('seen: the flag byte has bit 4 set, and the machine is one with a function (0, 1 or 4)',
                  all(e[1] & 0x10 for e in ents) and all(e[2] in (0, 1, 4) for e in ents), str(ents[:3]))
        rep.check('seen: the EMAC mode is the interrupt\'s own, the same for every trig', len({e[4] for e in ents}) == 1,
                  str({e[4] for e in ents}))
        print('     (macsr %#x; A to B, the walk and A\'s body: %d..%d counts of the 132 MHz timer)' % (ents[0][4], gmin, gap))
        c = {k: sorted(set(cost(runs[k]['hits']))) for k in ('stock', 'core', 'tt', 'inert')}
        rep.check('seen: from the site to the call stock runs %s instructions; with no handler %s, with two handlers %s' % (
            c['stock'], c['tt'], c['inert']), c['stock'] == c['core'] == [3] and c['tt'] == [7] and max(c['inert']) < 100)

    # ---- vetoes
    if want('veto'):
        sc = {'t1': dict(pre=[chan, cfg(veto_a=0x01)], midi=True), 't4': dict(pre=[chan, cfg(veto_b=0x08)], midi=True),
              'both': dict(pre=[chan, cfg(veto_a=0x01, veto_b=0x08)], midi=True), 'all': dict(pre=[chan, cfg(veto_a=0xff)], midi=True)}
        r = emu.run_many(out / 'probe.raw', sc, wide + pv, tag='veto')
        names = {'t1': (0x01, 0), 't4': (0, 0x08), 'both': (0x01, 0x08), 'all': (0xff, 0)}
        for k, (va, vb) in names.items():
            o = r[k]
            a_, b_, ents, _g, _m = probe_vars(o)
            seen_by_b = [t for t in st_tracks if not (va >> t) & 1]
            gone = [t for t in seen_by_b if not (vb >> t) & 1]      # the trigs that reach the machine
            rep.check('veto/%s: A sees every trig, B all A let through' % k,
                      [e[0] for e in ents] == st_tracks and list(b_) == [seen_by_b.count(t) for t in range(8)],
                      '%s %s' % (list(a_), list(b_)))
            rep.check('veto/%s: the machine is called for the trigs nobody vetoed, with stock\'s registers and stack, and no other' % k,
                      tracks(o['hits']) == gone and tracks(o['hits'], MACHINE) == gone,
                      '%s != %s' % (tracks(o['hits']), gone))
            keep = [(p, rest) for p, rest in lines(stock_h)]
            want_lines, i = [], 0
            for t_i, t in enumerate(st_tracks):
                two = keep[2 * t_i:2 * t_i + 2]
                if t in gone:
                    want_lines += two
            rep.check('veto/%s: the calls that remain are stock\'s own lines' % k, lines(o['hits']) == want_lines)
            rep.check('veto/%s: the sequencer\'s pending records and the MIDI out are stock\'s' % k,
                      all(o[(f, nm)] == runs['stock'][(f, nm)] for f in base.DUMPS for nm, _a, _n in base.REGIONS)
                      and o['midi.out'] == runs['stock']['midi.out'])
            rep.check('veto/%s: every flag byte has its trig bit cleared at the end, as stock\'s' % k, o['flags'] == runs['stock']['flags'],
                      '%s vs %s' % (o['flags'].hex(), runs['stock']['flags'].hex()))
        rep.check('veto/all: no call of the machine at all', tracks(r['all']['hits']) == [])
        rb = lambda h: sum(1 for p, _r, _t in h if p == REBIND)
        rep.check('veto: the machine\'s voice rebind runs once less for each trig vetoed (stock %d)' % rb(stock_h), all(
            rb(r[k]['hits']) == rb(stock_h) - sum(1 for t in st_tracks if (va >> t) & 1 or (vb >> t) & 1)
            for k, (va, vb) in names.items()))

    bad = rep.failed
    print('\n%d checks, %d failed' % (len(rep.rows), len(bad)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
