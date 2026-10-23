# SPDX-License-Identifier: GPL-2.0-or-later
"""Check ev_step_built (mods/seq-events-ot) in octabam's ColdFire port (ot_emu), with the Octatrack's own sequencer running.

What it shows (docs: mods/seq-events-ot/README.md, "The staged record"):
  - alone it is stock: with a handler that changes nothing, on its own (no step stage has a handler) and beside
    seq-events' own stage handlers, the pending step records of every track, the pattern's memory and the MIDI out
    are the stock sequencer's, byte for byte;
  - the handler is given what the builder staged, where the tick reads it: the arguments (track, bank, pattern,
    the step the build read, n), the pointers, and each word equals what the tick later puts in the pending slot;
    the pattern is as stored at that moment, also while a stage handler has changed the step for the build;
  - what PLOCKS P2 (the fill), MIDI SCENES (the staged row) and REC TRIG MUTE (the recorder trig) do at the
    builders, done on that record by a handler, arrives in the pending records and the MIDI out: a cancelled step,
    a lock, a time offset, a muted recorder trig, a snapshot, a table lookup by (bank, pattern, track, step, n);
  - the builds the firmware's edit path makes from a task, nested with the tick's, leave the tick's records alone.

Run it where ot_emu runs (Linux or WSL), from a built octabam checkout (make emu-cf), with the m68k cross tools on
PATH (ELEKLOADER_CROSS) as for the SDK's tests:

    python tests/probe_octatrack_step_built.py --emulator /path/to/octabam \\
        --stock OCTATRACK_OS1.40C.syx [--card CARD.img] [--out DIR] [--only NAME ...]

It uses the helpers of probe_octatrack_seq_events.py (the card, the pattern, the emulator's runs).
"""
import argparse
import struct
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tests'))

from elekloader import formats                                        # noqa: E402
from elekloader.sdk import build                                      # noqa: E402
from probe_octatrack_seq_events import (BASE, DUMPS, END, NONE, PATTERN, REGIONS, WIDE, BANK0, Emu, Report,  # noqa: E402
                                        decode, expect_records, fixture, link_image, make_card, notes_of, records)

# the pending slot 0 beyond the base probe's REGIONS: the recorder trigs' flags and time, the step's length and
# span, and the slide's steps (the tick's copies of the staged words)
EXTRA = [('arflag', 0x46c7faa4, 96), ('artime', 0x80001904 + 128, 96), ('alen', 0x46c7fa20, 32),
         ('aspan', 0x46c802e8, 32), ('adelta', 0x46c76ac0, 256)]
CFG = ['log', 'mask', 'cancel', 'shift', 'lock', 'mute', 'scenes', 'p2', 'rtime', 'nest']      # struct cfg in probe.c
STAGE_CFG = ['mask', 'gen', 'drop', 'remap', 'cond', 'notes', 'vel', 'locks', 'gnotes']   # the seq-events probe's
LOGMAX = 80
ENT = '>6b2xIIIiIi32B32b7I'             # struct ent in probe.c
assert struct.calcsize(ENT) == 124
FIRE = 0x11f                            # the tracks whose builds are logged: T1-T5, M1


def rec_fixture():
    """The base fixture and recorder trigs: T1 step 5 REC1, T2 steps 3 and 6 REC2, T3 step 4 REC3."""
    p = fixture()
    p.mask(0, 0x20, [5])
    p.mask(1, 0x28, [3, 6])
    p.mask(2, 0x30, [4])
    return p


def timed_fixture():
    """The base fixture with T1's step 8 given a micro-timing of +8 and its step 4 swung by 20 (a swung step sends the
    next one to pending slot 1, so this one is read from the log only, not from slot 0)."""
    p = fixture()
    p.timing(0, 8, 0x04, 0x00)
    p.mask(0, 0x40, [4])
    p.b[p.rec(0) + 0x52] = 20
    return p


REC_FLAGS = {(0, 5): 0x1020, (1, 3): 0x2020, (1, 6): 0x2020, (2, 4): 0x4020}     # (track, step): the staged flags


def poke(at, names, **kw):
    s = []
    for i, k in enumerate(names):
        v = kw.get(k, 0) & 0xffffffff
        s += ['%#x=%#x' % (at + 4 * i + j, (v >> (24 - 8 * j)) & 0xff) for j in range(4)]
    return ';'.join(s)


