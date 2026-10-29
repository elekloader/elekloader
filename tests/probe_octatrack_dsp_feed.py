# SPDX-License-Identifier: GPL-2.0-or-later
"""Check usb-ot's ot_dsp_feed in octabam's ColdFire port (ot_emu), with the Octatrack's own frame
interrupt and its transfer chain running.

What it shows (docs: mods/usb-ot/README.md, "Checked"):
  - the feed's state 7 runs: a handler on ev_dsp_feed is called each frame, and with a 16-byte
    aligned block ot_dsp_feed answers 1 and sets eDMA channel 0's source to the block;
  - a block that is not 16-byte aligned (4, 8 or 12 bytes off) is refused: it answers 0 and leaves
    channel 0's source as it was, so the controller is never aimed at it. On the unit a source
    4 bytes off a 16-byte boundary gave an eDMA source-address error that stopped the frame chain
    until a power cycle (Modwerk's hardware traps, OCTATRACK_HARDWARE_TRAPS.md); ot_emu does not
    model the fault, so only the refusal is shown here, not the error;
  - an aligned block 16 bytes on is taken again.

Run it where ot_emu runs (Linux or WSL), from a built octabam checkout (make emu-cf), with the
m68k cross tools on PATH (ELEKLOADER_CROSS):

    python tests/probe_octatrack_dsp_feed.py --emulator /path/to/octabam \
        --stock OCTATRACK_OS1.40C.syx [--card CARD.img] [--out DIR]
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


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--emulator', required=True, help="octabam checkout with out/emu/ot_emu (make emu-cf)")
    ap.add_argument('--stock', required=True, help='OCTATRACK_OS1.40C.syx')
    ap.add_argument('--card', help='a card image with a set and a project (default: made from an empty one)')
    ap.add_argument('--empty-card', help='an empty card image to make the project card from')
    ap.add_argument('--out', help='where the images and dumps go (default: a temporary folder)')
    a = ap.parse_args(argv)
    out = Path(a.out or tempfile.mkdtemp(prefix='dspfeed-'))
    out.mkdir(parents=True, exist_ok=True)
    rep = base.Report()

    st, dev, rel = formats.load(open(a.stock, 'rb').read())
    img = formats.main_image(st, dev)
    mods = out / 'mods'
    built = {}
    for key, d in (('core', 'mods/core-ot'), ('usb', 'mods/usb-ot'), ('probe', 'tests/fixtures/octatrack-dsp-feed')):
        built[key] = build.build(str(ROOT / d), a.stock, str(mods))[0]
    (out / 'stock.raw').write_bytes(img)
    pr = base.link_image([built['core'], built['usb'], built['probe']], img, out / 'probe.raw')
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
    pv = [(n, sym[n], 4) for n in ('df_calls', 'df_taken', 'df_refused', 'df_src', 'df_src_moved')]

    def cfg(on=1, offset=0):
        s = []
        for i, v in enumerate((on, offset)):
            s += ['%#x=%#x' % (sym['df_cfg'] + 4 * i + j, (v >> (24 - 8 * j)) & 0xff) for j in range(4)]
        return ';'.join(s)

    def vars_of(o):
        return {n: struct.unpack('>I', o[n])[0] for n, _a, _s in pv}

    scen = {'off': dict(pre=[cfg(0)]), 'aligned': dict(pre=[cfg(1, 0)]), 'plus16': dict(pre=[cfg(1, 16)]),
            'plus4': dict(pre=[cfg(1, 4)]), 'plus8': dict(pre=[cfg(1, 8)]), 'plus12': dict(pre=[cfg(1, 12)])}
    r = emu.run_many(out / 'probe.raw', scen, pv, tag='feed', watch=())
    v = {k: vars_of(o) for k, o in r.items()}
    base_addr = sym['df_buf'] + 0x08000000
    rep.check('the feed seam runs: the handler is called each frame (%d calls)' % v['off']['df_calls'],
              v['off']['df_calls'] > 100 and v['off']['df_taken'] == 0 == v['off']['df_refused'])
    # a feed holds the unmask until it completes: the next call is the visit its completion makes, so
    # a feeding handler is called about every other frame at most; what counts is that each call is answered
    for k, off in (('aligned', 0), ('plus16', 16)):
        x = v[k]
        rep.check('%s: an aligned block is taken each time it is offered (%d taken, %d refused) and channel 0 is aimed at it' % (k, x['df_taken'], x['df_refused']),
                  x['df_taken'] > 50 and x['df_refused'] == 0 and x['df_src'] == base_addr + off and x['df_src_moved'] == x['df_taken'],
                  '%s src %#x want %#x' % (x, x['df_src'], base_addr + off))
    for k, off in (('plus4', 4), ('plus8', 8), ('plus12', 12)):
        x = v[k]
        rep.check('%s: a block %d bytes off a 16-byte boundary is refused every time (%d refused) and channel 0 is never aimed at it' % (k, off, x['df_refused']),
                  x['df_refused'] > 50 and x['df_taken'] == 0 and x['df_src_moved'] == 0 and x['df_src'] != base_addr + off,
                  str(x))

    bad = rep.failed
    print('\n%d checks, %d failed' % (len(rep.rows), len(bad)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())