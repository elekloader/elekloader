# SPDX-License-Identifier: GPL-2.0-or-later
"""PR-1 UI acceptance through DigiKit's normal panel controls; no UI-memory writes.

Build sdk-hello-world, both alpha fixtures and tests/test_sdk.py's C_MACHINE as PAGETEST (slot 30).
The patched firmware's map must be beside it as FIRMWARE.syx.map.json.
Pass a main-branch machine-pages + PAGETEST build as --baseline to compare screens.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys


def sdk_alpha_main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--digikit', type=Path, required=True)
    ap.add_argument('--daemon', type=Path, required=True)
    ap.add_argument('--stock', type=Path, required=True)
    ap.add_argument('--firmware', type=Path, required=True)
    ap.add_argument('--baseline', type=Path)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    a.out = a.out.resolve()
    sys.path.insert(0, str(a.digikit.resolve()))
    from tools.control.digi_client import Emu

    a.out.mkdir(parents=True, exist_ok=True)
    symbols = json.loads(Path(str(a.firmware) + '.map.json').read_text())
    sha = hashlib.sha256(a.stock.read_bytes()).hexdigest()
    assert sha == 'f78ba80fa7b1da5fb0e1ff61ad61e9e71aafe79f4364fc49679f3651353e3cf6', 'needs stock DT 1.54'
    report = {'stock_sha256': sha,
              'firmware_sha256': hashlib.sha256(a.firmware.read_bytes()).hexdigest(),
              'daemon_sha256': hashlib.sha256(a.daemon.read_bytes()).hexdigest()}
    with Emu.spawn(str(a.stock.resolve()), binary=str(a.daemon.resolve()),
                   run_dir=a.out / 'run') as e:
        def sdk_alpha_read(name):
            return e.peek32(int(symbols[name], 16))

        def sdk_alpha_tap(key):
            e.tap(key, hold=10_000_000)
            e.step(30_000_000)

        def sdk_alpha_chord(modifier, key):
            e.button(modifier, True)
            e.step(15_000_000)
            sdk_alpha_tap(key)
            e.button(modifier, False)
            e.step(20_000_000)

        def sdk_alpha_select(current, wanted):
            sdk_alpha_chord(1, 20)       # FUNC+SRC: native machine menu
            for _ in range(abs(wanted - current)):
                sdk_alpha_tap(15 if wanted > current else 14)  # DOWN / UP
            sdk_alpha_tap(12)            # YES
            sdk_alpha_tap(20)            # SRC

        def sdk_alpha_machine(track=0):
            sound = sdk_alpha_read('fw_kit') + 0x20 + 0xa2 * track
            return (e.peek32(sound + 0x7c) >> 8) & 255

        def sdk_alpha_screen(name, empty=False):
            frame = e.frame()[1]
            (a.out / (name + '.png')).write_bytes(e.frame_png())
            if empty:
                # The device owns blank-position graphics, including its native dotted placeholders.
                layout = int(symbols['sdk_alpha_empty_layout'], 16)
                assert all(e.peek32(layout + 4 * (2 + k)) == 0 for k in range(8)), name
            assert e.status()['cf']['error'] is None
            return frame

        print('Booting alpha firmware', flush=True)
        report['boot'] = e.load(a.firmware.resolve(), base_sha256=sha)
        e.step(20_000_000)
        sdk_alpha_tap(20)
        stock = sdk_alpha_screen('stock')
        assert sdk_alpha_machine() == 0
        sdk_alpha_select(0, 5)            # four stock rows, PAGETEST, empty, callback
        assert sdk_alpha_machine() == 120
        sdk_alpha_screen('empty', empty=True)
        assert sdk_alpha_read('alpha_probe_calls') == 0
        sdk_alpha_tap(20)
        sdk_alpha_screen('empty-src-again', empty=True)
        sdk_alpha_select(5, 6)
        assert sdk_alpha_machine() == 121
        sdk_alpha_screen('callback', empty=True)
        assert sdk_alpha_read('alpha_probe_calls') > 0
        assert sdk_alpha_read('alpha_probe_bad_context') == 0
        sdk_alpha_select(6, 7)
        assert sdk_alpha_machine() == 122
        sdk_alpha_screen('hello-world', empty=True)
        sdk_alpha_select(7, 0)
        assert sdk_alpha_machine() == 0
        calls = sdk_alpha_read('alpha_probe_calls')
        e.step(60_000_000)
        assert sdk_alpha_read('alpha_probe_calls') == calls, 'callback escaped its machine'
        sdk_alpha_screen('stock-return')
        sdk_alpha_select(0, 4)
        assert sdk_alpha_machine() == 30
        legacy = sdk_alpha_screen('legacy')
        e.turn(5, 1)
        e.step(60_000_000)
        assert sdk_alpha_screen('legacy-encoder') != legacy, 'legacy encoder did not update the screen'
        sdk_alpha_select(4, 5)
        sdk_alpha_screen('empty-return', empty=True)
        sdk_alpha_chord(2, 25)           # TRK+TRIG 2, then back to TRK+TRIG 1
        assert sdk_alpha_read('fw_active_track') == 1 and sdk_alpha_machine(1) == 0
        sdk_alpha_tap(20)
        sdk_alpha_screen('stock-other-track')
        sdk_alpha_chord(2, 24)
        assert sdk_alpha_read('fw_active_track') == 0 and sdk_alpha_machine() == 120
        sdk_alpha_tap(20)
        sdk_alpha_screen('empty-track-return', empty=True)
        assert sdk_alpha_read('alpha_probe_calls') == calls
        report['callback_calls'] = calls
        report['bad_context'] = sdk_alpha_read('alpha_probe_bad_context')
        report['status'] = e.status()
        (a.out / 'panel-inputs.json').write_text(json.dumps(e.recorded(), indent=1))
        print('Alpha selection, empty rendering, callback, legacy encoder and tracks passed', flush=True)

        if a.baseline:
            print('Booting baseline firmware', flush=True)
            report['baseline_boot'] = e.load(a.baseline.resolve(), base_sha256=sha)
            e.step(20_000_000)
            sdk_alpha_tap(20)
            assert sdk_alpha_screen('baseline-stock') == stock, 'stock screen changed'
            sdk_alpha_select(0, 4)
            assert sdk_alpha_screen('baseline-legacy') == legacy, 'legacy screen changed'
            report['baseline_screens_equal'] = True
            print('Stock and legacy screens match the main-branch baseline byte for byte', flush=True)
    (a.out / 'report.json').write_text(json.dumps(report, indent=1))
    print('PASS: ' + str(a.out / 'report.json'))


if __name__ == '__main__':
    sdk_alpha_main()
