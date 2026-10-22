# SPDX-License-Identifier: GPL-2.0-or-later
"""Boot a build of core, machine-pages and examples/linked-knobs in an existing
digiemu checkout, and check cm_src_shown (machine-pages 1.5) where a page draws.

LINK's SHAPE knob (D) draws by its FORM knob (C), which it reads with
cm_src_shown, and records what it read in link_seen. Through the panel only:
FORM's two positions, SHAPE's ends and middle, a held step's lock on FORM, a
second track, and GLOBAL's menu over the page. Screens go to --out.

Run with the emulator's dependencies importable (its venv), and the cross compiler:
    python tests/probe_digitakt_shown.py --emulator /path/to/digiemu \\
        --stock Digitakt_OS1.53.syx --stock Digitakt_OS1.54.syx --out /tmp/shown

Output is a JSON report; nothing extracted from a stock OS is written but the
build under --out.
"""
import argparse
import contextlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from elekloader import formats  # noqa: E402
from elekloader.sdk import build  # noqa: E402

NO_VALUE = 0x80000000
FORM, SHAPE = 19, 20                     # knobs C and D: SLICE's slots 19 and 20


def firmware(stock, out):
    """-> the .syx of core, machine-pages and linked-knobs for `stock`, and its symbol map."""
    mods = [build.build(str(ROOT / d), str(stock), str(out))[0]
            for d in ('mods/core', 'mods/machine-pages', 'examples/linked-knobs')]
    syx = out / 'linked.syx'
    subprocess.run([sys.executable, '-m', 'elekloader.patch', '--stock', str(stock), '--out', str(syx),
                    '--version', 'LINK'] + [x for m in mods for x in ('--mod', m)],
                   check=True, capture_output=True, cwd=ROOT,
                   env=dict(os.environ, PYTHONPATH=str(ROOT)))
    return syx, json.loads(Path(str(syx) + '.map.json').read_text())


