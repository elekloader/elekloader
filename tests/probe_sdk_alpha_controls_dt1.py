# SPDX-License-Identifier: GPL-2.0-or-later
"""PR-2 normal-panel acceptance. Build SA03 with all alpha fixtures and PAGETEST.

Uses the same local DigiKit daemon and arguments as probe_sdk_alpha_dt1.py.
No UI-memory writes or direct UI calls. Firmware/map and screenshots stay ignored.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys


def sdk_alpha_main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ('digikit', 'daemon', 'stock', 'firmware', 'out'):
        ap.add_argument('--' + name, type=Path, required=True)
    ap.add_argument('--baseline', type=Path)
    a = ap.parse_args()
    a.out = a.out.resolve()
    a.out.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(a.digikit.resolve()))
    from tools.control.digi_client import Emu
    symbols = json.loads(Path(str(a.firmware) + '.map.json').read_text())
    sha = hashlib.sha256(a.stock.read_bytes()).hexdigest()
    assert sha == 'f78ba80fa7b1da5fb0e1ff61ad61e9e71aafe79f4364fc49679f3651353e3cf6'
    report = {'stock_sha256': sha, 'firmware_sha256': hashlib.sha256(a.firmware.read_bytes()).hexdigest(),
              'daemon_sha256': hashlib.sha256(a.daemon.read_bytes()).hexdigest(), 'events': []}
    rows = {0: 0, 1: 1, 2: 2, 3: 3, 30: 4, 120: 5, 121: 6, 122: 7, 123: 8, 124: 9}
    with Emu.spawn(str(a.stock.resolve()), binary=str(a.daemon.resolve()), run_dir=a.out / 'run') as e:
        def sdk_alpha_read(name, offset=0):
            return e.peek32(int(symbols[name], 16) + offset)

        def sdk_alpha_tap(key):
            e.tap(key, hold=10_000_000)
            e.step(30_000_000)

        def sdk_alpha_chord(modifier, key):
            e.button(modifier, True)
            e.step(15_000_000)
            sdk_alpha_tap(key)
            e.button(modifier, False)
            e.step(20_000_000)

        def sdk_alpha_machine(track=None):
            if track is None:
                track = sdk_alpha_read('fw_active_track')
            return (e.peek32(sdk_alpha_read('fw_kit') + 0x20 + 0xa2 * track + 0x7c) >> 8) & 255

        def sdk_alpha_select(wanted):
            current = sdk_alpha_machine()
            sdk_alpha_chord(1, 20)
            delta = rows[wanted] - rows[current]
            for _ in range(abs(delta)):
                sdk_alpha_tap(15 if delta > 0 else 14)
            sdk_alpha_tap(12)
            assert sdk_alpha_machine() == wanted

        def sdk_alpha_screen(name):
            (a.out / (name + '.png')).write_bytes(e.frame_png())
            assert e.status()['cf']['error'] is None
            return e.frame()[1]

        def sdk_alpha_values(label, track=0):
            raw = [sdk_alpha_read('alpha_controls_raw', 4 * (9 * track + k)) for k in range(9)]
            mapped = [sdk_alpha_read('alpha_controls_value', 4 * (9 * track + k)) for k in range(9)]
            mapped = [v if v < 0x80000000 else v - 0x100000000 for v in mapped]
            result = {'label': label, 'track': track, 'page': sdk_alpha_read('alpha_controls_page', 4 * track),
                      'raw': raw, 'mapped': mapped}
            assert sdk_alpha_read('alpha_controls_bad_context') == 0
            assert all(0 <= v <= 32512 for v in raw)
            assert mapped[0] == 10 + (10 * raw[0] + 16256) // 32512
            assert mapped[1] == 0 and mapped[2] == (raw[2] + 128) // 256 and mapped[3] == 7
            assert mapped[8] == -10 + (20 * raw[8] + 16256) // 32512
            report['events'].append(result)
            print(result, flush=True)
            return result

        def sdk_alpha_turn(encoder, delta):
            for _ in range(abs(delta)):
                e.turn(encoder, 4 if delta > 0 else -4)
                e.step(1_000_000)
            e.step(60_000_000)

        print('Booting PR-2 firmware', flush=True)
        report['boot'] = e.load(a.firmware.resolve(), base_sha256=sha)
        e.step(20_000_000)
        sdk_alpha_tap(20)
        stock = sdk_alpha_screen('stock')
        sdk_alpha_select(30)
        legacy = sdk_alpha_screen('legacy-fresh')
        sdk_alpha_select(0)
        sdk_alpha_select(123)
        initial = sdk_alpha_values('controls-initial')
        assert initial['page'] == 0
        assert initial['raw'] == [0] * 9
        sdk_alpha_screen('controls-page-0')
        sdk_alpha_turn(1, 20)  # A is logical AMOUNT (2), not PITCH (0).
        amount = sdk_alpha_values('amount-turned')
        assert amount['raw'][2] > 0 and amount['raw'][0] == 0
        sdk_alpha_turn(4, 30)  # D is PITCH; it stays a value editor, never a sample list.
        pitch = sdk_alpha_values('pitch-turned')
        assert pitch['raw'][0] > 0
        sdk_alpha_screen('controls-page-0-turned')
        sdk_alpha_turn(4, 128)
        maximum = sdk_alpha_values('pitch-maximum')
        assert maximum['raw'][0] == 32512 and maximum['mapped'][0] == 20
        sdk_alpha_screen('pitch-maximum')
        sdk_alpha_turn(4, -128)
        minimum = sdk_alpha_values('pitch-minimum')
        assert minimum['raw'][0] == 0 and minimum['mapped'][0] == 10
        sdk_alpha_turn(4, 30)
        sdk_alpha_turn(8, 10)
        pitch = sdk_alpha_values('zero-bound-raw-preserved')
        assert pitch['raw'][1] > 0 and pitch['mapped'][1] == 0
        sdk_alpha_turn(7, 10)  # G is blank.
        assert sdk_alpha_values('blank-unchanged')['raw'] == pitch['raw']
        sdk_alpha_tap(20)
        other_page = sdk_alpha_values('page-1-cross-page')
        assert other_page['page'] == 1 and other_page['raw'] == pitch['raw']
        sdk_alpha_turn(1, 30)  # A is logical OTHER (8), using an extra native slot.
        other = sdk_alpha_values('other-turned')
        assert other['raw'][8] > 0 and other['raw'][2] == amount['raw'][2]
        sdk_alpha_turn(2, 32)   # B is the same PITCH as D on page 0.
        shared = sdk_alpha_values('shared-pitch-turned')
        assert shared['raw'][0] > other['raw'][0]
        sdk_alpha_turn(8, 10)
        shared = sdk_alpha_values('equal-bounds-raw-preserved')
        assert shared['raw'][3] > 0 and shared['mapped'][3] == 7
        sdk_alpha_screen('controls-page-1-turned')
        sdk_alpha_tap(20)
        cross = sdk_alpha_values('page-0-cross-page')
        assert cross['page'] == 0 and cross['raw'] == shared['raw']
        sdk_alpha_chord(2, 25)
        assert sdk_alpha_read('fw_active_track') == 1
        sdk_alpha_select(123)
        second = sdk_alpha_values('second-instance', 1)
        assert second['raw'] == [0] * 9
        sdk_alpha_turn(1, 48)
        changed_second = sdk_alpha_values('second-instance-turned', 1)
        assert changed_second['raw'][2] > 0 and changed_second['raw'][2] != cross['raw'][2]
        sdk_alpha_tap(20)  # page selection also belongs to each track
        sdk_alpha_chord(2, 24)
        first_again = sdk_alpha_values('first-instance-return')
        assert first_again['page'] == 0 and first_again['raw'] == cross['raw']
        sdk_alpha_screen('first-instance-return')
        sdk_alpha_select(0)
        calls = sdk_alpha_read('alpha_controls_calls')
        sdk_alpha_tap(20)
        assert sdk_alpha_read('alpha_controls_calls') == calls
        sdk_alpha_select(124)
        one = sdk_alpha_screen('one-page')
        sdk_alpha_turn(4, 32)
        assert sdk_alpha_screen('one-page-turned') != one
        sdk_alpha_tap(20)
        assert sdk_alpha_machine() == 124
        sdk_alpha_select(122)
        sdk_alpha_screen('hello-page-0')
        sdk_alpha_turn(1, 32)
        sdk_alpha_tap(20)
        sdk_alpha_screen('hello-page-1')
        sdk_alpha_select(120)
        sdk_alpha_tap(20)
        sdk_alpha_screen('empty')
        assert all(sdk_alpha_read('sdk_alpha_empty_layout', 4 * (2 + k)) == 0 for k in range(8))
        sdk_alpha_select(121)
        sdk_alpha_tap(20)
        assert sdk_alpha_read('alpha_probe_calls') > 0 and sdk_alpha_read('alpha_probe_bad_context') == 0
        sdk_alpha_select(30)
        legacy_return = sdk_alpha_screen('legacy')
        sdk_alpha_turn(5, 32)
        assert sdk_alpha_screen('legacy-turned') != legacy_return
        sdk_alpha_select(0)
        sdk_alpha_screen('stock-return')
        report['controls_calls'] = sdk_alpha_read('alpha_controls_calls')
        report['status'] = e.status()
        (a.out / 'panel-inputs.json').write_text(json.dumps(e.recorded(), indent=1))
        if a.baseline:
            e.load(a.baseline.resolve(), base_sha256=sha)
            e.step(20_000_000)
            sdk_alpha_tap(20)
            assert sdk_alpha_screen('baseline-stock') == stock
            sdk_alpha_select(30)
            assert sdk_alpha_screen('baseline-legacy') == legacy
            report['baseline_screens_equal'] = True
    report['result'] = 'PASS'
    (a.out / 'report.json').write_text(json.dumps(report, indent=1))
    print('PASS: ' + str(a.out / 'report.json'))


if __name__ == '__main__':
    sdk_alpha_main()
