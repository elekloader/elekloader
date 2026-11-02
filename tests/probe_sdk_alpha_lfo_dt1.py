# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded native DT1 1.54 LFO checks; requires an existing digiemu checkout.

Executes linked firmware routines, not a booted UI or physical device.
"""
import argparse
import json
from pathlib import Path
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader import formats, link  # noqa: E402
from elekloader.sdk import build  # noqa: E402
from elekloader.sdk.sdk_alpha_build import sdk_alpha_build  # noqa: E402


def sdk_alpha_probe_lfo_panel(e, symbols, tap, turn, screen):
    """Alpha LFO is selected; the next LFO press must open LFO 1."""
    def read(name):
        return e.peek32(int(symbols[name], 16))

    def destination(offset):
        return e.peek32(read('fw_kit') + 0x20 + offset) >> 24

    def first_src():
        tap(20)
        if e.peek32(int(symbols['selected'], 16)) >> 24:
            tap(20)
        assert e.peek32(int(symbols['selected'], 16)) >> 24 == 0

    def choose(slot, offset):
        for _ in range(600):
            current = destination(offset)
            if current == slot:
                return
            e.turn(4, 4 if current < slot else -4)
            e.step(10_000_000)
        raise AssertionError(('LFO destination did not reach target', slot, destination(offset)))

    def open_list():
        e.step(660_000_000)  # Let the native transient value display finish before changing pages.
        tap(23)
        tap(43)
        e.step(120_000_000)
        assert read('alpha_lfo_page') == 0xFFFFFFFF, 'LFO naming did not run outside SRC'

    choices = []
    for lfo, offset in enumerate((28, 44), 1):
        open_list()  # D push opens the destination selector.
        for logical, slot in enumerate([*range(17, 25), *range(46, 53)]):
            choose(slot, offset)
            choices.append({'lfo': lfo, 'logical': logical, 'slot': destination(offset)})
            screen('lfo-%d-parameter-%02d' % (lfo, logical))
        choose(17, offset)
        tap(12)
    assert [destination(o) for o in (28, 44)] == [17, 17]
    first_src()
    turn(1, 24)  # A edits RENAME, while D and page 2's H share logical zero.
    screen('lfo-renamed-src-first')
    e.step(660_000_000)
    tap(20)
    assert e.peek32(int(symbols['selected'], 16)) >> 24 == 1
    screen('lfo-renamed-src-second')
    open_list()
    screen('lfo-renamed-list')
    address = int(symbols['sdk_alpha_labels'], 16)
    assert read('fw_active_track') == 0
    assert e.peek32(address) == int.from_bytes(b'QWE\0', 'big')
    assert [destination(o) for o in (28, 44)] == [17, 17]
    tap(12)
    first_src()
    turn(1, 512)
    screen('lfo-hidden-src')
    open_list()
    screen('lfo-hidden-list')
    assert e.peek32(address) == int.from_bytes(b'QWE\0', 'big')
    assert [destination(o) for o in (28, 44)] == [17, 17]
    tap(12)
    first_src()
    turn(1, -512)
    open_list()
    screen('lfo-restored-list')
    assert e.peek32(address) == int.from_bytes(b'XYZ\0', 'big')
    assert [destination(o) for o in (28, 44)] == [17, 17]
    tap(12)
    return {'choices': choices, 'rename_and_hide_preserve_both_destinations': True}


def sdk_alpha_main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--stock', required=True)
    ap.add_argument('--emulator', type=Path, required=True)
    args = ap.parse_args()
    sys.path.insert(0, str(args.emulator.resolve()))
    from emu.harness import Machine, call
    from unicorn.m68k_const import UC_M68K_REG_PC

    stock, dev, release = formats.load(args.stock)
    assert dev.key == 'digitakt-mk1' and release.version == '1.54'
    with tempfile.TemporaryDirectory(prefix='sdk-alpha-lfo-') as tmp:
        mods = [build.build(str(ROOT / path), args.stock, tmp)[1]
                for path in ('mods/core', 'mods/machine-pages')]
        mods += [sdk_alpha_build(str(ROOT / 'tests/fixtures' / path), args.stock, tmp)[1]
                 for path in ('sdk-alpha-lfo-machine', 'sdk-alpha-empty-machine')]
        linked = link.link(mods, formats.main_image(stock, dev))
    m = Machine()
    m.load(linked.image, dev.main_load)
    m.install_isa_patches_scoped(linked.image, dev.main_load)
    lo, hi = linked.layout['ddr']
    offset = linked.layout['run_load'] - dev.main_load
    runtime = linked.image[offset:offset + hi - lo]
    m.load(runtime, lo)
    m.install_isa_patches_scoped(runtime, lo)
    lo, hi = linked.layout['bss']
    m.load(bytes(hi - lo), lo)

    def native(address, values):
        result = call(m, address, values, ret_magic=0xDEADBEE0, limit=500_000)
        assert m.uc.reg_read(UC_M68K_REG_PC) == 0xDEADBEE0, hex(address)
        return result

    def put(address, value):
        m.ensure(address)
        m.uc.mem_write(address, struct.pack('>I', value))

    def label(parameter, what):
        address = native(linked.map['mp_lfo'], [parameter, what, 0])
        return bytes(m.uc.mem_read(address, 32)).split(b'\0')[0].decode('ascii')

    kit, sound = 0x43000100, 0x43000120
    m.ensure(kit)
    put(linked.map['fw_kit'], kit)
    put(linked.map['fw_active_track'], 0)
    native(0x40078B20, [])  # native parameter descriptors, including the seven extras
    native(0x40084EF6, [sound, 0, 126])
    slots = [*range(17, 25), *range(46, 53)]
    parameters = [*range(132, 140), *range(164, 171)]
    names = ['XYZ', 'RENAME'] + ['P%02d' % i for i in range(3, 16)]
    before = bytes(m.uc.mem_read(sound, 162))
    for slot, parameter, name in zip(slots, parameters, names):
        assert native(0x40078F44, [slot, 126]) == parameter
        assert native(0x40078D80, [parameter]) == slot
        assert label(parameter, 0) == 'Alpha LFO'
        assert label(parameter, 1) == label(parameter, 2) == name
    assert bytes(m.uc.mem_read(sound, 162)) == before
    for machine in (0, 120):
        m.uc.mem_write(sound + 126, bytes([machine]))
        assert native(linked.map['mp_lfo'], [132, 1, 123]) == 123
    m.uc.mem_write(sound + 126, bytes([126]))
    for parameter in (51, 55, 64, 171, 0xFFFFFFFF):
        assert native(linked.map['mp_lfo'], [parameter, 1, 123]) == 123

    # A rename, hide and cross-track lookup must not write the stored sound or destinations.
    for rename in (0, 256, 32512, 0):
        m.uc.mem_write(sound + 20 + 2 * 18, struct.pack('>h', rename))
        for slot in (4, 12):
            m.uc.mem_write(sound + 20 + 2 * slot, struct.pack('>h', 17 << 8))
        before = bytes(m.uc.mem_read(sound, 162))
        assert label(132, 1) == label(132, 2) == ('QWE' if rename else 'XYZ')
        assert bytes(m.uc.mem_read(sound, 162)) == before
        other = sound + 7 * 0xa2
        native(0x40084EF6, [other, 0, 126])
        put(linked.map['fw_active_track'], 7)
        assert label(132, 1) == 'XYZ'
        put(linked.map['fw_active_track'], 0)
        assert label(132, 2) == ('QWE' if rename else 'XYZ')

    # Both real native LFO engines must modulate each logical parameter, before/after rename.
    results = []
    m.ensure(0x421F4E10)
    for lfo in (0, 1):
        for logical, slot in enumerate(slots):
            samples = []
            for rename in (0, 256):
                native(0x40084EF6, [sound, 0, 126])
                values = list(struct.unpack('>53h', m.uc.mem_read(sound + 20, 106)))
                for s in slots:
                    values[s] = 8000
                # Avoid changing the tested target when the rename controller is itself targeted.
                values[18] = rename if slot != 18 else 8000
                base = 8 * lfo
                values[1 + base] = 0x5000
                values[4 + base] = slot << 8
                values[5 + base] = 2 << 8
                values[6 + base] = 0x4000
                values[7 + base] = 1 << 8
                values[8 + base] = 0x4800
                m.uc.mem_write(sound + 20, struct.pack('>53h', *values))
                assert label(132, 1) == ('QWE' if values[18] else 'XYZ')
                m.uc.mem_write(0x421F4E10, bytes(640))
                native(0x40077282, [sound, 0])
                put(0x800019B4, 0)
                m.uc.emu_start(0x40074732, 0x40074738, count=1)
                output = native(0x40074B9E, [0x800014F0])
                m.uc.emu_start(0x4006A868, 0x4006A86E, count=1)
                native(0x400ED766, [output, 14400, 255])
                actual = struct.unpack('>53h', m.uc.mem_read(output + 18, 106))
                assert actual[slot] != 8000, (lfo, logical, actual[slot])
                for other in slots:
                    if other != slot:
                        assert actual[other] == values[other], (lfo, logical, other)
                assert bytes(m.uc.mem_read(sound + 20, 106)) == struct.pack('>53h', *values)
                assert native(linked.map['cm_src_value'], [0, logical]) == actual[slot]
                samples.append(actual[slot])
            assert samples[0] == samples[1], (lfo, logical, samples)
            results.append({'lfo': lfo + 1, 'logical': logical, 'slot': slot, 'value': samples[0]})
    print(json.dumps({'all_names_and_ids': True, 'rename_preserves_destinations': True,
                      'track_isolation': True, 'modulation': results}, indent=1))


if __name__ == '__main__':
    sdk_alpha_main()
