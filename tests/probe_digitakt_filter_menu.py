# SPDX-License-Identifier: GPL-2.0-or-later
"""Boot core and machine-pages in a digiemu checkout and drive the machine
menu's filter tab (machine-pages 1.6) through the panel, reading the track's
sound from RAM:

    python tests/probe_digitakt_filter_menu.py --emulator /path/to/digiemu \\
        --stock Digitakt_OS1.53.syx --stock Digitakt_OS1.54.syx --out /tmp/menu

Writes each screen as a PNG, never firmware.
"""
import argparse
import contextlib
import glob
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from elekloader import formats  # noqa: E402


def firmware(stock, out):
    def run(*args):
        subprocess.run([sys.executable, '-m', *args], check=True, capture_output=True, cwd=ROOT,
                       env=dict(os.environ, PYTHONPATH=str(ROOT)))
    for mod in ('core', 'machine-pages'):
        run('elekloader.sdk.build', str(ROOT / 'mods' / mod), '--stock', str(stock), '--out', str(out))
    syx = out / 'menu.syx'
    mods = [x for name in ('core', 'machine-pages') for x in ('--mod', glob.glob(str(out / (name + '-*.elemod')))[0])]
    run('elekloader.patch', '--stock', str(stock), '--out', str(syx), '--version', 'MENU', *mods)
    return syx


def probe(stock, out):
    from emu import panel
    from emu.bootstrap import first_run
    from emu.fwcheck import prepare
    from emu.longrun import never_fake_set_posts
    from emu.session import Session

    _s, _dev, release = formats.load(str(stock))
    core = json.loads((ROOT / 'mods/core/mod.json').read_text())
    defs = core['defsym'] | core.get('ports', {}).get(release.version, {}).get('defsym', {})
    fw_kit, fw_active_track = int(defs['FW_KIT'], 0), int(defs['FW_ACTIVE_TRACK'], 0)
    out.mkdir(parents=True, exist_ok=True)
    syx = firmware(stock, out)
    with (out / 'boot.log').open('w') as log, contextlib.redirect_stdout(log):
        paths, _device = prepare(str(syx), str(out / 'emulator'))
        os.environ.update(paths.env())
        session = Session(first_run(paths), paths.syx, hle=False)
        never_fake_set_posts(session.m, session.ev, session.profile.set_post)
    uc = session.m.uc

    def sound():
        """The active track's sound in the UI kit: 162 bytes, its machine at +126."""
        kit = struct.unpack('>I', uc.mem_read(fw_kit, 4))[0]
        track = struct.unpack('>I', uc.mem_read(fw_active_track, 4))[0]
        return bytes(uc.mem_read(kit + 0x20 + 162 * track, 162))

    def run(ms):
        session.run_ms(ms)
        assert not session.halted, session.halted

    def tap(key):
        session.tap(session.code(key), hold_ms=80, after_ms=200)
        run(50)

    def menu():
        session.press(session.code('FUNC'))
        run(100)
        tap('SRC')
        run(100)
        session.release(session.code('FUNC'))

    def screen(name):
        run(300)
        pixels = session.frames[-1][1]
        panel.write_png(pixels, str(out / (name + '.png')), scale=4)
        return hashlib.sha256(bytes(pixels)).hexdigest()

    def body():
        """The screen below its header, where a page's title comes and goes."""
        return {p for p in panel.lit(session.frames[-1][1]) if p[1] >= 10}

    run(3000)
    tap('SRC')
    menu()
    machines = screen('machines')
    tap('NO')
    src_page = screen('src-page')           # as the menu leaves it: SRC on the page itself steps to the wave
    assert machines != src_page
    before = sound()
    menu()
    tap('RIGHT')
    filters = screen('filters')
    assert filters != machines, 'RIGHT shows the filter list'
    tap('RIGHT')
    assert screen('right-again') == filters
    tap('UP')
    tap('DOWN')
    assert screen('up-down') == filters, 'nothing to step to'
    tap('LEFT')
    assert screen('left') == machines, 'LEFT brings the machines back'
    tap('RIGHT')
    tap('YES')
    assert screen('yes') == src_page, 'YES closes and the machine stays'
    assert sound() == before, 'YES on the filter tab writes nothing'
    menu()
    assert screen('machines-again') == machines, 'a menu opens on its machines'
    tap('NO')
    run(200)
    tap('FLTR')
    screen('fltr-page')
    fltr_page = body()
    before = sound()
    menu()
    assert screen('from-fltr') == filters, 'from FLTR: the filter list'
    tap('YES')
    screen('from-fltr-yes')
    assert sound() == before, 'from FLTR too'
    assert body() == fltr_page, 'YES goes back to FLTR'
    menu()
    tap('NO')
    screen('from-fltr-no')
    assert body() == fltr_page, 'NO goes back to FLTR'
    menu()
    tap('LEFT')
    assert screen('from-fltr-left') == machines
    tap('YES')
    screen('from-fltr-machines-yes')
    assert body() == fltr_page, 'from the machine list too'
    tap('AMP')
    screen('amp-page')
    amp_page = body()
    assert amp_page != fltr_page
    menu()
    assert screen('from-amp') == machines, 'from AMP: the machines'
    tap('NO')
    tap('FLTR')
    run(200)
    menu()
    tap('AMP')
    screen('from-fltr-amp-key')
    assert body() == amp_page, 'a page key goes to its own page'
    menu()
    tap('NO')
    screen('after-page-key')
    assert body() != fltr_page, 'and leaves nothing to go back to'

    # A machine stepped to on the machine list, then RIGHT and YES: committed as YES on the list commits it.
    start = sound()[126]
    menu()
    tap('DOWN')
    tap('YES')
    stepped = sound()[126]
    assert stepped != start, 'DOWN and YES on the machine list commit the next machine'
    menu()
    tap('UP')
    tap('YES')
    assert sound()[126] == start
    menu()
    tap('DOWN')
    tap('RIGHT')
    screen('stepped-filters')
    tap('YES')
    screen('stepped-yes')
    assert sound()[126] == stepped, 'the filter tab\'s YES commits the machine stepped to'
    return machines, filters


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--emulator', required=True)
    ap.add_argument('--stock', action='append', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    sys.path.insert(0, a.emulator)
    os.chdir(a.emulator)
    for stock in a.stock:
        print(Path(stock).name, probe(Path(stock).resolve(), Path(a.out).resolve() / Path(stock).stem))
    print('OK')


if __name__ == '__main__':
    main()
