# SPDX-License-Identifier: GPL-2.0-or-later
"""The unit's Data router over USB MIDI, and `elekloader mods` (docs/FORMAT.md, "+Drive mods").

Elektron's units answer a request/response protocol in SysEx: F0 00 20 3C 10 00, the message in
7-bit groups (one byte of high bits, then up to seven bytes), F7. A message is a u16 sequence
number, two zero bytes, a command byte and its arguments; the reply carries the request's sequence
number at +2, its command | 0x80 at +4 and a status byte at +5 (1: done). The Data router's commands
(big-endian words; paths NUL-terminated):

  0x53 list    path, i32 first, i32 last, u8 all -> from +18: per item its name, u8 has children,
               u8 kind; kind 1: u32; kind 2: i32 index, u32 size, u16 operations, u8 valid, u8 meta
  0x54 open to read    path, u32 chunk, u8 compressed -> u32 job, u32 chunk, u8 compressed
  0x55 read    u32 job, u32 sequence -> u32 job, u32 sequence, u32 progress, u8 last, u32 hash,
               u32 n, n bytes
  0x56 close a read    u32 job
  0x57 open to write   u32 size, path -> u32 job
  0x58 write   u32 job, u32 sequence, u32 CRC-32 of the chunk, u32 n, n bytes -> u32 job,
               u32 sequence, u32 bytes taken so far
  0x59 close a write   u32 job, u32 size -> u32 job, u32 size taken
  0x5c clear   path

What goes through it is a data container (container.py). The Drive Loader's `/mods` route takes
`.elemod` files and `active`, and serves `.build` (the build descriptor) and `.status`.
"""
import os
import struct
import sys
import time

from . import container, drive
from .elemod import ModError

HEAD = bytes((0xF0, 0x00, 0x20, 0x3C, 0x10, 0x00))
LIST, ROPEN, READ, RCLOSE, WOPEN, WRITE, WCLOSE, CLEAR = 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5C
CHUNK = 0x2000
FOLDER = '/mods'


def pack7(data):
    """Bytes -> Elektron's 7-bit groups."""
    out = bytearray()
    for i in range(0, len(data), 7):
        g = data[i:i + 7]
        hi = 0
        for k, b in enumerate(g):
            if b & 0x80:
                hi |= 0x40 >> k
        out.append(hi)
        out += bytes(b & 0x7F for b in g)
    return bytes(out)


def unpack7(data):
    """Elektron's 7-bit groups -> bytes."""
    out = bytearray()
    for i in range(0, len(data), 8):
        hi, g = data[i], data[i + 1:i + 8]
        out += bytes(b | (0x80 if hi & (0x40 >> k) else 0) for k, b in enumerate(g))
    return bytes(out)


def frame(msg):
    return HEAD + pack7(msg) + b'\xf7'


def unframe(raw):
    """A SysEx message -> the message, or None if it is not one of these."""
    raw = bytes(raw)
    if len(raw) < 8 or raw[:6] != HEAD or raw[-1] != 0xF7:
        return None
    return unpack7(raw[6:-1])


class RpcError(ModError):
    pass


