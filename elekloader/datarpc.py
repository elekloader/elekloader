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
`.elemod` files and `active`, and serves `.build` (the build descriptor), `.status` and `.store`
(+Drive storage's places, read only). The router has
no command for a folder and refuses its own Rename on the route's entries: an upload of an empty
file to `FOLDER/.dir` asks the route to make FOLDER (`Mods.mkdir`), an upload of "FROM\nTO" to `.mv`
to move or rename a file or a folder (`Mods.move`; `/mods/active` follows it), and a clear of an
empty folder removes it.
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
# a read's chunk sizes, largest first: when a reply never arrives (a host whose MIDI input drops SysEx
# over about 1 KB, as Firefox's Web MIDI and python-rtmidi on Windows), the read starts again with the
# next; 0x300 bytes travel as a SysEx message of about 920 bytes
READ_CHUNKS = (CHUNK, 0x300)
NO_REPLY = 'no reply from the unit to command 0x%02x'
FOLDER = '/mods'
DEPTH = 3                       # folders below FOLDER the route reaches (the loader's DL_DEPTH)
MKDIR = '.dir'                  # an upload to FOLDER/.dir makes FOLDER (the loader's DL_MKDIR)
MOVE = '.mv'                    # an upload of "FROM\nTO" to .mv moves FROM to TO (the loader's DL_MOVE)
NAME_MAX, SEG_MAX = 127, 63     # a path below FOLDER, one name in it, in bytes (DL_NAME_MAX, DL_SEG_MAX)


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
        self.read_chunk = CHUNK                 # the largest read chunk that has come through

    def call(self, cmd, args=b''):
        seq = self.seq
        self.seq = (self.seq + 1) & 0xFFFF
        self.port.send(frame(struct.pack('>HH', seq, 0) + bytes((cmd,)) + bytes(args)))
        end = time.monotonic() + self.timeout
        while True:
            left = end - time.monotonic()
            if left <= 0:
                raise RpcError(NO_REPLY % cmd)
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
        """-> [(name, kind, size)] (kind 1 a folder, 2 an item). An item whose children byte is
        set can be listed itself (a route's folder, as /soundbanks' banks): it is given as kind 1."""
        msg = self.call(LIST, path.encode('latin-1') + b'\0' + struct.pack('>iiB', 0, 0, 1))
        out, at = [], 18
        while at < len(msg):
            end = msg.index(b'\0', at)
            name = msg[at:end].decode('latin-1')
            children, kind = msg[end + 1], msg[end + 2]
            at = end + 3
            if kind == 1:
                out.append((name, 1, 0))
                at += 4
            else:
                _index, size = struct.unpack_from('>iI', msg, at)
                out.append((name, 1, 0) if children else (name, 2, size))
                at += 12
        return out

    def get(self, path):
        """-> the bytes the unit serves (a data container). A read whose reply never arrives starts
        again with a smaller chunk (READ_CHUNKS); the client keeps the size that worked."""
        sizes = [c for c in READ_CHUNKS if c <= self.read_chunk]
        for i, chunk in enumerate(sizes):
            try:
                data = self._get(path, chunk)
            except RpcError as e:
                if i + 1 == len(sizes) or not str(e).endswith(NO_REPLY % READ):
                    raise
                continue
            self.read_chunk = chunk
            return data

    def _get(self, path, chunk):
        # The bytes are counted here, never taken from a reply's progress word: stock's per-mille
        # progress (ReadPartial, done * 1000 in 32 bits) wraps past 4,294,967 bytes, and with the
        # stage in the +Drive area's free tail a read can be larger than that.
        msg = self.call(ROPEN, path.encode('latin-1') + b'\0' + struct.pack('>IB', chunk, 0))
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
        except RpcError as e:
            try:                                # close it, but say why the read stopped:
                self.call(RCLOSE, struct.pack('>I', job))   # the unit refuses to close an unfinished read
            except RpcError:
                pass
            raise RpcError('reading %s, chunk %d: %s' % (path, seq, e))
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
        """`name`: a file below the folder, in up to DEPTH folders ("drums/kicks/x.elemod")."""
        parts = name.split('/')
        if not name or len(parts) > DEPTH + 1 or any(not p or p in ('.', '..') for p in parts) \
                or any(p.startswith('.') for p in parts[:-1]) or any(p.startswith(' ') for p in parts):
            raise ModError('%r is not a file name in %s' % (name, self.folder))
        return '%s/%s' % (self.folder, name)

    def ls(self, sub=''):
        """One folder: [(name, kind, size)] (kind 1 a folder, 2 a file)."""
        return self.c.ls('%s/%s' % (self.folder, sub) if sub else self.folder)

    def walk(self):
        """Every file below the folder, to DEPTH folders deep, one listing a folder:
        [(path below the folder, size)], the folder's own files first, then each folder's."""
        out, todo = [], ['']
        while todo:
            sub = todo.pop(0)
            for name, kind, size in self.ls(sub):
                p = '%s/%s' % (sub, name) if sub else name
                if kind == 1:
                    if p.count('/') < DEPTH:
                        todo.append(p)
                else:
                    out.append((p, size))
        return out

    def get(self, name):
        """-> (the container's header, the file's bytes)."""
        return container.parse(self.c.get(self.path(name)))

    def status(self):
        """`/mods/.status` as text, or None when it can't be read."""
        try:
            return self.get('.status')[1].decode('utf-8', 'replace')
        except (RpcError, ModError):
            return None

    def room(self, build):
        """The most bytes one upload or read takes on the unit now (drive.upload_room)."""
        return drive.upload_room(build, self.status())

    def build(self):
        """The unit's build (its descriptor, `.build`) and the container header it came in."""
        head, body = self.get('.build')
        return drive.load_build(body), head

    def put(self, name, raw, head):
        """Upload a file in a container with the unit's family, device and OS (from `head`)."""
        self.c.put(self.path(name), container.wrap(raw, head['family'], head['device'], head['release']))

    def rm(self, name):
        self.c.clear(self.path(name))

    def folder_path(self, sub):
        """`sub`: a folder below the folder, up to DEPTH deep ("drums/kicks")."""
        parts = sub.split('/')
        if not sub or len(parts) > DEPTH or any(not p or p.startswith(('.', ' ')) for p in parts):
            raise ModError('%r is not a folder name in %s (up to %d deep, no part starting with . or a space)'
                           % (sub, self.folder, DEPTH))
        return '%s/%s' % (self.folder, sub)

    def mkdir(self, sub, head):
        """Make the folder `sub` below the folder, and its parents, empty (an upload of nothing to
        `sub/.dir`, in a container as `put`'s). The route can't refuse once the upload is in, so the
        parent's listing says whether the folder is there."""
        self.folder_path(sub)
        self.put('%s/%s' % (sub, MKDIR), b'', head)
        parent, _, name = sub.rpartition('/')
        if not any(n == name and kind == 1 for n, kind, _size in self.ls(parent)):
            raise ModError('the unit did not make %s (see %s/.status)' % (self.folder_path(sub), self.folder))

    def _ls(self, sub):
        try:
            return self.ls(sub)
        except RpcError:                        # no such folder
            return []

    def is_folder(self, sub):
        """`sub` (below the folder) is a folder on the unit."""
        return bool(sub) and any(n == _base(sub) and kind == 1 for n, kind, _s in self._ls(_parent(sub)))

    def move(self, frm, to, head, folder=None):
        """Move or rename the file or folder `frm` to `to` (paths below the folder; a folder's
        files and folders go with it), as one upload of "FROM\nTO" to `.mv`. The route keeps
        /mods/active's lines naming them, refuses a name already there, and keeps the depth and
        name rules; it can't refuse once the upload is in, so the listings say whether it moved.
        `folder`: whether frm is a folder (None: look in its parent)."""
        frm, to = frm.strip('/'), to.strip('/')
        if folder is None:
            folder = self.is_folder(frm)
        for p in (frm, to):
            parts = p.split('/')
            if not p or any(not q or q.startswith(('.', ' ')) or len(q.encode('latin-1', 'replace')) > SEG_MAX
                            for q in parts) or len(p.encode('latin-1', 'replace')) > NAME_MAX:
                raise ModError('%r is not a name in %s' % (p, self.folder))
        if frm == to:
            raise ModError('%s is already %s' % (frm, to))
        if folder:
            if to.startswith(frm + '/'):
                raise ModError('%s can\'t go into itself' % self.folder_path(frm))
            if to.count('/') + 1 > DEPTH:
                raise ModError('folders go %d deep below %s' % (DEPTH, self.folder))
        elif not (frm.endswith('.elemod') and to.endswith('.elemod') and len(_base(to)) > 7):
            raise ModError('a mod file is moved to a .elemod name (%r)' % to)
        elif to.count('/') > DEPTH:
            raise ModError('%r is not a file name in %s' % (to, self.folder))
        self.put(MOVE, ('%s\n%s' % (frm, to)).encode('latin-1'), head)
        there = any(n == _base(to) and kind == (1 if folder else 2) for n, kind, _s in self._ls(_parent(to)))
        gone = not any(n == _base(frm) for n, _k, _s in self._ls(_parent(frm)))
        if not there or not gone:
            raise ModError('the unit did not move %s/%s to %s/%s (see %s/.status)'
                           % (self.folder, frm, self.folder, to, self.folder))


def _parent(p):
    return p.rpartition('/')[0]


def _base(p):
    return p.rpartition('/')[2]


# ---- ports ---------------------------------------------------------------------------------------

def open_port(name=None):
    """A unit's USB MIDI ports: winmm's own on Windows (winmm.py: python-rtmidi drops SysEx over
    1 KB there, and every read reply is longer), mido elsewhere."""
    if sys.platform == 'win32':
        from .winmm import WinmmPort
        return WinmmPort(name)
    return MidoPort(name)


class MidoPort:
    """A unit's USB MIDI ports through mido (pip install mido python-rtmidi). Not on Windows:
    open_port."""

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
    q = sub.add_parser('ls', help='list /mods, or one of its folders')
    q.add_argument('folder_path', nargs='?', help='a folder below /mods (default: /mods itself)')
    sub.add_parser('room', help='the most bytes one upload or read takes on the unit now '
                   '(/mods/.status, stage: the +Drive area\'s free tail, or the loader\'s buffer)')
    q = sub.add_parser('get', help='download a file (also .build, .status and .store)')
    q.add_argument('name')
    q.add_argument('--out', help='where to write it (default: its name)')
    q = sub.add_parser('put', help='check the files against the unit\'s build, then upload them')
    q.add_argument('files', nargs='+')
    q.add_argument('--into', help='a folder below /mods to upload into, up to %d deep ("fx/delays"); '
                   'its folders are made as needed' % DEPTH)
    q = sub.add_parser('rm', help='delete a file, or an empty folder')
    q.add_argument('name')
    q = sub.add_parser('mkdir', help='make an empty folder below /mods, up to %d deep, with its parents' % DEPTH)
    q.add_argument('folder_path')
    q = sub.add_parser('mv', help='move or rename a file or a folder below /mods (/mods/active follows it)')
    q.add_argument('source')
    q.add_argument('dest', help='the new path below /mods; a folder that is there takes it in, keeping its name')
    q = sub.add_parser('active', help='choose the mods the unit loads at its next boot, in this order')
    q.add_argument('names', nargs='*', help='local .elemod files, or names already in /mods')


def run(a, mods, log=print):
    """`elekloader mods` with a Mods object. -> the exit code."""
    if a.op == 'ls':
        for name, kind, size in mods.ls(getattr(a, 'folder_path', None) or ''):
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
    if a.op == 'room':
        n = mods.room(build)
        log('ROOM %s' % ('%d bytes' % n if n else 'unknown'))
        return 0
    if a.op == 'mv':
        src, dest = a.source.strip('/'), a.dest.strip('/')
        if dest == '' or mods.is_folder(dest):
            dest = '%s/%s' % (dest, _base(src)) if dest else _base(src)   # into that folder
        mods.move(src, dest, head)
        log('MOVED %s/%s to %s/%s' % (mods.folder, src, mods.folder, dest))
        return 0
    if a.op == 'mkdir':
        sub = a.folder_path.strip('/')
        mods.mkdir(sub, head)
        log('MADE %s' % mods.folder_path(sub))
        return 0
    if a.op == 'put':
        files = drive._files(a.files)
        into = (getattr(a, 'into', None) or '').strip('/')
        if into:
            files = [('%s/%s' % (into, n), raw) for n, raw in files]
            for n, _raw in files:
                mods.path(n)                    # refused here, before anything is checked or sent
        r = drive.check(build, files)
        drive.print_result(r, log)
        if not all(c.state == drive.LOADED for c in r.choices):
            log('REFUSED: nothing was uploaded')
            return 1
        room = mods.room(build)                 # the live stage: it changes with each boot's set
        over = [(n, raw) for n, raw in files if room and len(raw) > room]
        if over:
            for n, raw in over:
                log('REFUSED: %s is %d bytes; the unit takes up to %d now (%s/.status, stage)'
                    % (n, len(raw), room, mods.folder))
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
        return run(a, Mods(Client(open_port(a.port)), a.folder))
    except (OSError, ModError) as e:
        print('REFUSED: %s' % e)
        return 2


if __name__ == '__main__':
    sys.exit(drive.main(['mods'] + sys.argv[1:]))
