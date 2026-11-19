#!/usr/bin/env python3
"""A small Elektron Data RPC client, for checking drive-loader's /mods route in an emulator.

Talks to an emulator's --usb-midi-tcp port (a raw MIDI byte stream). Messages as in elektroid
(src/connectors/elektron.c): F0 00 20 3C 10 00, 7-bit packed [seq hi, seq lo, cmd, args...], F7.
Data commands: 0x53 list, 0x54 read open, 0x55 read partial, 0x56 read close, 0x57 write open,
0x58 write partial, 0x59 write close, 0x5c clear. A message is [seq hi, seq lo, 0, 0, cmd, args]; a reply
[?, ?, seq hi, seq lo, cmd | 0x80, status, ...], a NUL-terminated message at 6 on refusal.

    modsrpc.py PORT ls PATH
    modsrpc.py PORT get PATH OUT          (container stripped, CRC checked)
    modsrpc.py PORT put FILE PATH [--type 0x454d4f44] [--badcrc] [--compressed] [--hold]
    modsrpc.py PORT rm PATH
    modsrpc.py PORT overlap FILE_A PATH_A FILE_B PATH_B   (A's upload left open while B uploads)

From Git Bash on Windows, set MSYS_NO_PATHCONV=1, or "/mods" arrives as a Windows path.
Exit status 0 on success, 1 when the unit refused (the message is printed).
"""
import socket
import struct
import sys
import time
import zlib

HEADER = bytes([0xF0, 0x00, 0x20, 0x3C, 0x10, 0x00])
MAGIC_HEAD, MAGIC_TAIL = 0xac11d303, 0xaaa1daaa
EMOD = 0x454d4f44
CHUNK = 0x2000


def pack7(msg):
    out = bytearray()
    for j in range(0, len(msg), 7):
        chunk = msg[j:j + 7]
        top = 0
        for k in range(7):
            top <<= 1
            if k < len(chunk) and chunk[k] & 0x80:
                top |= 1
        out.append(top)
        out += bytes(b & 0x7F for b in chunk)
    return HEADER + bytes(out) + b'\xF7'


def unpack7(sx):
    src = sx[len(HEADER):-1]
    out = bytearray()
    for i in range(0, len(src), 8):
        top = src[i]
        for k, b in enumerate(src[i + 1:i + 8]):
            out.append(b | (0x80 if top & (0x40 >> k) else 0))
    return bytes(out)


def crc(b):
    return zlib.crc32(b, 0xffffffff) & 0xffffffff  # elektroid's crc32(0xffffffff, ...)


def container(body, ftype=EMOD, compressed=0, badcrc=False, family=2, device=0, os_rel=0):
    hdr = struct.pack('>IBHHIIiiIBB', MAGIC_HEAD, 1, family, device, os_rel, ftype, 2, 0,
                      len(body), compressed, 12)
    c = crc(body)
    if badcrc:
        c ^= 1
    return hdr + body + struct.pack('>III', c, len(body), MAGIC_TAIL)


def uncontainer(raw):
    assert len(raw) >= 43, 'short container'
    magic, ver, fam, dev, osr, ftype, fver, fidx, size, comp, fsz = struct.unpack('>IBHHIIiiIBB', raw[:31])
    assert magic == MAGIC_HEAD, 'bad head magic %08x' % magic
    body = raw[31:31 + size]
    h, csize, tail = struct.unpack('>III', raw[31 + size:31 + size + 12])
    assert tail == MAGIC_TAIL, 'bad tail magic'
    assert h == crc(body), 'CRC mismatch'
    return {'type': ftype, 'family': fam, 'device': dev, 'os': osr}, body