class Client:
    """Requests over a port: an object with send(bytes) and receive(timeout) -> bytes or None (one
    whole SysEx message, F0 to F7)."""

    def __init__(self, port, timeout=5.0):
        self.port, self.timeout, self.seq = port, timeout, 0

    def call(self, cmd, args=b''):
        seq = self.seq
        self.seq = (self.seq + 1) & 0xFFFF
        self.port.send(frame(struct.pack('>HH', seq, 0) + bytes((cmd,)) + bytes(args)))
        end = time.monotonic() + self.timeout
        while True:
            left = end - time.monotonic()
            if left <= 0:
                raise RpcError('no reply from the unit to command 0x%02x' % cmd)
            raw = self.port.receive(left)
            if raw is None:
                continue
            msg = unframe(raw)
            if msg is None or len(msg) < 6 or struct.unpack_from('>H', msg, 2)[0] != seq:
                continue
            if msg[4] != cmd | 0x80:
                raise RpcError('the unit answered command 0x%02x with 0x%02x' % (cmd, msg[4]))
            if msg[5] != 1:
                text = msg[6:].split(b'\0', 1)[0].decode('latin-1', 'replace')
                raise RpcError('the unit refused command 0x%02x%s' % (cmd, ': ' + text if text else ''))
            return msg

    # -- the Data router
    def ls(self, path):
        """-> [(name, kind, size)] (kind 1 a folder, 2 an item)."""
        msg = self.call(LIST, path.encode('latin-1') + b'\0' + struct.pack('>iiB', 0, 0, 1))
        out, at = [], 18
        while at < len(msg):
            end = msg.index(b'\0', at)
            name = msg[at:end].decode('latin-1')
            _children, kind = msg[end + 1], msg[end + 2]
            at = end + 3
            if kind == 1:
                out.append((name, 1, 0))
                at += 4
            else:
                _index, size = struct.unpack_from('>iI', msg, at)
                out.append((name, 2, size))
                at += 12
        return out

    def get(self, path):
        """-> the bytes the unit serves (a data container)."""
        msg = self.call(ROPEN, path.encode('latin-1') + b'\0' + struct.pack('>IB', CHUNK, 0))
        job = struct.unpack_from('>I', msg, 6)[0]
        data, seq = bytearray(), 0
        try:
            while True:
                msg = self.call(READ, struct.pack('>II', job, seq))
                last = msg[18]
                n = struct.unpack_from('>I', msg, 23)[0]
                data += msg[27:27 + n]
                seq += 1
                if last:
                    break
        finally:
            self.call(RCLOSE, struct.pack('>I', job))
        return bytes(data)

    def put(self, path, data):
        """Write `data` (a data container) to `path`."""
        data = bytes(data)
        msg = self.call(WOPEN, struct.pack('>I', len(data)) + path.encode('latin-1') + b'\0')
        job = struct.unpack_from('>I', msg, 6)[0]
        for seq, at in enumerate(range(0, len(data), CHUNK)):
            chunk = data[at:at + CHUNK]
            msg = self.call(WRITE, struct.pack('>IIII', job, seq, container.crc(chunk), len(chunk)) + chunk)
            if struct.unpack_from('>I', msg, 14)[0] != at + len(chunk):
                raise RpcError('the unit took %d bytes of %d' % (struct.unpack_from('>I', msg, 14)[0],
                                                                 at + len(chunk)))
        msg = self.call(WCLOSE, struct.pack('>II', job, len(data)))
        if struct.unpack_from('>I', msg, 10)[0] != len(data):
            raise RpcError('the unit kept %d bytes of %d' % (struct.unpack_from('>I', msg, 10)[0], len(data)))

    def clear(self, path):
        self.call(CLEAR, path.encode('latin-1') + b'\0')


class Mods:
    """`/mods` on a unit: files in and out of their containers."""

    def __init__(self, client, folder=FOLDER):
        self.c, self.folder = client, folder.rstrip('/')

    def path(self, name):
        if '/' in name or not name:
            raise ModError('%r is not a file name in %s' % (name, self.folder))
        return '%s/%s' % (self.folder, name)

    def ls(self):
        return self.c.ls(self.folder)

    def get(self, name):
        """-> (the container's header, the file's bytes)."""
        return container.parse(self.c.get(self.path(name)))

    def build(self):
        """The unit's build (its descriptor, `.build`) and the container header it came in."""
        head, body = self.get('.build')
        return drive.load_build(body), head

    def put(self, name, raw, head):
        """Upload a file in a container with the unit's family, device and OS (from `head`)."""
        self.c.put(self.path(name), container.wrap(raw, head['family'], head['device'], head['release']))

    def rm(self, name):
        self.c.clear(self.path(name))


# ---- ports ---------------------------------------------------------------------------------------

