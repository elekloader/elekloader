# SPDX-License-Identifier: GPL-2.0-or-later
"""Check mods/trig-events-ot's ev_tail_kill in octabam's ColdFire port (ot_emu), with the Octatrack's
own sequencer running and STOP's tail kill holding tracks (the held-track byte at 0x8000184b is poked).

What it shows (docs: mods/trig-events-ot/README.md, "Checked"):
  - alone it is stock: with core, core + trig-events and no handler, and with a probe whose handlers
    change nothing, every pass through the site (0x40004d0c) reaches stock's next instruction
    (0x40004d16) with the registers and the stack stock has, and the engine's frame records and
    memory are stock's;
  - the handlers are told of each held track that reaches the test, in each frame, in order, with the
    record (the frame builder's a2) and the track (d4), and of no other;
  - what a handler stores in the record is what the next handler finds, and it goes on to the
    copies of the record the DSP is sent: the one byte (slot 11) of the marked tracks is the only
    difference from stock at the end of the run (the builder rebuilds the record in the next
    frame, so the last side handled holds it).

The stack's address at a pass differs between stock and a build with core in 1.2 percent of the passes
(core's boot shifts which task a frame interrupt lands on: core-ot's README, 0.7), so the registers are
compared with it left out.

Run it where ot_emu runs (Linux or WSL), from a built octabam checkout (make emu-cf), with the
m68k cross tools on PATH (ELEKLOADER_CROSS) as for the SDK's tests:

    python tests/probe_octatrack_tail_kill.py --emulator /path/to/octabam \
        --stock OCTATRACK_OS1.40C.syx [--card CARD.img] [--out DIR]

It borrows its fixture and emulator driver from probe_octatrack_seq_events.py and
probe_octatrack_track_trig.py, and makes its card the same way.
"""
import argparse
import re
import struct
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tests'))

from elekloader import formats                           # noqa: E402
from elekloader.sdk import build                         # noqa: E402
import probe_octatrack_seq_events as base                # noqa: E402
import probe_octatrack_track_trig as tt                  # noqa: E402

