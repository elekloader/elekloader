# SPDX-License-Identifier: GPL-2.0-or-later
"""Check mods/intercepts-ot's ev_midi_note_key in octabam's ColdFire port (ot_emu), with the
Octatrack's own sequencer playing a MIDI track.

What it shows (docs: mods/intercepts-ot/README.md, "Checked"):
  - alone it is stock: with core + intercepts and no record, and with a probe record that
    claims nothing, every pass through the site (0x4009fb58) reaches stock's exit (0x4009fb80)
    with the registers and the stack stock has, and the MIDI out and the sequencer's frame
    records are stock's;
  - claims is asked four times for a trig (the chord's four slots), with the MIDI track (0-7) in d7;
  - a record that claims the track gets the call: its entry sees the track and the note byte, may rewrite the note (the MIDI out follows, the note-off too), drop
    it (-1: no note is sent), send it as it is (0x4009fb80) or hand it to stock's KEY correction
    (the replayed tst.l/ble.s), whose result is checked against the correction computed from the
    stock image's own table;
  - a record that claims another track is asked, not entered.

Run it where ot_emu runs (Linux or WSL), from a built octabam checkout (make emu-cf), with the
m68k cross tools on PATH (ELEKLOADER_CROSS) as for the SDK's tests:

    python tests/probe_octatrack_note_key.py --emulator /path/to/octabam \
        --stock OCTATRACK_OS1.40C.syx [--card CARD.img] [--out DIR]

It borrows its fixture and emulator driver from probe_octatrack_seq_events.py and
probe_octatrack_track_trig.py, and makes its card the same way.
"""
import argparse
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