class Rpc:
    def __init__(self, port, timeout=20.0):
        self.s = socket.create_connection(('127.0.0.1', int(port)))
        self.s.settimeout(timeout)
        self.seq = 1
        self.buf = bytearray()

    def _read_sysex(self):
        while True:
            if 0xF0 in self.buf:
                a = self.buf.index(0xF0)
                if 0xF7 in self.buf[a:]:
                    b = self.buf.index(0xF7, a)
                    sx = bytes(self.buf[a:b + 1])
                    del self.buf[:b + 1]
                    return sx
            self.buf += self.s.recv(65536)

    def call(self, cmd, args=b''):
        seq = self.seq
        self.seq = (self.seq + 1) & 0x3fff
        self.s.sendall(pack7(struct.pack('>HHB', seq, 0, cmd) + args))
        while True:
            sx = self._read_sysex()
            if sx[:len(HEADER)] != HEADER:
                continue
            m = unpack7(sx)
            if len(m) >= 6 and struct.unpack('>H', m[2:4])[0] == seq and m[4] == cmd | 0x80:
                return m

    @staticmethod
    def ok(m):
        return m[5] != 0

    @staticmethod
    def text(m, at=6):
        return m[at:m.index(0, at)].decode('latin-1') if 0 in m[at:] else m[at:].decode('latin-1')

    def ls(self, path):
        m = self.call(0x53, path.encode() + b'\0' + struct.pack('>IIB', 0, 0xffff, 1))
        return m

    def read(self, path):
        m = self.call(0x54, path.encode() + b'\0' + struct.pack('>IB', CHUNK, 0))
        if not self.ok(m):
            raise RuntimeError('read open refused: ' + self.text(m))
        jid = struct.unpack('>I', m[6:10])[0]
        out, seq = bytearray(), 0
        while True:
            r = self.call(0x55, struct.pack('>II', jid, seq))
            if not self.ok(r):
                raise RuntimeError('read partial refused: ' + self.text(r))
            last = r[18]
            n = struct.unpack('>I', r[23:27])[0]
            out += r[27:27 + n]
            seq += 1
            if last:
                break
        self.call(0x56, struct.pack('>I', jid))
        return bytes(out)

    def write_open(self, path, size):
        m = self.call(0x57, struct.pack('>I', size) + path.encode() + b'\0')
        if not self.ok(m):
            raise RuntimeError('write open refused: ' + self.text(m))
        return struct.unpack('>I', m[6:10])[0]

    def write(self, path, raw, hold=False):
        jid = self.write_open(path, len(raw))
        off, seq = 0, 0
        while off < len(raw):
            chunk = raw[off:off + CHUNK]
            r = self.call(0x58, struct.pack('>IIII', jid, seq, crc(chunk), len(chunk)) + chunk)
            if not self.ok(r):
                raise RuntimeError('write partial refused: ' + self.text(r))
            off += len(chunk)
            seq += 1
        if hold:
            return jid
        r = self.call(0x59, struct.pack('>II', jid, len(raw)))
        if not self.ok(r):
            raise RuntimeError('write close refused: ' + self.text(r))
        return jid

    def rm(self, path):
        r = self.call(0x5c, path.encode() + b'\0')
        if not self.ok(r):
            raise RuntimeError('clear refused: ' + self.text(r))


def main(argv):
    port, cmd = argv[1], argv[2]
    if any(a == '' for a in argv[3:]):
        raise RuntimeError('an empty path: stock 1.74 stops answering SysEx after one (seen in rytm-emu)')
    rpc = Rpc(port)
    if cmd == 'ls':
        m = rpc.ls(argv[3])
        if not rpc.ok(m):
            print('refused: ' + rpc.text(m))
            return 1
        try:
            print(listing(m))
        except (ValueError, IndexError, struct.error):
            print(m.hex())
    elif cmd == 'get':
        meta, body = uncontainer(rpc.read(argv[3]))
        open(argv[4], 'wb').write(body)
        print('got %d bytes, type %08x' % (len(body), meta['type']))
    elif cmd == 'put':
        body = open(argv[3], 'rb').read()
        ftype = int(argv[argv.index('--type') + 1], 0) if '--type' in argv else EMOD
        raw = container(body, ftype, 1 if '--compressed' in argv else 0, '--badcrc' in argv)
        rpc.write(argv[4], raw, hold='--hold' in argv)
        print('put %d bytes' % len(body))
        if '--hold' in argv:
            time.sleep(3600)
    elif cmd == 'rm':
        rpc.rm(argv[3])
        print('removed')
    elif cmd == 'overlap':
        a = container(open(argv[3], 'rb').read())
        jid = rpc.write_open(argv[4], len(a))
        r = rpc.call(0x58, struct.pack('>IIII', jid, 0, crc(a[:CHUNK]), len(a[:CHUNK])) + a[:CHUNK])
        print('A first chunk: ' + ('ok' if rpc.ok(r) else 'refused: ' + rpc.text(r)))
        rpc.write(argv[6], container(open(argv[5], 'rb').read()))
        print('B uploaded')
        r = rpc.call(0x59, struct.pack('>II', jid, len(a)))
        print('A close: ' + ('ok' if rpc.ok(r) else 'refused: ' + rpc.text(r)))
    return 0


def listing(m):
    """A list reply as text: one line per entry (name, kind, then index and size for a file)."""
    out, p = [], 6 + 12                 # after the reply's start, end and count words
    while p < len(m):
        end = m.index(0, p)
        name = m[p:end].decode('latin-1')
        p = end + 1
        _children, kind = m[p], m[p + 1]
        p += 2
        if kind == 1:
            n = struct.unpack('>I', m[p:p + 4])[0]
            p += 4
            out.append('%s/ (%d)' % (name, n))
        else:
            idx, size, ops = struct.unpack('>IIH', m[p:p + 10])
            p += 10 + 2
            out.append('%s #%d %d bytes ops %02x' % (name, idx, size, ops))
    return '\n'.join(out)


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv))
    except RuntimeError as e:
        print(e)
        sys.exit(1)
