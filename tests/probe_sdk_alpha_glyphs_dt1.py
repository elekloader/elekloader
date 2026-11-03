# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded DT1 1.54 glyph checks, using an existing digiemu checkout.

Runs the linked SDK and renderer, intercepting only the native rectangle primitive.
This does not boot the UI or qualify physical hardware.
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


def sdk_alpha_probe_glyph_panel(e, symbols, select, chord, screen):
    """SDK Glyphs is selected through the normal panel; memory access is read-only."""
    def read(name):
        return e.peek32(int(symbols[name], 16))

    def value():
        sound = read('fw_kit') + 0x20 + 0xa2 * read('fw_active_track')
        raw = e.peek32(sound + 52) & 0xffff
        return 1 + (3 * raw + 16256) // 32512

    def crop(x0, y0, width, height):
        frame = e.frame()[1]
        return [sum(((frame[7 - y // 8 + 8 * x] >> (y % 8)) & 1) << (width - 1 - x + x0)
                    for x in range(x0, x0 + width)) for y in range(y0, y0 + height)]

    first = read('sdk_alpha_glyphs')

    def check():
        pointer = read('sdk_alpha_glyphs')
        assert pointer == first + 32 * (value() - 1), (value(), hex(pointer))
        words = [e.peek32(pointer + 4 * i) for i in range(8)]
        rows = [half for word in words for half in (word >> 16, word & 0xffff)]
        assert crop(25, 13, 16, 16) == rows, 'rendered pixels differ from the selected glyph'
        assert e.status()['cf']['error'] is None
        return value()

    def choose(wanted):
        for _ in range(600):
            if value() == wanted:
                e.step(60_000_000)
                assert check() == wanted
                screen('glyph-%d' % wanted)
                return wanted
            e.turn(1, 4 if value() < wanted else -4)
            e.step(10_000_000)
        raise AssertionError(('state not reached', wanted, value()))

    assert value() == check() == 1
    sequence = [choose(v) for v in (1, 2, 3, 4, 3, 2)]
    digit = crop(25, 13, 16, 16)
    for enabled in (True, False):
        for _ in range(600):
            sound = read('fw_kit') + 0x20 + 0xa2 * read('fw_active_track')
            if ((e.peek32(sound + 56) >> 16) >= 16256) == enabled:
                break
            e.turn(2, 4 if enabled else -4)
            e.step(10_000_000)
        else:
            raise AssertionError('STOCK selector did not reach its value')
        e.step(60_000_000)
        assert value() == 2, 'changing the glyph changed its parameter value'
        if enabled:
            assert read('sdk_alpha_glyphs') == 0
            assert crop(25, 13, 16, 16) != digit, 'stock graphic did not replace the glyph'
            screen('glyph-stock-override')
        else:
            assert check() == 2
            screen('glyph-restored-without-value-change')
    e.step(660_000_000)
    chord(2, 25)
    select(127)
    assert value() == check() == 1
    screen('glyph-other-track')
    chord(2, 24)
    assert value() == check() == 2
    screen('glyph-first-track-return')

    # Changing glyphs must not reset the native numeric display while an encoder turns.
    e.step(660_000_000)
    label = crop(18, 30, 33, 9)
    seen = set()
    for i in range(64):
        e.turn(1, 4)
        e.step(4_500_000)
        seen.add(value())
        if i >= 6:
            assert crop(18, 30, 33, 9) != label, 'value display flickered during glyph changes'
        if value() == 4:
            break
    assert len(seen) > 1, 'encoder did not change the glyph during the timing check'
    e.step(132_000_000)
    assert crop(18, 30, 33, 9) != label, 'numeric display ended early'
    e.step(528_000_000)
    assert crop(18, 30, 33, 9) == label, 'label did not return after the native timeout'
    check()
    screen('glyph-after-native-timeout')
    return {'sequence': sequence, 'exact_pixels': True, 'track_isolation': True,
            'native_numeric_display_timing': True, 'stock_override_without_value_change': True}


def sdk_alpha_main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--stock', required=True)
    ap.add_argument('--emulator', type=Path, required=True)
    args = ap.parse_args()
    sys.path.insert(0, str(args.emulator.resolve()))
    from emu.harness import Machine, call
    from unicorn import UC_HOOK_CODE
    from unicorn.m68k_const import UC_M68K_REG_A7, UC_M68K_REG_PC

    stock, dev, release = formats.load(args.stock)
    assert dev.key == 'digitakt-mk1' and release.version == '1.54'
    with tempfile.TemporaryDirectory(prefix='sdk-alpha-glyphs-') as tmp:
        mods = [build.build(str(ROOT / path), args.stock, tmp)[1]
                for path in ('mods/core', 'mods/machine-pages')]
        mods.append(sdk_alpha_build(str(ROOT / 'examples/sdk-glyphs'), args.stock, tmp)[1])
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
        result = call(m, address, values, limit=500_000)
        assert m.uc.reg_read(UC_M68K_REG_PC) == 0xDEADBEE0, hex(address)
        return result

    def read(address):
        return struct.unpack('>I', m.uc.mem_read(address, 4))[0]

    def put(address, value):
        m.ensure(address)
        m.uc.mem_write(address, struct.pack('>I', value))

    # Capture the actual renderer's calls without requiring a constructed native Bitmap.
    rectangles = []

    def rectangle(uc, address, size, data):
        sp = uc.reg_read(UC_M68K_REG_A7)
        ret, bmp, x0, y0, x1, y1, colour = struct.unpack('>7I', uc.mem_read(sp, 28))
        assert bmp == 0x43003000
        rectangles.append((x0, y0, x1, y1, colour))
        uc.reg_write(UC_M68K_REG_A7, sp + 4)
        uc.reg_write(UC_M68K_REG_PC, ret)

    fill = linked.map['fw_fillrect']
    m.uc.hook_add(UC_HOOK_CODE, rectangle, begin=fill, end=fill)
    kit, sound = 0x43000100, 0x43000120
    m.ensure(kit)
    put(linked.map['fw_kit'], kit)
    put(linked.map['fw_active_track'], 0)
    native(0x40078B20, [])  # Native parameter descriptors.
    native(0x40084EF6, [sound, 0, 127])
    machine = native(linked.map['sdk_alpha_machine'], [127])
    assert machine
    page = read(machine + 12)
    state = read(page + 4)
    first_glyph = read(state + 24)
    present = read(machine + 28)
    native(linked.map['mp_layout_track'], [127, 0])

    def snapshot():
        return native(linked.map['sdk_alpha_snapshot'], [machine, 0xFFFFFFFF, 0])

    def glyph(k=0):
        return native(linked.map['sdk_alpha_glyph'], [machine, k])

    def draw(k=0):
        rectangles.clear()
        m.uc.mem_write(0x43002000, struct.pack('>7I', 132 + k, 0, 0, 0, 0x43003000, 17, 23))
        return native(linked.map['mp_knob'], [0x43002000])

    put(machine + 28, 0)
    snapshot()
    assert glyph() == first_glyph and glyph(1) == 0  # Declared and omitted defaults.
    put(machine + 28, present)
    original = bytes(m.uc.mem_read(sound, 162))
    sequence = []
    for raw, value in ((0, 1), (42, 2), (85, 3), (127, 4), (85, 3), (42, 2), (0, 1)):
        m.uc.mem_write(sound + 54, struct.pack('>h', raw << 8))
        before = bytes(m.uc.mem_read(sound, 162))
        snapshot()
        assert glyph() == first_glyph + 32 * (value - 1)
        assert not snapshot(), 'unchanged glyph requested another redraw'
        assert draw() == 1
        assert rectangles[0] == (18, 23, 34, 39, 0), rectangles[0]
        rows = struct.unpack('>16H', m.uc.mem_read(glyph(), 32))
        pixels = {(18 + col, 39 - row) for row in range(16) for col in range(16)
                  if rows[row] & (0x8000 >> col)}
        assert set(rectangles[1:]) == {(x, y, x, y, 1) for x, y in pixels}
        assert bytes(m.uc.mem_read(sound, 162)) == before
        sequence.append(value)
    assert draw(1) == 0 and not rectangles, 'omitted glyph did not use the native graphic'
    m.uc.mem_write(sound + 56, struct.pack('>h', 127 << 8))
    snapshot()
    assert glyph() == 0 and draw() == 0 and not rectangles  # Callback explicitly selects stock.
    m.uc.mem_write(sound + 56, bytes(2))
    snapshot()
    assert glyph() == first_glyph and draw() == 1

    # Default reset and a legacy descriptor must not reuse an earlier dynamic glyph.
    put(machine + 28, 0)
    put(state + 24, 0)
    snapshot()
    assert glyph() == 0 and draw() == 0 and not rectangles
    put(state + 24, 1)  # Invalid pointer would fault if an old descriptor's tail were read.
    adapter = native(linked.map['core_machine_ui'], [127])
    put(adapter, linked.map['sdk_alpha_dt1_ui_v3'])
    snapshot()
    assert glyph() == 0 and draw() == 0 and not rectangles
    put(adapter, linked.map['sdk_alpha_dt1_ui_v4'])
    put(state + 24, first_glyph)
    snapshot()
    assert glyph() == first_glyph and draw() == 1
    assert bytes(m.uc.mem_read(sound, 162)) == original
    print(json.dumps({'glyph_sequence': sequence, 'static_default': True, 'stock_fallback': True,
                      'legacy_descriptor': True, 'draw_pixels': True, 'stored_values_preserved': True}, indent=1))


if __name__ == '__main__':
    sdk_alpha_main()