SITE, KEYED, SEND = 0x4009fb58, 0x4009fb5e, 0x4009fb80   # the site; stock's correction; the send
KEY_TABLE = 0x400d80a0                                    # twelve longs: the correction added to a note
WATCH = (SITE, KEYED, SEND)
M1 = 0          # the MIDI track in d7: 0-7


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--emulator', required=True, help="octabam checkout with out/emu/ot_emu (make emu-cf)")
    ap.add_argument('--stock', required=True, help='OCTATRACK_OS1.40C.syx')
    ap.add_argument('--card', help='a card image with a set and a project (default: made from an empty one)')
    ap.add_argument('--empty-card', help='an empty card image to make the project card from')
    ap.add_argument('--out', help='where the images and dumps go (default: a temporary folder)')
    a = ap.parse_args(argv)
    out = Path(a.out or tempfile.mkdtemp(prefix='notekey-'))
    out.mkdir(parents=True, exist_ok=True)
    rep = base.Report()

    st, dev, rel = formats.load(open(a.stock, 'rb').read())
    img = formats.main_image(st, dev)
    mods = out / 'mods'
    built = {}
    for key, d in (('core', 'mods/core-ot'), ('ic', 'mods/intercepts-ot'), ('probe', 'tests/fixtures/octatrack-note-key')):
        built[key] = build.build(str(ROOT / d), a.stock, str(mods))[0]
    (out / 'stock.raw').write_bytes(img)
    base.link_image([built['core']], img, out / 'core.raw')
    base.link_image([built['core'], built['ic']], img, out / 'ic.raw')
    pr = base.link_image([built['core'], built['ic'], built['probe']], img, out / 'probe.raw')
    sym = pr.map
    table = struct.unpack('>12i', img[KEY_TABLE - dev.main_load:KEY_TABLE - dev.main_load + 48])

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
    chan = '0x46c76de0=0x1;0x40171442=0x1;0x4017770c=0x1'      # M1 on channel 1: the live copy, the working and saved Parts
    wide = [tt.FRM, tt.FLAGS] + [('eng', 0x80000000, 0x7000), ('stg', 0x46c75000, 0xd000)]
    pv = [('nk_asked', sym['nk_asked'], 64), ('nk_log', sym['nk_log'], 256), ('nk_n', sym['nk_n'], 4)]

    def cfg(claim=0, mode=0, offset=0):
        s = []
        for i, v in enumerate((claim, mode, offset)):
            s += ['%#x=%#x' % (sym['nk_cfg'] + 4 * i + j, (v >> (24 - 8 * j)) & 0xff) for j in range(4)]
        return ';'.join(s)

    def probe_vars(o):
        asked = struct.unpack('>16I', o['nk_asked'])
        n = struct.unpack('>I', o['nk_n'])[0]
        return asked, [(o['nk_log'][4 * i], o['nk_log'][4 * i + 1]) for i in range(n)]

    def lines(h, pcs=WATCH):
        return [(p, rest) for p, rest, _t in h if p in pcs]

    def count(h, pc):
        return sum(1 for p, _r, _t in h if p == pc)

    # ---- alone it is stock
    runs = {}
    for k in ('stock', 'core', 'ic'):
        runs[k] = emu.run_many(out / (k + '.raw'), {'a': dict(pre=[chan], midi=True)}, wide, tag='alone-' + k, watch=WATCH)['a']
    runs['inert'] = emu.run_many(out / 'probe.raw', {'a': dict(pre=[chan, cfg()], midi=True)}, wide + pv, tag='alone-inert', watch=WATCH)['a']
    stock = runs['stock']
    notes = base.notes_of(stock['midi.out'])
    rep.check('alone: stock plays M1\'s two notes', notes == ['on 48/100', 'off 48/0', 'on 60/90', 'off 60/0'], str(notes))
    n_site = count(stock['hits'], SITE)
    rep.check('alone: stock passes the site four times for each of the two trigs (%d), and its exit is taken' % n_site,
              n_site == 8 and count(stock['hits'], SEND) == 8, str([hex(p) for p, _r, _t in stock['hits']]))
    for k in ('core', 'ic', 'inert'):
        rep.check('alone/%s: every pass reaches stock\'s exit with the registers and the stack stock has' % k,
                  lines(runs[k]['hits']) == lines(stock['hits']))
        rep.check('alone/%s: the MIDI out and the engine\'s frame records are stock\'s' % k,
                  runs[k]['midi.out'] == stock['midi.out'] and all(runs[k][nm] == stock[nm] for nm in ('frm', 'flags')),
                  '%s' % [nm for nm in ('frm', 'flags') if runs[k][nm] != stock[nm]])
    rep.check('alone: core with intercepts = core alone, 80 KB of engine and staging memory at the end', all(
        runs['core'][nm] == runs['ic'][nm] for nm in ('eng', 'stg')))

    # ---- what claims and the entry see
    asked, log = probe_vars(runs['inert'])
    rep.check('seen: claims is asked once for each pass, with the MIDI track in d7 (M1 = %d)' % M1,
              asked[M1] == n_site and sum(asked) == n_site, str(asked))
    cost = lambda h: [t2 - t1 for (p1, _r1, t1), (p2, _r2, t2) in zip(h, h[1:]) if p1 == SITE and p2 == SEND]
    print('     (instructions from the site to the send: stock %s, no record %s, one record asked %s)' % (
        sorted(set(cost(stock['hits']))), sorted(set(cost(runs['ic']['hits']))), sorted(set(cost(runs['inert']['hits'])))))

    # ---- records
    sc = {'other': dict(pre=[chan, cfg(claim=1 << 1, mode=1)], midi=True),
          'observe': dict(pre=[chan, cfg(claim=1 << M1, mode=0)], midi=True),
          'plus2': dict(pre=[chan, cfg(claim=1 << M1, mode=1)], midi=True),
          'drop': dict(pre=[chan, cfg(claim=1 << M1, mode=2)], midi=True),
          'send': dict(pre=[chan, cfg(claim=1 << M1, mode=3)], midi=True),
          'key': dict(pre=[chan, cfg(claim=1 << M1, mode=4, offset=1)], midi=True)}
    r = emu.run_many(out / 'probe.raw', sc, wide + pv, tag='records', watch=WATCH)
    notes_of = {k: base.notes_of(v['midi.out']) for k, v in r.items()}
    o = r['other']
    rep.check('records: a record that claims another track is asked, not entered, and the unit is stock',
              probe_vars(o)[1] == [] and notes_of['other'] == notes and lines(o['hits']) == lines(stock['hits']))
    o = r['observe']
    asked, log = probe_vars(o)
    rep.check('records: the entry is given the track (d7) and the note byte, four times for each trig (the chord\'s four slots)',
              log == [(M1, 48)] * 4 + [(M1, 60)] * 4, str(log))
    rep.check('records: an entry that changes nothing and hands the note to stock is stock\'s own: notes, registers, frame records',
              notes_of['observe'] == notes and lines(o['hits']) == lines(stock['hits']) and all(o[nm] == stock[nm] for nm in ('frm', 'flags')))
    rep.check('records: an entry that rewrites the note changes what is sent, and the note-off follows it',
              notes_of['plus2'] == ['on 50/100', 'off 50/0', 'on 62/90', 'off 62/0'], str(notes_of['plus2']))
    rep.check('records: an entry that stores -1 drops the note: nothing is sent',
              notes_of['drop'] == [] and count(r['drop']['hits'], SEND) == n_site, str(notes_of['drop']))
    rep.check('records: an entry that goes to the send sends the note as it left it, and stock\'s correction does not run',
              notes_of['send'] == ['on 50/100', 'off 50/0', 'on 62/90', 'off 62/0'] and count(r['send']['hits'], KEYED) == 0)
    corr = lambda nt: nt + table[(nt + 1) % 12]
    keyed = ['on %d/100' % corr(48), 'off %d/0' % corr(48), 'on %d/90' % corr(60), 'off %d/0' % corr(60)]
    rep.check('records: with a KEY offset (the ble.s not taken) stock corrects the note: %s' % keyed,
              notes_of['key'] == keyed and count(r['key']['hits'], KEYED) >= 2, str(notes_of['key']))

    bad = rep.failed
    print('\n%d checks, %d failed' % (len(rep.rows), len(bad)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())