class MidoPort:
    """A unit's USB MIDI ports through mido (pip install mido python-rtmidi)."""

    def __init__(self, name=None):
        try:
            import mido
        except ImportError:
            raise ModError('elekloader mods needs mido and python-rtmidi: pip install mido python-rtmidi')
        ins, outs = mido.get_input_names(), mido.get_output_names()
        want = name or 'Rytm'
        i = next((n for n in ins if want.lower() in n.lower()), None)
        o = next((n for n in outs if want.lower() in n.lower()), None)
        if not i or not o:
            raise ModError('no MIDI port named like %r (inputs: %s)' % (want, ', '.join(ins) or 'none'))
        self.i, self.o = mido.open_input(i), mido.open_output(o)
        self.mido = mido

    def send(self, raw):
        self.o.send(self.mido.Message('sysex', data=bytes(raw[1:-1])))

    def receive(self, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            m = self.i.poll()
            if m is None:
                time.sleep(0.002)
                continue
            if m.type == 'sysex':
                return bytes((0xF0,)) + bytes(m.data) + bytes((0xF7,))
        return None


# ---- elekloader mods -------------------------------------------------------------------------------

def add_arguments(p):
    p.add_argument('--port', help='the MIDI port, or a part of its name (default: Rytm)')
    p.add_argument('--folder', default=FOLDER, help='the route\'s folder (default /mods)')
    sub = p.add_subparsers(dest='op', required=True)
    sub.add_parser('ls', help='list /mods')
    q = sub.add_parser('get', help='download a file (also .build and .status)')
    q.add_argument('name')
    q.add_argument('--out', help='where to write it (default: its name)')
    q = sub.add_parser('put', help='check the files against the unit\'s build, then upload them')
    q.add_argument('files', nargs='+')
    q = sub.add_parser('rm', help='delete a file')
    q.add_argument('name')
    q = sub.add_parser('active', help='choose the mods the unit loads at its next boot, in this order')
    q.add_argument('names', nargs='*', help='local .elemod files, or names already in /mods')


def run(a, mods, log=print):
    """`elekloader mods` with a Mods object. -> the exit code."""
    if a.op == 'ls':
        for name, kind, size in mods.ls():
            log('%s%s %d' % (name, '/' if kind == 1 else '', size))
        return 0
    if a.op == 'get':
        _head, body = mods.get(a.name)
        out = a.out or a.name
        with open(out, 'wb') as fh:
            fh.write(body)
        log('WROTE %s (%d bytes)' % (out, len(body)))
        return 0
    if a.op == 'rm':
        mods.rm(a.name)
        log('DELETED %s' % mods.path(a.name))
        return 0
    build, head = mods.build()
    if a.op == 'put':
        files = drive._files(a.files)
        r = drive.check(build, files)
        drive.print_result(r, log)
        if not all(c.state == drive.LOADED for c in r.choices):
            log('REFUSED: nothing was uploaded')
            return 1
        for name, raw in files:
            mods.put(name, raw, head)
            log('UPLOADED %s (%d bytes, CRC-32 %08x)' % (mods.path(name), len(raw), drive.file_crc(raw)))
        return 0
    chosen = []                                 # active
    for n in a.names:
        if os.path.isfile(n):
            with open(n, 'rb') as fh:
                chosen.append((os.path.basename(n), fh.read()))
        else:
            chosen.append((n, mods.get(n)[1]))
    r = drive.check(build, chosen)
    drive.print_result(r, log)
    if not all(c.state == drive.LOADED for c in r.choices):
        log('REFUSED: %s is unchanged' % mods.path('active'))
        return 1
    text = drive.active_text(chosen)
    mods.put('active', text.encode('utf-8'), head)
    log('WROTE %s (%d mods)' % (mods.path('active'), len(chosen)))
    return 0


def main(a):
    try:
        return run(a, Mods(Client(MidoPort(a.port)), a.folder))
    except (OSError, ModError) as e:
        print('REFUSED: %s' % e)
        return 2


if __name__ == '__main__':
    sys.exit(drive.main(['mods'] + sys.argv[1:]))
