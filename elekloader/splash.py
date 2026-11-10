# SPDX-License-Identifier: GPL-2.0-or-later
"""Boot splash mods without a toolchain: your frames -> a format-2 .elemod.

The shop's Splash screen maker (web/splash.js) turns a PNG or a GIF into
128x64 one-bit frames and a sequence, and hands them to make() through the
page's engine (web/bridge.py). The mod is examples/splash/splash.s, assembled:
its code has no relocations and only five 32-bit fields differ from one OS to
another, so its bytes are kept here and the fields filled in
(tests/test_units.py assembles the source and compares, when the cross
assembler is there).

    doc = make([frame0, frame1], [0, 0, 1, 1], device='digitakt-mk1', os='1.54')
    raw = dumps(doc)

A frame is 1024 bytes in the panel's format (pack() makes one from rows of
pixels). The sequence names a frame for each tick of the intro's PIT3 timer,
15 a second (the intro presents several frames a tick, each showing the
tick's); looping, it starts again after its last entry, else it stays on that
entry. The intro runs about 50 ticks (3.3 s): in digiemu, from a snapshot taken
before it, 49 ticks and 180 presents on each of the four OSes.

The present sites and the PIT3 gate are DigiSplash's
(github.com/DigiAlchemydsp/DigiSplash, GPL-2.0-or-later), measured for 1.53 and
1.43 there and found again in 1.54 and 1.44 by their bytes; the tick site is
the handler's call to the semaphore post, the same bytes in all four.
"""
import hashlib
import json
import re

from . import devices

W, H = 128, 64
FRAME = W * H // 8                      # 1024 bytes
TICKS_PER_SECOND = 15                   # the intro's PIT3: 66.67 ms
INTRO_TICKS = 50                        # about how long the intro runs: 49-51 ticks in digiemu, all four OSes
MAX_FRAMES = 96                         # 96 KB of frames, beside the other mods' RAM
MAX_SEQUENCE = 4096                     # entries: over four minutes, far past the intro
PIT3_VECTOR = 0x40000340

# examples/splash/splash.s assembled (m68k-linux-gnu-as -mcpu=54455), with its five
# fields zero: DIFF at 6, FLUSH at 16, POST at 52, ISR at 64, FB at 72
CODE = bytes.fromhex(
    '61000036' '4ef900000000' '6100002c' '4ef900000000'                     # splash_diff, splash_flush
    '43fa0062' '20290004' '5280' 'b091' '650c' '4280' '4aa9000c' '6604' '2011' '5380'
    '23400004' '4ef900000000'                                               # splash_tick
    '20394000' '0340' '0c8000000000' '662e' '207900000000' '4a88' '6724'    # splash_draw
    '43fa0026' '20290004' '7200' '32310a10' '700a' 'e1a9' 'd2a90008' 'd3c1' '203c00000100'
    '20d9' '5380' '66fa' '4e75' '0000')
FIELDS = {'diff': 6, 'flush': 16, 'post': 52, 'isr': 64, 'fb': 72}
SYMBOLS = {'splash_diff': 0, 'splash_flush': 10, 'splash_tick': 20, 'splash_draw': 56,
           'splash_data': len(CODE)}
TICK_SITE = 0x1c                        # in the PIT3 handler: its jsr to the post

# Per device and OS: the presents, the intro's calls to them, its PIT3 handler (whose
# call at TICK_SITE is to `post`) and the front buffer pointer.
TABLES = {
    ('digitakt-mk1', '1.53'): dict(
        diff=0x400e60e2, flush=0x400e6064, isr=0x4006c154, fb=0x4020d8f8, post=0x40001770,
        diff_sites=(0x4006c2d0, 0x4006c3ac, 0x4006c864, 0x4006cb68),
        flush_sites=(0x4006c3a4, 0x4006c85c, 0x4006cb60)),
    ('digitakt-mk1', '1.54'): dict(
        diff=0x400e630a, flush=0x400e628c, isr=0x4006c154, fb=0x4020dca8, post=0x40001770,
        diff_sites=(0x4006c2d0, 0x4006c3ac, 0x4006c864, 0x4006cb68),
        flush_sites=(0x4006c3a4, 0x4006c85c, 0x4006cb60)),
    ('digitone-mk1', '1.43'): dict(
        diff=0x400f8efa, flush=0x400f8e7c, isr=0x40090cdc, fb=0x40241a44, post=0x400018d0,
        diff_sites=(0x40090e58, 0x40091000, 0x4009130a, 0x400917bc, 0x40091ac0),
        flush_sites=(0x40090ff8, 0x40091302, 0x400917b4, 0x40091ab8)),
    ('digitone-mk1', '1.44'): dict(
        diff=0x400f916e, flush=0x400f90f0, isr=0x40090cfc, fb=0x40241e44, post=0x400018d0,
        diff_sites=(0x40090e78, 0x40091020, 0x4009132a, 0x400917dc, 0x40091ae0),
        flush_sites=(0x40091018, 0x40091322, 0x400917d4, 0x40091ad8)),
}


class SplashError(ValueError):
    pass


def targets():
    """-> [{device, name, os}] the splash can be made for."""
    out = []
    for d in devices.devices():
        for v in sorted(d.releases):
            if (d.key, v) in TABLES:
                out.append({'device': d.key, 'name': d.name, 'os': v})
    return out