SITE, NEXT = 0x40004d0c, 0x40004d16                       # the site and the instruction after it
HELD = 0x8000184b                                         # the held-track mask: bit t
REC = 0x80000110                                          # the records: + (side << 9) + 64 * track
WATCH = (SITE, NEXT)
REG = re.compile(r'd2-7 (\S+) (\S+) (\S+) (\S+) (\S+) (\S+) a2-6 (\S+)')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--emulator', required=True, help="octabam checkout with out/emu/ot_emu (make emu-cf)")
    ap.add_argument('--stock', required=True, help='OCTATRACK_OS1.40C.syx')
    ap.add_argument('--card', help='a card image with a set and a project (default: made from an empty one)')
    ap.add_argument('--empty-card', help='an empty card image to make the project card from')
    ap.add_argument('--out', help='where the images and dumps go (default: a temporary folder)')
    a = ap.parse_args(argv)
    out = Path(a.out or tempfile.mkdtemp(prefix='tailkill-'))
    out.mkdir(parents=True, exist_ok=True)
    rep = base.Report()

    st, dev, rel = formats.load(open(a.stock, 'rb').read())
    img = formats.main_image(st, dev)
    mods = out / 'mods'
    built = {}
    for key, d in (('core', 'mods/core-ot'), ('tt', 'mods/trig-events-ot'), ('probe', 'tests/fixtures/octatrack-tail-kill')):
        built[key] = build.build(str(ROOT / d), a.stock, str(mods))[0]
    (out / 'stock.raw').write_bytes(img)
    base.link_image([built['core']], img, out / 'core.raw')
    base.link_image([built['core'], built['tt']], img, out / 'tt.raw')
    pr = base.link_image([built['core'], built['tt'], built['probe']], img, out / 'probe.raw')
    sym = pr.map

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
    emu = tt.Run(a.emulator, out, card)
    chan = '0x46c76de0=0x1;0x40171442=0x1;0x4017770c=0x1'      # M1 on channel 1
    held = lambda mask: '%#x=%#x' % (HELD, mask)
    MASK = 0xff                                                 # every track held
    wide = [tt.FRM, tt.FLAGS] + [('eng', 0x80000000, 0x7000), ('stg', 0x46c75000, 0xd000)]
    pv = [('tk_a', sym['tk_a'], 32), ('tk_b', sym['tk_b'], 32), ('tk_seen', sym['tk_seen'], 8),
          ('tk_log', sym['tk_log'], 64 * 8), ('tk_n', sym['tk_n'], 4)]

    def cfg(mark=0):
        return ';'.join('%#x=%#x' % (sym['tk_cfg'] + j, (mark >> (24 - 8 * j)) & 0xff) for j in range(4))

    def probe_vars(o):
        u = lambda b: struct.unpack('>%dI' % (len(b) // 4), b)
        n = u(o['tk_n'])[0]
        ents = [struct.unpack('>BBHI', o['tk_log'][8 * i:8 * i + 8]) for i in range(n)]
        return u(o['tk_a']), u(o['tk_b']), list(o['tk_seen']), ents

    def lines(h, pcs=WATCH):
        return [(p, re.sub(r'\[sp 0x[0-9a-f]+:', '[sp:', rest)) for p, rest, _t in h if p in pcs]

    def at_site(h):
        """(a2, d4) of each pass at the site: the record and the track"""
        out = []
        for p, rest, _t in h:
            if p == SITE:
                m = REG.search(rest)
                out.append((int(m.group(7), 0), int(m.group(3), 0)))
        return out

    # ---- alone it is stock
    runs = {}
    for k in ('stock', 'core', 'tt'):
        runs[k] = emu.run_many(out / (k + '.raw'), {'a': dict(pre=[chan, held(MASK)], midi=True)}, wide, tag='alone-' + k, watch=WATCH)['a']
    runs['inert'] = emu.run_many(out / 'probe.raw', {'a': dict(pre=[chan, held(MASK), cfg()], midi=True)}, wide + pv, tag='alone-inert', watch=WATCH)['a']
    stock = runs['stock']
    passes = at_site(stock['hits'])
    tracks = sorted({t for _r, t in passes})
    rep.check('alone: with every track held stock passes the site every frame for the tracks that reach the test (%d passes, tracks %s)' % (len(passes), tracks),
              len(passes) > 100 and tracks and sum(1 for p, _r, _t in stock['hits'] if p == NEXT) == len(passes))
    for k in ('core', 'tt', 'inert'):
        rep.check('alone/%s: every pass reaches stock\'s next instruction with the registers and the stack stock has' % k,
                  lines(runs[k]['hits']) == lines(stock['hits']))
        rep.check('alone/%s: the MIDI out, the frame records and the flag bytes are stock\'s' % k,
                  runs[k]['midi.out'] == stock['midi.out'] and all(runs[k][nm] == stock[nm] for nm in ('frm', 'flags')),
                  '%s' % [nm for nm in ('frm', 'flags') if runs[k][nm] != stock[nm]])
    rep.check('alone: core with trig-events = core alone, 80 KB of engine and staging memory at the end', all(
        runs['core'][nm] == runs['tt'][nm] for nm in ('eng', 'stg')))
    cost = lambda h: sorted({t2 - t1 for (p1, _r1, t1), (p2, _r2, t2) in zip(h, h[1:]) if p1 == SITE and p2 == NEXT})
    print('     (instructions from the site to the next: stock %s, no handler %s, two handlers %s)' % (
        cost(stock['hits']), cost(runs['tt']['hits']), cost(runs['inert']['hits'])))

    # ---- what the handlers see
    a_, b_, seen, ents = probe_vars(runs['inert'])
    per = [sum(1 for _r, t in passes if t == k) for k in range(8)]
    # the counters are read 5 frames before the run ends, the hits are counted to its end
    rep.check('seen: both handlers are told of every pass, track by track (%s)' % per,
              list(a_) == list(b_) and all(0 <= per[t] - a_[t] <= 6 for t in range(8)) and all(a_[t] == 0 for t in range(8) if t not in tracks),
              '%s %s' % (list(a_), list(b_)))
    rep.check('seen: A\'s log has the track and the record the builder had at the site (a2, d4), in order',
              [(e[3], e[0]) for e in ents] == passes[:len(ents)] and len(ents) == min(64, len(passes)),
              str(ents[:3]))
    rep.check('seen: the records are the frame builder\'s own: 64-byte slots of the two sides',
              all(0 <= (e[3] - REC) % 512 < 512 and (e[3] - REC) % 64 == 0 and (e[3] - REC) // 64 % 8 == e[0] for e in ents), str(ents[:3]))

    # ---- what a handler stores
    r = emu.run_many(out / 'probe.raw', {'t1': dict(pre=[chan, held(MASK), cfg(1 << tracks[0])], midi=True),
                                         'all': dict(pre=[chan, held(MASK), cfg(0xff)], midi=True),
                                         'free': dict(pre=[chan, held(0), cfg(0xff)], midi=True)}, wide + pv, tag='mark', watch=WATCH)
    marks = {}
    for k, marked in (('t1', tracks[:1]), ('all', tracks)):
        o = r[k]
        a2, b2, seen2, ents2 = probe_vars(o)
        rep.check('mark/%s: B finds A\'s marker in the record of the marked tracks %s, A finds the rebuilt byte (0) each time' % (k, marked),
                  all((seen2[t] == 0x5a) == (t in marked) for t in tracks) and {e[1] for e in ents2} == {0}, str(seen2))
        rep.check('mark/%s: the lines at the site and after it and the MIDI out are stock\'s' % k,
                  lines(o['hits']) == lines(stock['hits']) and o['midi.out'] == stock['midi.out'])
        diff = [i for i in range(len(stock['frm'])) if stock['frm'][i] != o['frm'][i]]
        rep.check('mark/%s: the only difference in the frame records at the end is the marker, in %d bytes: the records and the copies the DSP is sent' % (k, len(diff)),
                  diff and all(stock['frm'][i] == 0 and o['frm'][i] == 0x5a for i in diff), str(diff[:8]))
        marks[k] = set(diff)
    rep.check('mark/t1: marking one track marks a subset of the bytes marking all of them does', marks['t1'] < marks['all'])
    o = r['free']
    rep.check('mark/none held: the handlers are never told, and stock does not pass the site',
              list(probe_vars(o)[0]) == [0] * 8 and not lines(o['hits']))

    bad = rep.failed
    print('\n%d checks, %d failed' % (len(rep.rows), len(bad)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())