def want_ptrs(track, n):
    """Where rec should point (locks, deltas, kind, length, span, time, recorder): the builders' own arrays."""
    t, o = track & 7, 4 * (track & 7)
    if track < 8:
        locks = (0x46c7aa24 if n < 0 else 0x46c7ab30 + 256 * n) + 32 * t
        deltas = (0x46c77c32 if n < 0 else 0x46c76ac0 + 256 * n) + 32 * t
        rec = 0x46c7a6c0 + o
    else:
        locks = (0x46c7a6f0 if n < 0 else 0x46c78960 + 256 * n) + 32 * t
        deltas = rec = 0
    return (locks, deltas, 0x46c7a810 + o, 0x46c7a14c + o, 0x46c77bfa + o, 0x46c7a830 + o, rec)


def words(b):
    return struct.unpack('>%di' % (len(b) // 4), b)


def parse_log(d):
    raw = (Path(d) / 'bplog.bin').read_bytes()
    n = struct.unpack('>I', (Path(d) / 'bpn.bin').read_bytes())[0]
    out = []
    for i in range(min(n, LOGMAX)):
        v = struct.unpack(ENT, raw[124 * i:124 * i + 124])
        out.append(dict(track=v[0], bank=v[1], pattern=v[2], step=v[3], n=v[4], seen=v[5], kind=v[6], length=v[7],
                        span=v[8], time=v[9], recorder=v[10], rec_time=v[11], locks=bytes(v[12:44]),
                        deltas=bytes(x & 255 for x in v[44:76]), ptr=v[76:83]))
    return out, n


def calls_of(d):
    return struct.unpack('>8I', (Path(d) / 'bpcalls.bin').read_bytes())


def locks_of(e):
    return {i: b for i, b in enumerate(e['locks']) if b != 255}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--emulator', required=True, help="octabam checkout with out/emu/ot_emu (make emu-cf)")
    ap.add_argument('--stock', required=True, help='OCTATRACK_OS1.40C.syx')
    ap.add_argument('--card', help='a card image with a set and a project (default: made from an empty one)')
    ap.add_argument('--empty-card', help='an empty card image to make the project card from')
    ap.add_argument('--out', help='where the images and dumps go (default: a temporary folder)')
    ap.add_argument('--only', nargs='*', help='run only these checks (alone, log, edits, p2, combined, nesting)')
    a = ap.parse_args(argv)
    out = Path(a.out or tempfile.mkdtemp(prefix='stepbuilt-'))
    out.mkdir(parents=True, exist_ok=True)
    want = lambda n: not a.only or n in a.only
    rep = Report()

    # ---- the images: stock; core; core and seq-events; with the event's probe; with the stage probe too
    st, dev, rel = formats.load(open(a.stock, 'rb').read())
    img = formats.main_image(st, dev)
    mods = out / 'mods'
    built = {}
    for key, d in (('core', 'mods/core-ot'), ('seq', 'mods/seq-events-ot'),
                   ('bprobe', 'tests/fixtures/octatrack-step-built'), ('sprobe', 'tests/fixtures/octatrack-seq-events')):
        built[key] = build.build(str(ROOT / d), a.stock, str(mods))[0]
    (out / 'stock.raw').write_bytes(img)
    link_image([built['core']], img, out / 'core.raw')
    link_image([built['core'], built['seq']], img, out / 'seq.raw')
    pb = link_image([built['core'], built['seq'], built['bprobe']], img, out / 'built.raw')
    pa = link_image([built['core'], built['seq'], built['sprobe'], built['bprobe']], img, out / 'both.raw')
    sb, sa = pb.map, pa.map

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
    chan = '0x46c76de0=0x1;0x40171442=0x1;0x4017770c=0x1'      # M1 on channel 1: the live copy, the working and saved Parts
    pat_dump = ['%d:dump:%#x,%d={D}/pat.bin' % (END, BANK0, PATTERN)]

    def tail(sym):
        """the end-of-run dump of the probe's log, counters and tables"""
        return ['%d:dump:%#x,%d={D}/bplog.bin;%#x,32={D}/bpcalls.bin;%#x,4={D}/bpn.bin;%#x,96={D}/bpp2.bin;'
                '%#x,96={D}/bpstage.bin;%#x,256={D}/bpsnap.bin' % (
                    END, sym['bp_log'], LOGMAX * 124, sym['bp_calls'], sym['bp_n'], sym['bp_p2pend'],
                    sym['bp_p2stage'], sym['bp_snap'])]

    def regions(sym):
        return REGIONS + EXTRA + [('p2', sym['bp_p2pend'], 96), ('snap', sym['bp_snap'], 256)]

    def bcfg(sym, **kw):
        return poke(sym['bp_cfg'], CFG, **kw)

    def run(raw, sym, scen, tag, jobs=3):
        for sc in scen.values():
            sc['extra'] = [x for x in sc.get('extra', []) if x not in pat_dump] + pat_dump + tail(sym)
        return emu.run_many(out / raw, scen, regions(sym), tag=tag, jobs=jobs)

    def same_pending(x, y, regs=REGIONS + EXTRA):
        return all(x[(f, nm)] == y[(f, nm)] for f in DUMPS for nm, _a, _n in regs)

    # ---- alone it is stock
    if want('alone'):
        scen = {'a': dict(pat=fixture(), pre=[chan], midi=True, extra=list(pat_dump)),
                'r': dict(pat=rec_fixture(), pre=[chan], midi=True, extra=list(pat_dump))}
        res = {k: emu.run_many(out / (k + '.raw'), scen, REGIONS + EXTRA, jobs=2, tag='alone-' + k)
               for k in ('stock', 'seq')}
        sc2 = {k: dict(v, pre=[chan, bcfg(sb)], extra=[]) for k, v in scen.items()}
        res['inert'] = run('built.raw', sb, sc2, 'alone-inert', 2)
        sc3 = {k: dict(v, pre=[chan, bcfg(sa), poke(sa['sp_cfg'], STAGE_CFG)], extra=[]) for k, v in scen.items()}
        res['both'] = run('both.raw', sa, sc3, 'alone-both', 2)
        for k in ('a', 'r'):
            s = res['stock'][k]
            rep.check('alone/%s: the fixture stages as stock does' % k, records(s) == expect_records({}) if k == 'a' else True)
            for who in ('seq', 'inert', 'both'):
                o = res[who][k]
                rep.check('alone/%s/%s: pending records (kind, locks, time, recorder, length, span, slide) are stock\'s' % (k, who),
                          same_pending(o, s))
                rep.check('alone/%s/%s: the pattern memory and the MIDI out are stock\'s' % (k, who),
                          o['pat.bin'] == s['pat.bin'] and o['midi.out'] == s['midi.out'])
        for who in ('inert', 'both'):
            c = calls_of(res[who]['a']['dir'])
            rep.check('alone/%s: the handler ran for every build (%d calls, %d staged, %d with n < 0)' % (who, c[0], c[1], c[3]),
                      c[0] >= 16 * 8 and c[1] >= 20 and c[3] >= 16, str(c))
        r = res['stock']['r']
        rep.check('alone: the recorder fixture stages recorder trigs in stock',
                  any(w for f in DUMPS for w in words(r[(f, 'arflag')])))

    # ---- what the handler is given
    if want('log'):
        fl = dict(log=FIRE)
        sc = {'log': dict(pat=rec_fixture(), pre=[bcfg(sb, **fl)], extra=[])}
        r = run('built.raw', sb, sc, 'log')['log']
        ents, total = parse_log(r['dir'])
        ns = sorted({e['n'] for e in ents})
        rep.check('log: %d builds of 5 tracks logged, none lost (%d), n in %s' % (len(ents), total, ns),
                  0 < len(ents) == total <= LOGMAX and set(ns) <= {-1, 0, 1, 2} and 0 in ns and -1 in ns, str((total, ns)))
        rep.check('log: bank 0, pattern 0, steps 0-15, the tracks asked for',
                  all(e['bank'] == 0 and e['pattern'] == 0 and 0 <= e['step'] < 16 and (FIRE >> e['track']) & 1 for e in ents))
        bad = [(e['track'], e['n'], [hex(x) for x in e['ptr']], [hex(x) for x in want_ptrs(e['track'], e['n'])])
               for e in ents if tuple(e['ptr']) != want_ptrs(e['track'], e['n'])]
        rep.check('log: rec points at the builders\' own arrays, for every track kind and n (-1 included)', not bad, str(bad[:2]))
        # what the stock sequencer stages for the steps the dumps cover: the fixture's own model, BASE
        wrong = []
        for e in ents:
            if 1 <= e['step'] <= 8 and e['n'] >= 0:
                kind, lk = BASE.get(e['step'], {}).get(e['track'], NONE)
                if (e['kind'], locks_of(e) if e['kind'] else {}) != (kind, lk):
                    wrong.append((e['track'], e['step'], e['n'], hex(e['kind']), locks_of(e)))
        rep.check('log: the staged kind and lock bytes are the sequencer\'s, steps 1-8', not wrong, str(wrong[:3]))
        rep.check('log: the pattern is as stored when the event runs (AMP VOL / VEL of the step: the fixture\'s)', all(
            (e['seen'] & 255) == fixture().b.get(fixture().rec(e['track']) + (0x59 if e['track'] < 8 else 0x30)
                                                + 32 * e['step'] + (15 if e['track'] < 8 else 1), 255) for e in ents))
        flags = {(e['track'], e['step']): e['recorder'] for e in ents if e['recorder']}
        rep.check('log: the recorder trigs: REC1, REC2, REC3 flags on their steps, 0 elsewhere; MIDI has none',
                  all(REC_FLAGS.get(k) == v for k, v in flags.items()) and set(REC_FLAGS) <= set(flags) and
                  all(not e['recorder'] and not e['rec_time'] for e in ents if e['track'] >= 8), str(flags))
        t4 = [e for e in ents if e['track'] == 0 and e['kind'] == 0x1d]
        rep.check('log: a trigger trig has length index 1 and a span; a step with no record has kind 0',
                  all(e['length'] == 1 and e['span'] > 0 for e in t4) and t4 and all(
                      e['kind'] == 0 for e in ents if e['track'] == 0 and e['step'] in (1, 3, 5)), str(t4[:1]))
        l4 = {x['step']: (x['length'], x['span']) for x in ents if x['track'] == 3 and x['kind'] == 0x1d and x['n'] == 0}
        rep.check('log: the length index comes from the timing halfword (T4 step 6: 2, the others 1), and the span is halved with it',
                  l4.get(6, (0, 0))[0] == 2 and l4.get(3, (0, 0))[0] == 1 and 2 * l4[6][1] == l4[3][1], str(l4))
        # the time word: micro-timing (+8 on T1 step 8) and swing (20 on T1 step 4), in the unit of the track's rate
        o = run('built.raw', sb, {'timed': dict(pat=timed_fixture(), pre=[bcfg(sb, **fl)], extra=[])}, 'log-timed')['timed']
        tm = {x['step']: x['time'] for x in parse_log(o['dir'])[0] if x['track'] == 0 and x['kind'] == 0x1d and x['n'] == 0}
        rate = tm.get(8, 0) / (8 * 110250)
        rep.check('log: the time word is the step\'s micro-timing (+8: %d) plus its swing (20: %d), 0 without, one rate for both (%g)' % (
            tm.get(8, 0), tm.get(4, 0), rate), tm.get(8, 0) > 0 and rate == int(rate) and tm.get(4) == 20 * 52920 * int(rate) and
            tm.get(2) == 0, str(tm))
        sl = [e for e in ents if e['track'] == 4 and e['kind'] and any(e['deltas'])]
        rep.check('log: the slide trig on T5 stages its per-lock steps; MIDI tracks have none',
                  len(sl) > 0 and all(not any(e['deltas']) for e in ents if e['track'] >= 8), str(len(sl)))
        # the end-to-end check: slot 0's builds against what the tick put in the pending slot 0
        o = r
        bad = []
        for e in ents:
            if e['n'] == 0 and 1 <= e['step'] <= 8:
                f, t = DUMPS[e['step'] - 1], e['track']
                kind, lk = decode(o, f)[t]
                ln = words(o[(f, 'alen')])[t] if t < 8 else None
                sp = words(o[(f, 'aspan')])[t] if t < 8 else None
                dl = o[(f, 'adelta')][32 * t:32 * t + 32] if t < 8 else None
                if (e['kind'], locks_of(e) if e['kind'] else {}) != (kind, lk) or (
                        t < 8 and e['kind'] and (e['length'], e['span'], e['deltas']) != (ln, sp, dl)):
                    bad.append((t, e['step'], hex(e['kind']), lk, kind))
        rep.check('log: the slot-0 builds equal the pending slot 0 the tick filled (kind, locks, length, span, slide steps)',
                  not bad and any(e['n'] == 0 and e['kind'] for e in ents), str(bad[:3]))

    # ---- what the three modules do at the builders, on the staged record
    if want('edits'):
        base_sc = dict(pat=rec_fixture(), pre=[chan, bcfg(sb)], midi=True)
        sc = {'base': base_sc,
              'cancel': dict(base_sc, pre=[chan, bcfg(sb, mask=0x101, cancel=1)]),
              'lock': dict(base_sc, pre=[chan, bcfg(sb, mask=0x101, lock=77)]),
              'shift': dict(base_sc, pre=[chan, bcfg(sb, mask=0x08, shift=1234)]),
              'mute': dict(base_sc, pre=[chan, bcfg(sb, mask=0x07, mute=1)]),
              'rtime': dict(base_sc, pre=[chan, bcfg(sb, mask=0x02, rtime=500)]),
              'scenes': dict(base_sc, pre=[chan, bcfg(sb, mask=0x100, scenes=1)])}
        r = run('built.raw', sb, sc, 'edits')
        b = r['base']
        brec = records(b)
        n_base = notes_of(b['midi.out'])
        rep.check('edits/base: inert, the recorder fixture stages as stock does', brec == expect_records({}) and
                  n_base == ['on 48/100', 'off 48/0', 'on 60/90', 'off 60/0'], str(n_base))
        # REC TRIG MUTE: the staged recorder word of T1-T3 is 0, so nothing starts; the rest is untouched
        bad, seen = [], 0
        for f in DUMPS:
            wb, wm = words(b[(f, 'arflag')]), words(r['mute'][(f, 'arflag')])
            seen += sum(1 for i, w in enumerate(wb) if w and i % 8 < 3)
            bad += [i for i in range(24) if wm[i] != (0 if i % 8 < 3 else wb[i])]
        rep.check('edits/mute: REC TRIG MUTE\'s gate on the staged word: T1-T3\'s recorder flags are 0 in the pending slots '
                  '(%d staged in stock), the others and every time word as stock' % seen, seen >= 3 and not bad and same_pending(
                      r['mute'], b, [x for x in REGIONS + EXTRA if x[0] != 'arflag']), str(bad[:4]))
        # the recorder trig's time offset (the word of a slot with no recorder trig is the last one's: not asked)
        bad, shifted = [], 0
        for f in DUMPS:
            wb, wr = words(b[(f, 'artime')]), words(r['rtime'][(f, 'artime')])
            wf = words(b[(f, 'arflag')])
            for i in range(24):
                if i % 8 == 1 and wf[i]:
                    shifted += 1
                    if wr[i] != wb[i] + 500:
                        bad.append((f, i))
                elif i % 8 != 1 and wr[i] != wb[i]:
                    bad.append((f, i))
        rep.check('edits/rtime: rec_time moves T2\'s recorder trig\'s time word by +500 in the pending slot (%d), the other '
                  'tracks\' are stock\'s' % shifted, shifted >= 2 and not bad, str(bad[:4]))
        # cancel: T1's and M1's staged steps are gone, as if they had no trig
        want_none = {(s, t): NONE for s in (2, 4, 8) for t in (0,)}
        want_none.update({(4, 8): NONE, (8, 8): NONE})
        rep.check('edits/cancel: *kind = 0 on T1 and M1: their trigs are not in the pending records',
                  records(r['cancel']) == expect_records(want_none), str({k: v for k, v in records(r['cancel']).items()
                                                                          if v != expect_records(want_none)[k]})[:300])
        rep.check('edits/cancel: M1 sends no note (the first step\'s too)', notes_of(r['cancel']['midi.out']) == [],
                  str(notes_of(r['cancel']['midi.out'])))
        # lock: AMP VOL on T1, VEL on M1, over a stored lock, on a record the builder staged
        ch = {(2, 0): (0x1d, {15: 77}), (4, 0): (0x1d, {0: 70, 15: 77}), (8, 0): (0x1d, {15: 77}),
              (4, 8): (0x1d, {0: 60, 1: 77}), (8, 8): (0x1d, {1: 77, 3: 71})}
        rep.check('edits/lock: AMP VOL 77 on T1 over a stored lock, VEL 77 on M1',
                  records(r['lock']) == expect_records(ch), str({k: v for k, v in records(r['lock']).items()
                                                                 if v != expect_records(ch)[k]})[:300])
        rep.check('edits/lock: the note-ons M1 sends have velocity 77 (the first step\'s, built at PLAY, too)',
                  notes_of(r['lock']['midi.out']) == ['on 48/77', 'off 48/0', 'on 60/77', 'off 60/0'], str(notes_of(r['lock']['midi.out'])))
        # shift: the time word of T4 (the word of a step with no record is the last one's: not asked)
        bad, shifted = [], 0
        for f in DUMPS:
            tb, ts = words(b[(f, 'atime')]), words(r['shift'][(f, 'atime')])
            for i in range(8):
                if i == 3:
                    if decode(b, f)[3][0]:
                        shifted += 1
                        if ts[i] != tb[i] + 1234:
                            bad.append((f, i))
                elif ts[i] != tb[i]:
                    bad.append((f, i))
        rep.check('edits/shift: *time += 1234 moves T4\'s staged times in the pending slot (%d records), the other tracks\' '
                  'are stock\'s' % shifted, shifted >= 2 and not bad, str(bad[:4]))
        rep.check('edits/shift: the kinds and locks are stock\'s', records(r['shift']) == brec)
        # MIDI SCENES: the staged row of M1 was copied before the edit: the last staged build's row is step 8's
        sn = (Path(r['scenes']['dir']) / 'bpsnap.bin').read_bytes()[:32]
        rep.check('edits/scenes: bp_snap holds M1\'s last staged row, as the builder wrote it', sn == bytes([255] * 3 + [71] + [255] * 28),
                  sn.hex())
        rep.check('edits/scenes: and changes nothing else', records(r['scenes']) == brec and
                  notes_of(r['scenes']['midi.out']) == n_base)
        rep.check('edits: the pattern is put back after every run', all(v['pat.bin'] == b['pat.bin'] for v in r.values()))

    # ---- PLOCKS P2's fill: a table keyed by (bank, pattern, track, step), in the slot n the build fills
    if want('p2'):
        sc = {'p2': dict(pat=rec_fixture(), pre=[chan, bcfg(sb, mask=0xff, p2=1)], midi=True, extra=[])}
        o = run('built.raw', sb, sc, 'p2')['p2']
        bad, fired = [], 0
        for i, f in enumerate(DUMPS):
            s = i + 1
            tbl = o[(f, 'p2')]
            for t in range(8):
                if decode(o, f)[t][0]:
                    fired += 1
                    exp = bytes((0 * 37 + 0 * 11 + t * 7 + s * 3 + j) & 255 for j in range(12))
                    got = tbl[12 * t:12 * t + 12]
                    if got != exp:
                        bad.append((s, t, got.hex(), exp.hex()))
        rep.check('p2: at each dump, the table row of every track with a staged record in slot 0 is the one for its '
                  '(bank, pattern, track, step) (%d records)' % fired, fired >= 8 and not bad, str(bad[:3]))
        stage = (Path(o['dir']) / 'bpstage.bin').read_bytes()
        rep.check('p2: the builds with n = -1 (the first step, at PLAY) fill the staging rows: T1 and T4 trig on step 0',
                  all(stage[12 * t:12 * t + 12] == bytes((t * 7 + j) & 255 for j in range(12)) for t in (0, 3)), stage.hex()[:60])
        rep.check('p2: records are stock\'s', records(o) == expect_records({}))

    # ---- beside the step stages
    if want('combined'):
        fl = dict(log=0x1, mask=0)
        sc = {'locks': dict(pat=fixture(), pre=[chan, bcfg(sa, **fl), poke(sa['sp_cfg'], STAGE_CFG, mask=1, locks=1)], extra=[]),
              'remap': dict(pat=fixture(), pre=[chan, bcfg(sa, **fl), poke(sa['sp_cfg'], STAGE_CFG, mask=1, remap=1)], extra=[]),
              'euclid': dict(pat=fixture(), pre=[chan, bcfg(sa, **fl), poke(sa['sp_cfg'], STAGE_CFG, mask=1, gen=1)], extra=[]),
              'cancel': dict(pat=fixture(), pre=[chan, bcfg(sa, mask=1, cancel=1), poke(sa['sp_cfg'], STAGE_CFG, mask=1, gen=1)], extra=[])}
        r = run('both.raw', sa, sc, 'combined')
        e, _ = parse_log(r['locks']['dir'])
        rep.check('combined/locks: the event sees the locks the stage added (AMP VOL 50 + step, slot 0 = 99) and the pattern as stored',
                  e and all(x['locks'][15] == 50 + x['step'] and x['locks'][0] == 99 for x in e if x['kind'] and x['n'] >= 0 and 1 <= x['step'] <= 8) and
                  all(x['seen'] & 255 == (100 if x['step'] == 4 else 255) for x in e), str([(x['step'], x['kind'], x['locks'][15]) for x in e][:6]))
        e, _ = parse_log(r['remap']['dir'])
        t1 = (0, 2, 4, 8, 12)
        rep.check('combined/remap: step is the step the build read: the mirrored one (15 - the real step), with its record',
                  {(x['step'], x['kind']) for x in e if x['n'] == 0 and 7 <= x['step'] <= 14} ==
                  {(15 - s_, 0x1d if 15 - s_ in t1 else 0) for s_ in range(1, 9)} and all(7 <= x['step'] <= 15 for x in e),
                  str(sorted({(x['step'], x['kind']) for x in e if x['n'] == 0})))
        e, _ = parse_log(r['euclid']['dir'])
        rep.check('combined/euclid: a generated trig has kind 0x1d and no locks',
                  any(x['kind'] == 0x1d and not locks_of(x) and x['step'] == 7 for x in e), str([(x['step'], hex(x['kind'])) for x in e if x['n'] == 0]))
        rep.check('combined/cancel: a Euclidean rhythm on T1, cancelled by the event: no record on T1',
                  all(records(r['cancel'])[s][0] == NONE for s in range(1, 9)))
        rep.check('combined: the pattern is put back', all(v['pat.bin'] == r['locks']['pat.bin'] for v in r.values()))

    # ---- a build nested in a build: the handler of every tick build starts the stock builder for T8 (it has no
    # trig, so nothing is staged) from inside, with n = -1, as the firmware's edit path does from a task. (The
    # firmware's own edit routine, 0x4009da20, builds nothing while the sequencer plays, so calling it from the
    # emulator does not do this: a watch on the builder's entry counts the same entries with and without the calls.)
    if want('nesting'):
        everything = dict(mask=0x7f, gen=1, notes=1, vel=1, locks=1)               # every track but T8
        edit = dict(mask=0xff7f, lock=61, shift=100, mute=1, log=0x80)
        sc = {'flat': dict(pat=fixture(), pre=[bcfg(sa, **edit), poke(sa['sp_cfg'], STAGE_CFG, **everything)], extra=[]),
              'nested': dict(pat=fixture(), pre=[bcfg(sa, nest=1, **edit), poke(sa['sp_cfg'], STAGE_CFG, **everything)], extra=[])}
        r = run('both.raw', sa, sc, 'nesting')
        c, f0 = calls_of(r['nested']['dir']), calls_of(r['flat']['dir'])
        rep.check('nesting: %d builds started inside a handler ran, each with its own event and the arguments it was started with '
                  '(%d, %d)' % (c[4], c[6], c[7]), c[4] >= 100 and c[5] == c[4] and c[6] == c[4] and c[7] == 0 and f0[4:] == (0, 0, 0, 0),
                  str((f0, c)))
        rep.check('nesting: every tick build\'s event still ran: %d, as without the nested builds (%d)' % (c[0] - c[5], f0[0]),
                  c[0] - c[5] == f0[0])
        rep.check('nesting: the tick\'s pending records (kinds, locks, times, recorder, length, span, slide steps) are the same with them',
                  same_pending(r['flat'], r['nested']))
        stock = emu.run_many(out / 'stock.raw', {'p': dict(pat=fixture(), extra=list(pat_dump))}, tag='nesting-stock')['p']
        rep.check('nesting: the pattern is put back', r['flat']['pat.bin'] == r['nested']['pat.bin'] == stock['pat.bin'])
        e = [x for x in parse_log(r['nested']['dir'])[0] if x['n'] == -1 and x['step'] == 1]
        rep.check('nesting: the nested build\'s event (%d logged): T8, bank 0, pattern 0, step 1, n = -1, nothing staged, rec at T8\'s '
                  'staging words' % len(e), len(e) >= 10 and all(
                      (x['track'], x['bank'], x['pattern'], x['kind'], x['recorder']) == (7, 0, 0, 0, 0) and
                      tuple(x['ptr']) == want_ptrs(7, -1) for x in e), str(e[:1]))

    bad = rep.failed
    print('\n%d checks, %d failed' % (len(rep.rows), len(bad)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