def code(device, os):
    """-> the code for this OS: CODE with its fields."""
    t = _table(device, os)
    b = bytearray(CODE)
    for k, at in FIELDS.items():
        b[at:at + 4] = t[k].to_bytes(4, 'big')
    return bytes(b)


def _table(device, os):
    try:
        return TABLES[(device, str(os))]
    except KeyError:
        raise SplashError('no splash for the %s OS %s; it is made for %s' % (
            device, os, ', '.join('%s %s' % k for k in sorted(TABLES))))


def pack(rows):
    """Rows of pixels (64 rows of 128 values, truthy = lit) -> one frame."""
    if len(rows) != H or any(len(r) != W for r in rows):
        raise SplashError('a frame is %d rows of %d pixels' % (H, W))
    buf = bytearray(FRAME)
    for y, row in enumerate(rows):
        page, bit = 7 - (y >> 3), 1 << (y & 7)
        for x, v in enumerate(row):
            if v:
                buf[page + 8 * x] |= bit
    return bytes(buf)


def unpack(frame):
    """One frame -> 64 rows of 128 ints (1 = lit)."""
    return [[(frame[7 - (y >> 3) + 8 * x] >> (y & 7)) & 1 for x in range(W)] for y in range(H)]


def slug(s):
    return re.sub(r'[^a-z0-9]+', '-', str(s).lower()).strip('-')[:40] or 'splash'


def data(frames, sequence=None, loop=True):
    """-> the bytes after the code: header, sequence, frames."""
    frames = [bytes(f) for f in frames]
    if not frames:
        raise SplashError('no frames')
    if len(frames) > MAX_FRAMES:
        raise SplashError('%d frames; at most %d fit' % (len(frames), MAX_FRAMES))
    if any(len(f) != FRAME for f in frames):
        raise SplashError('a frame is %d bytes' % FRAME)
    seq = list(range(len(frames))) if sequence is None else list(sequence)
    if not seq or len(seq) > MAX_SEQUENCE:
        raise SplashError('the sequence has 1 to %d entries' % MAX_SEQUENCE)
    if any(not isinstance(n, int) or isinstance(n, bool) or not 0 <= n < len(frames) for n in seq):
        raise SplashError('the sequence names frames 0 to %d' % (len(frames) - 1))
    head = 16 + 2 * len(seq)
    start = (head + 3) & ~3
    out = bytearray()
    for v in (len(seq), 0, start, 1 if loop else 0):
        out += v.to_bytes(4, 'big')
    for n in seq:
        out += n.to_bytes(2, 'big')
    out += bytes(start - head)
    for f in frames:
        out += f
    return bytes(out)


def _site(addr, target, call):
    stock = b'\x4e\xb9' + call.to_bytes(4, 'big')
    return {'addr': '0x%08x' % addr, 'len': 6, 'stock_sha256': hashlib.sha256(stock).hexdigest(),
            'new': '4eb900000000', 'kind': 'code', 'relocs': [[2, 'abs32', 'sym:' + target, 0]]}


def make(frames, sequence=None, *, device, os, title='My splash', loop=True, author='',
         source=''):
    """-> the .elemod document (format 2) showing `frames` over the intro."""
    t = _table(device, os)
    dev = next(d for d in devices.devices() if d.key == device)
    rel = dev.releases[str(os)]
    body = code(device, os) + data(frames, sequence, loop)
    n = len(frames)
    seq_n = n if sequence is None else len(sequence)
    title = ' '.join(str(title).split())[:40] or 'My splash'
    secs = round(seq_n / TICKS_PER_SECOND, 1)
    what = 'a picture' if n == 1 else '%d frames, %s s%s' % (n, secs, ', looping' if loop else '')
    return {
        'elemod': 2, 'id': 'splash', 'version': '1.0',
        'title': title,
        'description': 'Your boot splash (%s%s), drawn over the boot intro while it runs. '
                       'Made with the Splash screen maker.' % (what, ', from ' + source if source else ''),
        'category': 'Boot', 'author': str(author)[:60], 'license': 'GPL-2.0-or-later',
        'target': devices.target_of(dev, rel),
        'sections': {'.run': {'align': 4, 'len': len(body), 'parts': [['hex', body.hex()]]}},
        'symbols': {k: ['.run', v] for k, v in SYMBOLS.items()},
        'exports': ['splash_diff', 'splash_flush', 'splash_tick'],
        'imports': [], 'weak': [], 'relocs': [],
        'sites': [_site(a, 'splash_diff', t['diff']) for a in t['diff_sites']]
                 + [_site(a, 'splash_flush', t['flush']) for a in t['flush_sites']]
                 + [_site(t['isr'] + TICK_SITE, 'splash_tick', t['post'])],
        'collections': {}, 'contribute': [], 'copied': [],
        'resources': {'names': ['splash']},
        'requires': ['core'], 'conflicts': [],
        'build': {'splash': {'frames': n, 'sequence': seq_n, 'loop': bool(loop)}},
        'signature': None,
    }


def dumps(doc):
    return (json.dumps(doc, indent=1) + '\n').encode()


def file_name(device, os):
    return 'splash-%s-os%s.elemod' % ({'digitakt-mk1': 'dt', 'digitone-mk1': 'dn'}.get(device, device), os)
