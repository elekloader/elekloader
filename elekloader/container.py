# SPDX-License-Identifier: GPL-2.0-or-later
"""Elektron's data container: what the Data router (`/projects`, `/soundbanks`, and the +Drive
loader's `/mods`) sends and takes (docs/FORMAT.md, "The data container").

  header (31 bytes, big-endian): u32 magic 0xac11d303, u8 header version (2), u16 family id,
  u16 device id, u32 OS release, u32 file type, i32 file version, i32 file index, u32 payload
  size (the body's size uncompressed), u8 is-compressed, u8 footer size (12)
  body: the payload (compressed when the header says so)
  footer (12 bytes): u32 hash (CRC-32 of the body: zlib's, from 0xffffffff), u32 the body's size
  as sent, u32 magic 0xaaa1daaa

A stock Analog Rytm MKII 1.74 project download reads family 2, device 5, OS release 0x30313736,
file type 1, file version 9, compressed, with the footer's size the compressed body's.
"""
import struct
import zlib

from .elemod import ModError

MAGIC, TAIL = 0xac11d303, 0xaaa1daaa
HEADER, FOOTER = 31, 12
VERSION = 2
EMOD = 0x454d4f44                # 'EMOD': a +Drive mod file, or another file of /mods
EMOD_VERSION = 2
_HEAD = '>IBHHIIiiIBB'


def crc(body):
    """The footer's hash: CRC-32 from 0xffffffff, as stock and the boot linker compute it."""
    return zlib.crc32(bytes(body), 0xFFFFFFFF) & 0xFFFFFFFF


def wrap(body, family, device, release, file_type=EMOD, version=EMOD_VERSION, index=0):
    """A file's bytes -> the container the Data router takes (never compressed)."""
    body = bytes(body)
    head = struct.pack(_HEAD, MAGIC, VERSION, family, device, release, file_type, version, index,
                       len(body), 0, FOOTER)
    return head + body + struct.pack('>III', crc(body), len(body), TAIL)


def parse(raw):
    """A container -> (header fields as a dict, body). Raises ModError on a bad one. A compressed
    body is returned as it is (`compressed` is set)."""
    raw = bytes(raw)
    if len(raw) < HEADER + FOOTER:
        raise ModError('not a data container: %d bytes' % len(raw))
    (magic, ver, family, device, release, ftype, fver, index, size, comp,
     fsize) = struct.unpack_from(_HEAD, raw)
    if magic != MAGIC:
        raise ModError('not a data container (magic 0x%08x)' % magic)
    if fsize != FOOTER:
        raise ModError('a data container with a %d-byte footer' % fsize)
    body = raw[HEADER:len(raw) - FOOTER]
    hsh, sent, tail = struct.unpack_from('>III', raw, len(raw) - FOOTER)
    if tail != TAIL:
        raise ModError('the data container\'s footer magic is 0x%08x' % tail)
    if sent != len(body):
        raise ModError('the data container\'s footer says %d bytes; the body has %d' % (sent, len(body)))
    if not comp and size != len(body):
        raise ModError('the data container\'s header says %d bytes; the body has %d' % (size, len(body)))
    if crc(body) != hsh:
        raise ModError('the data container\'s hash does not match its body')
    return {'version': ver, 'family': family, 'device': device, 'release': release, 'type': ftype,
            'file_version': fver, 'index': index, 'size': size, 'compressed': bool(comp)}, body


def header_of(raw):
    """Only the header's fields, from the first 31 bytes of a download (the device's id and OS
    release a client copies into its uploads)."""
    raw = bytes(raw[:HEADER])
    if len(raw) < HEADER or struct.unpack_from('>I', raw)[0] != MAGIC:
        raise ModError('not a data container header')
    f = struct.unpack(_HEAD, raw)
    return {'version': f[1], 'family': f[2], 'device': f[3], 'release': f[4], 'type': f[5],
            'file_version': f[6], 'index': f[7], 'size': f[8], 'compressed': bool(f[9])}


def rewrap(head, body):
    """A parsed container back to its bytes (the round trip a test checks against a download)."""
    h = struct.pack(_HEAD, MAGIC, head['version'], head['family'], head['device'], head['release'],
                    head['type'], head['file_version'], head['index'], head['size'],
                    1 if head['compressed'] else 0, FOOTER)
    return h + bytes(body) + struct.pack('>III', crc(body), len(body), TAIL)
