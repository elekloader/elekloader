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
    ap.add_argument('--labels', action='store_true', help='also exercise PR-3 labels and visibility')
    ap.add_argument('--lfo', action='store_true', help='also exercise all 15 SDK LFO targets')
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
    rows = {0: 0, 1: 1, 2: 2, 3: 3, 30: 4, 120: 5, 121: 6, 122: 7, 123: 8, 124: 9, 125: 10, 126: 11}
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

        print('Booting SDK controls firmware', flush=True)
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
        if a.labels:
            sdk_alpha_select(125)
            def sdk_alpha_bytes(addr, count):
                start = addr & ~3
                data = b''.join(e.peek32(p).to_bytes(4, 'big') for p in range(start, addr + count, 4))
                return data[addr - start:addr - start + count]
            def sdk_alpha_labels(label):
                assert sdk_alpha_read('alpha_labels_bad_context') == 0
                page = sdk_alpha_read('alpha_labels_page')
                amount = sdk_alpha_read('alpha_labels_amount')
                # The SDK owns a bounded copy; no callback buffer pointer is retained.
                addr = int(symbols['sdk_alpha_labels'], 16)
                text = sdk_alpha_bytes(addr, 9).split(b'\0')[0].decode('ascii')
                visible = sdk_alpha_bytes(int(symbols['sdk_alpha_visibility'], 16), 2)
                result = {'label': label, 'page': page, 'amount': amount, 'text': text,
                          'visible': list(visible)}
                report['events'].append(result)
                print(result, flush=True)
                sdk_alpha_screen(label)
                return result
            default = sdk_alpha_labels('labels-default-page-0')
            assert default['text'] == 'PITCH' and default['visible'] == [1, 1]
            sdk_alpha_tap(20)
            # A changing H's label must not clear A's native temporary value display.
            def sdk_alpha_source_text():
                f = e.frame()[1]
                return bytes((f[7 - y // 8 + 8 * x] >> (y % 8)) & 1
                             for y in range(30, 39) for x in range(18, 51))
            e.step(600_000_000)
            source_label = sdk_alpha_source_text()
            active_labels = []
            for i in range(24):
                e.turn(1, 4)
                e.step(4_500_000)
                pixels = sdk_alpha_source_text()
                active_labels.append(pixels == source_label)
                if i >= 6: # allow the initial input/render latency
                    assert any(pixels) and pixels != source_label, 'source label flickered during turns'
            e.step(132_000_000)
            assert sdk_alpha_source_text() != source_label, 'source value disappeared before timeout'
            sdk_alpha_screen('labels-source-value-after-one-second')
            e.step(528_000_000)
            assert sdk_alpha_source_text() == source_label, 'source label did not return after timeout'
            sdk_alpha_screen('labels-source-label-after-five-seconds')
            report['source_value_display'] = {'active_label_frames': active_labels,
                                               'one_second_value': True, 'five_second_label': True}
            sdk_alpha_turn(1, 24) # page 1's A is AMOUNT; H is the dependent PITCH.
            changed = sdk_alpha_labels('labels-live-page-1')
            assert 0 < changed['amount'] < 127 and changed['text'] == f"AMT {changed['amount']:03}"
            sdk_alpha_tap(20)
            cross = sdk_alpha_labels('labels-cross-page-0')
            assert cross['page'] == 0 and cross['text'] == changed['text']
            sdk_alpha_tap(20)
            sdk_alpha_turn(1, 512)
            hidden = sdk_alpha_labels('labels-hidden-page-1')
            assert hidden['amount'] == 127 and hidden['visible'] == [0, 1]
            pitch_address = int(symbols['fw_kit'], 16)
            def pitch_raw():
                kit = e.peek32(pitch_address)
                return sdk_alpha_bytes(kit + 0x20 + 0x36, 2)
            before = pitch_raw()
            sdk_alpha_turn(8, 32)
            assert pitch_raw() == before, 'hidden H still edited PITCH'
            sdk_alpha_tap(20)
            sdk_alpha_labels('labels-hidden-page-0')
            sdk_alpha_turn(1, 32)
            assert pitch_raw() == before, 'hidden A still edited PITCH'
            sdk_alpha_tap(20)
            sdk_alpha_turn(1, -512)
            restored = sdk_alpha_labels('labels-restored-page-1')
            assert restored['amount'] == 0 and restored['text'] == 'PITCH' and restored['visible'] == [1, 1]
            sdk_alpha_turn(8, 24)
            assert pitch_raw() != before, 'shown H did not edit PITCH'
            sdk_alpha_turn(1, 24)
            first = sdk_alpha_labels('labels-first-track')
            sdk_alpha_chord(2, 25)
            sdk_alpha_select(125)
            # Track 2 starts with a separate sound and static presentation.
            assert sdk_alpha_read('alpha_labels_amount', 4) == 0
            sdk_alpha_screen('labels-second-track')
            sdk_alpha_chord(2, 24)
            returned = sdk_alpha_labels('labels-first-track-return')
            assert returned['text'] == first['text'] and returned['amount'] == first['amount']
        if a.lfo:
            from probe_sdk_alpha_lfo_dt1 import sdk_alpha_probe_lfo_panel
            sdk_alpha_select(126)
            report['lfo'] = sdk_alpha_probe_lfo_panel(
                e, symbols, sdk_alpha_tap, sdk_alpha_turn, sdk_alpha_screen)
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
