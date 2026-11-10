#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A shop card's thumbnail: a picture of the device's screen, checked.

    python packaging/thumbs.py thumb.png [thumb-<mod id>.png ...] [--md]
    python packaging/thumbs.py thumb.png --png out.png      # the canonical PNG the site serves

A thumbnail is exactly 128x64 pixels in exactly two solid colours: the size and
colours of the Elektron screens (docs/THUMBNAILS.md). Nothing else is
taken, and nothing is converted: a photo scaled down and dithered to two colours
stays recognisable, so the check refuses dithering too. What passes becomes

  - a canonical 1-bit PNG (the lit colour white, no other chunks), the file the
    site serves (packaging/build_web.py), and
  - a braille preview, 64x16 characters with every pixel exact, which is what a
    maintainer sees in the shop's pull request (packaging/submit_mod.py): the
    picture is reviewed as text, and no image is shown there.

Standard library only (it runs in the workflows and in the tests).
"""
import hashlib
import re
import struct
import sys
import zlib

W, H = 128, 64
NAME = 'thumb.png'               # a release's picture for its mods that have none of their own
PICTURE = re.compile(r'thumb(?:-([\w.-]+))?\.png')   # thumb.png, or thumb-<mod id>.png: one mod's, on every device
MAX_PICTURES = 200               # the most one submission takes
MAX_BYTES = 16 << 10             # a 1-bit 128x64 PNG is under 1 KB; this allows an editor's RGBA save
# Dithering: real screens and drawings measure at most 0.09 and 0.03 here, Floyd-Steinberg and ordered
# (Bayer) dithers 0.34 and more on both. The limits sit in the gap.
MAX_ISOLATED = 0.20              # the larger share, of lit or of dark pixels, whose four neighbours all differ
MAX_CHECKER = 0.12               # the share of 2x2 blocks that are a checkerboard


class Refused(Exception):
    pass


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else b if pb <= pc else c


def read_png(data):
    """A PNG's pixels as rows of (r, g, b, a): the colour types and depths image editors save, not interlaced.
    It refuses any size but W x H before it decompresses anything."""
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise Refused('it is not a PNG file')
    pos, idat, plte, trns, ihdr = 8, [], None, None, None
    while pos + 8 <= len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b'IHDR' and n == 13:
            ihdr = struct.unpack('>IIBBBBB', body)
        elif kind == b'PLTE':
            plte = [tuple(body[i:i + 3]) for i in range(0, len(body) - 2, 3)]
        elif kind == b'tRNS':
            trns = body
        elif kind == b'IDAT':
            idat.append(body)
        elif kind == b'IEND':
            break
    if not ihdr:
        raise Refused('the PNG has no header')
    w, h, depth, ctype, _, _, interlace = ihdr
    if (w, h) != (W, H):
        raise Refused('it is %dx%d; a thumbnail is exactly %dx%d, the size of the screen' % (w, h, W, H))
    if interlace:
        raise Refused('it is an interlaced PNG; save it without interlacing')
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if chans is None or depth not in (1, 2, 4, 8) or (ctype in (2, 4, 6) and depth != 8):
        raise Refused('it is a %d-bit PNG of colour type %d; save it as 1-bit or 8-bit' % (depth, ctype))
    if ctype == 3 and not plte:
        raise Refused('it is a palette PNG without a palette')
    stride = (w * chans * depth + 7) // 8
    try:
        raw = zlib.decompressobj().decompress(b''.join(idat), h * (stride + 1) + 1)
    except zlib.error:
        raise Refused('its image data is damaged')
    if len(raw) != h * (stride + 1):
        raise Refused('its image data has the wrong length')
    bpp = max(1, chans * depth // 8)
    prev, rows = bytearray(stride), []
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if f > 4:
            raise Refused('its image data is damaged')
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            c = prev[i - bpp] if i >= bpp else 0
            line[i] = (line[i] + (0, a, prev[i], (a + prev[i]) // 2, _paeth(a, prev[i], c))[f]) & 0xFF
        prev = line
        if depth < 8:
            per, mask = 8 // depth, (1 << depth) - 1
            s = [(line[x // per] >> (8 - depth * (x % per + 1))) & mask for x in range(w)]
        else:
            s = line
        if ctype == 3:
            row = [tuple(plte[i]) + (trns[i] if trns and i < len(trns) else 255,) if i < len(plte) else (0, 0, 0, 255)
                   for i in s]
        elif ctype == 0:
            row = [(v * 255 // ((1 << depth) - 1),) * 3 + (255,) for v in s]
        elif ctype == 4:
            row = [(s[2 * x],) * 3 + (s[2 * x + 1],) for x in range(w)]
        elif ctype == 2:
            row = [tuple(s[3 * x:3 * x + 3]) + (255,) for x in range(w)]
        else:
            row = [tuple(s[4 * x:4 * x + 4]) for x in range(w)]
        rows.append(row)
    return rows


def to_bits(rows):
    """Two solid colours -> rows of 0 (the darker) and 1 (the lighter, lit)."""
    colours = {p for row in rows for p in row}
    if any(p[3] != 255 for p in colours):
        raise Refused('it has transparent pixels; use two solid colours')
    colours = {p[:3] for p in colours}
    if len(colours) > 2:
        raise Refused('it has %d colours; a thumbnail has exactly two (no greys: switch anti-aliasing off)'
                      % len(colours))
    if len(colours) < 2:
        raise Refused('it is one flat colour')
    lit = max(colours, key=lambda c: (c[0] * 299 + c[1] * 587 + c[2] * 114, c))
    return [[1 if p[:3] == lit else 0 for p in row] for row in rows]


def texture(bits):
    """-> (isolated, checker): how much of the picture is dots rather than drawing."""
    lone, count = [0, 0], [0, 0]
    for y in range(H):
        for x in range(W):
            v = bits[y][x]
            count[v] += 1
            if all(bits[yy][xx] != v for yy, xx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1))
                   if 0 <= yy < H and 0 <= xx < W):
                lone[v] += 1
    checker = sum(1 for y in range(H - 1) for x in range(W - 1)
                  if bits[y][x] == bits[y + 1][x + 1] != bits[y][x + 1] == bits[y + 1][x])
    return max(lone[v] / count[v] for v in (0, 1)), checker / ((W - 1) * (H - 1))


def check(data):
    """-> {ok, sha256, bytes, and reason (refused) or bits, isolated, checker (taken)}."""
    out = {'ok': False, 'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)}
    try:
        if len(data) > MAX_BYTES:
            raise Refused('it is %d bytes; a thumbnail is at most %d' % (len(data), MAX_BYTES))
        bits = to_bits(read_png(data))
        isolated, checker = texture(bits)
        out.update(isolated=round(isolated, 4), checker=round(checker, 4))
        if isolated > MAX_ISOLATED or checker > MAX_CHECKER:
            raise Refused('it looks dithered (shading or a photo turned into dots); draw it with lines, shapes '
                          'and text, or use a screenshot of the screen')
        out.update(ok=True, bits=bits)
    except Refused as e:
        out['reason'] = str(e)
    return out


def braille(bits):
    """Every pixel as braille dots, 2x4 a character: 64x16 characters."""
    dots = ((0, 0, 0x01), (0, 1, 0x02), (0, 2, 0x04), (1, 0, 0x08), (1, 1, 0x10), (1, 2, 0x20), (0, 3, 0x40), (1, 3, 0x80))
    return '\n'.join(''.join(chr(0x2800 + sum(m for dx, dy, m in dots if bits[y + dy][x + dx])) for x in range(0, W, 2))
                     for y in range(0, H, 4))


def canonical_png(bits):
    """The site's file: a 1-bit greyscale PNG, lit = white, IHDR, IDAT and IEND only."""
    raw = b''.join(b'\x00' + bytes(sum(bits[y][x + i] << (7 - i) for i in range(8)) for x in range(0, W, 8))
                   for y in range(H))
    def chunk(k, d):
        return struct.pack('>I', len(d)) + k + d + struct.pack('>I', zlib.crc32(k + d))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 1, 0, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def preview_md(r, name=NAME):
    """A thumbnail's check, for a report: why it was refused, or its pixels as text."""
    if not r['ok']:
        return '`%s` was not taken: %s.\n' % (name, r['reason'])
    return ('`%s`: 128×64, two colours, %d bytes, sha256 `%s`. Its pixels, as text (no image is shown here):\n\n'
            '```text\n%s\n```\n' % (name, r['bytes'], r['sha256'][:12], braille(r['bits'])))


def main(argv=None):
    args = sys.argv[1:] if argv is None else argv
    out = args[args.index('--png') + 1] if '--png' in args else None
    files = [a for a in args if not a.startswith('-') and a != out]
    if not files or (out and len(files) > 1):
        sys.exit(__doc__)
    if '--md' in args:
        sys.stdout.reconfigure(encoding='utf-8')
    bad = 0
    for path in files:
        with open(path, 'rb') as fh:
            r = check(fh.read())
        bad += not r['ok']
        if '--md' in args:
            sys.stdout.write(preview_md(r, path))
        else:
            print('%s%s' % (path + ': ' if len(files) > 1 else '',
                            'taken (isolated %.3f, checker %.3f)' % (r['isolated'], r['checker']) if r['ok']
                            else 'refused: ' + r['reason']))
        if r['ok'] and out:
            with open(out, 'wb') as fh:
                fh.write(canonical_png(r['bits']))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