def probe(stock, out):
    from emu import panel
    from emu.bootstrap import first_run
    from emu.fwcheck import prepare
    from emu.longrun import never_fake_set_posts
    from emu.session import Session

    _s, dev, release = formats.load(str(stock))
    out.mkdir(parents=True, exist_ok=True)
    syx, symbols = firmware(stock, out)
    core = json.loads((ROOT / 'mods/core/mod.json').read_text())
    defs = core['defsym'] | core.get('ports', {}).get(release.version, {}).get('defsym', {})
    fw_kit = int(defs['FW_KIT'], 0)
    seen_at = int(symbols['linked-knobs:link_seen'], 0)
    with (out / 'boot.log').open('w') as log, contextlib.redirect_stdout(log):
        paths, device = prepare(str(syx), str(out / 'emulator'))
        os.environ.update(paths.env())
        session = Session(first_run(paths), paths.syx, hle=False)
        never_fake_set_posts(session.m, session.ev, session.profile.set_post)
    uc = session.m.uc
    shots = []

    def run(ms):
        session.run_ms(ms)
        assert not session.halted, session.halted

    def tap(key):
        session.tap(session.code(key), hold_ms=80, after_ms=150)
        assert not session.halted, session.halted

    def chord(modifier, key):
        session.press(session.code(modifier))
        run(80)
        try:
            tap(key)
        finally:
            session.release(session.code(modifier))
        run(150)

    def sound(track):
        kit = struct.unpack('>I', uc.mem_read(fw_kit, 4))[0]
        return list(struct.unpack('>53h', uc.mem_read(kit + 0x20 + 162 * track + 20, 106)))

    def seen():
        return list(struct.unpack('>4I', uc.mem_read(seen_at, 16)))

    def turn_to(track, slot, encoder, value):
        for _ in range(256):
            v = sound(track)[slot]
            if v == value:
                return
            step = min(8, max(1, abs(value - v) // 512))
            session.turn(encoder, step if value > v else -step, after_ms=150)
            assert not session.halted, session.halted
        raise AssertionError('knob %d did not reach %#x: %#x' % (encoder, value, sound(track)[slot]))

    def screen(name):
        panel.write_png(session.frames[-1][1], str(out / (name + '.png')), scale=4)
        shots.append(name + '.png')

    def drawn(name):
        """A fresh draw of the page: what SHAPE's draw read since."""
        count = seen()[3]
        run(300)
        s = seen()
        assert s[3] > count, ('SHAPE not drawn', name)
        panel.write_png(session.frames[-1][1], str(out / (name + '.png')), scale=4)
        shots.append(name + '.png')
        return s

    def select_link(track):
        chord('FUNC', 'SRC')                        # the machine menu: 0-3 stock, then LINK
        for _ in range(4):
            tap('DOWN')
        tap('YES')
        assert sound(track)[0] is not None
        kit = struct.unpack('>I', uc.mem_read(fw_kit, 4))[0]
        assert uc.mem_read(kit + 0x20 + 162 * track + 126, 1)[0] == 121, 'LINK not on track %d' % (track + 1)

    run(100)
    select_link(0)
    tap('SRC')
    checks = []

    # FORM's two positions: SHAPE's draw reads the one the page shows.
    turn_to(0, FORM, 3, 0)
    s = drawn('form-ramp')
    assert s[0] == 0 and s[1] == s[2] == sound(0)[SHAPE], s
    turn_to(0, FORM, 3, 0x100)
    s = drawn('form-pulse')
    assert s[0] == 0x100 and s[1] == s[2], s
    checks.append('FORM 0 and 1: SHAPE reads it; its own value through cm_src_shown is the one drawn')

    # SHAPE's ends and a middle: eight detents up from 0.
    turn_to(0, SHAPE, 4, 0)
    s = drawn('shape-min')
    assert s[1] == s[2] == 0 and s[0] == 0x100, s
    session.turn(4, 8, after_ms=300)
    middle = sound(0)[SHAPE]
    assert 0 < middle < 0x7f00, middle
    s = drawn('shape-mid')
    assert s[1] == s[2] == middle and s[0] == 0x100, s
    turn_to(0, SHAPE, 4, 0x7f00)
    s = drawn('shape-max')
    assert s[1] == s[2] == 0x7f00 and s[0] == 0x100, s
    checks.append('SHAPE at 0, %#x and 0x7f00: cm_src_shown(SHAPE) equals the value drawn' % middle)

    # A held step's lock on FORM: the page shows the lock, and so does the draw; the sound keeps 0.
    turn_to(0, FORM, 3, 0)
    tap('RECORD')                                   # grid recording: trig keys are steps
    screen('grid-record')
    session.press(session.code('#24'))              # trig 1, held: a trig on step 1, and its locks
    run(1000)
    screen('holding-1')
    for _ in range(4):
        session.turn(3, 1, after_ms=200)
    s = drawn('held-lock')
    assert sound(0)[FORM] == 0, sound(0)[FORM]
    assert s[0] == 0x100, ('the lock', s)
    session.release(session.code('#24'))
    run(300)
    s = drawn('released')
    assert s[0] == 0, ('after the lock', s)
    tap('RECORD')
    checks.append("a held step's lock on FORM: read while held, the sound's value after")

    # Track 2 on LINK with FORM 1, track 1 at 0: the draw follows the track shown.
    chord('TRK', '2')
    select_link(1)
    tap('SRC')
    turn_to(1, FORM, 3, 0x100)
    s = drawn('track-2')
    assert s[0] == 0x100 and sound(0)[FORM] == 0, s
    chord('TRK', '1')
    tap('SRC')
    s = drawn('track-1')
    assert s[0] == 0, s
    checks.append('two tracks on LINK: the draw reads the shown track')

    # GLOBAL's menu over the page: no knob of the page is drawn under it.
    tap('GLOBAL')
    count = seen()[3]
    run(500)
    panel.write_png(session.frames[-1][1], str(out / 'global.png'), scale=4)
    shots.append('global.png')
    under = seen()[3] - count
    tap('NO')
    run(300)
    checks.append('GLOBAL over the page: SHAPE drawn %d times under it' % under)

    return dict(device=dev.key, os=release.version, mode='booted; panel input only',
                checks=checks, screens=shots, no_value=hex(NO_VALUE))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--stock', type=Path, action='append', required=True)
    ap.add_argument('--emulator', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    sys.path.insert(0, str(args.emulator.resolve()))
    reports = [probe(p, args.out / formats.load(str(p))[2].version) for p in args.stock]
    print(json.dumps(reports, indent=2))


if __name__ == '__main__':
    main()
