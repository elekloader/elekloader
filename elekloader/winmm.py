# SPDX-License-Identifier: GPL-2.0-or-later
"""A unit's USB MIDI ports through the Windows MIDI API (winmm, through ctypes: no packages), for
`elekloader mods` on Windows.

python-rtmidi (under mido) gives winmm four SysEx buffers of 1 KB, and a longer message does not
arrive: the Data router's read replies carry 8 KB chunks (datarpc.CHUNK), so a read of anything
over one small chunk never got its reply. This port gives winmm sixteen buffers of 64 KB, as
Transfer-sized messages need, and joins a message that spans buffers."""
import collections
import ctypes
import time
from ctypes import wintypes

MHDR_DONE = 0x1
CALLBACK_NULL = 0
NBUF, SIZE = 16, 65536


class MIDIHDR(ctypes.Structure):
    pass


MIDIHDR._fields_ = [('lpData', ctypes.c_void_p), ('dwBufferLength', wintypes.DWORD),
                    ('dwBytesRecorded', wintypes.DWORD), ('dwUser', ctypes.c_void_p),
                    ('dwFlags', wintypes.DWORD), ('lpNext', ctypes.POINTER(MIDIHDR)),
                    ('reserved', ctypes.c_void_p), ('dwOffset', wintypes.DWORD),
                    ('dwReserved', ctypes.c_void_p * 8)]


class _InCaps(ctypes.Structure):
    _fields_ = [('wMid', wintypes.WORD), ('wPid', wintypes.WORD), ('vDriverVersion', wintypes.UINT),
                ('szPname', wintypes.WCHAR * 32), ('dwSupport', wintypes.DWORD)]


class _OutCaps(ctypes.Structure):
    _fields_ = [('wMid', wintypes.WORD), ('wPid', wintypes.WORD), ('vDriverVersion', wintypes.UINT),
                ('szPname', wintypes.WCHAR * 32), ('wTechnology', wintypes.WORD), ('wVoices', wintypes.WORD),
                ('wNotes', wintypes.WORD), ('wChannelMask', wintypes.WORD), ('dwSupport', wintypes.DWORD)]


def _names(mm, count, caps_fn, caps):
    out = []
    for i in range(getattr(mm, count)()):
        c = caps()
        getattr(mm, caps_fn)(i, ctypes.byref(c), ctypes.sizeof(c))
        out.append(c.szPname)
    return out


def names():
    """-> (input names, output names)."""
    mm = ctypes.WinDLL('winmm')
    return (_names(mm, 'midiInGetNumDevs', 'midiInGetDevCapsW', _InCaps),
            _names(mm, 'midiOutGetNumDevs', 'midiOutGetDevCapsW', _OutCaps))


class WinmmPort:
    """send(bytes) and receive(timeout) -> one whole SysEx message (F0 to F7) or None, as
    datarpc.Client wants."""

    def __init__(self, name=None):
        self.mm = mm = ctypes.WinDLL('winmm')
        ins, outs = names()
        want = (name or 'Rytm').lower()
        i = next((k for k, n in enumerate(ins) if want in n.lower()), None)
        o = next((k for k, n in enumerate(outs) if want in n.lower()), None)
        if i is None or o is None:
            raise OSError('no MIDI port named like %r (inputs: %s)' % (name or 'Rytm', ', '.join(ins) or 'none'))
        self.hin, self.hout = wintypes.HANDLE(), wintypes.HANDLE()
        self.bufs = collections.deque()         # in the order winmm fills them
        self.pending, self.msgs = bytearray(), collections.deque()
        if mm.midiInOpen(ctypes.byref(self.hin), i, 0, 0, CALLBACK_NULL):
            raise OSError('could not open MIDI input %r (is another program using it?)' % ins[i])
        try:
            if mm.midiOutOpen(ctypes.byref(self.hout), o, 0, 0, CALLBACK_NULL):
                raise OSError('could not open MIDI output %r' % outs[o])
            for _ in range(NBUF):
                mem = ctypes.create_string_buffer(SIZE)
                h = MIDIHDR(lpData=ctypes.cast(mem, ctypes.c_void_p), dwBufferLength=SIZE)
                mm.midiInPrepareHeader(self.hin, ctypes.byref(h), ctypes.sizeof(h))
                mm.midiInAddBuffer(self.hin, ctypes.byref(h), ctypes.sizeof(h))
                self.bufs.append((h, mem))
            mm.midiInStart(self.hin)
        except BaseException:
            self.close()
            raise
        time.sleep(0.1)     # a message sent within ~50 ms of the start can be lost (loopMIDI, measured)

    def send(self, raw):
        mem = ctypes.create_string_buffer(bytes(raw), len(raw))
        h = MIDIHDR(lpData=ctypes.cast(mem, ctypes.c_void_p), dwBufferLength=len(raw))
        self.mm.midiOutPrepareHeader(self.hout, ctypes.byref(h), ctypes.sizeof(h))
        try:
            if self.mm.midiOutLongMsg(self.hout, ctypes.byref(h), ctypes.sizeof(h)):
                raise OSError('could not send to the MIDI port')
            end = time.monotonic() + 5
            while not h.dwFlags & MHDR_DONE:
                if time.monotonic() > end:
                    raise OSError('the MIDI port did not take a message in 5 s')
                time.sleep(0.001)
        finally:
            self.mm.midiOutUnprepareHeader(self.hout, ctypes.byref(h), ctypes.sizeof(h))

    def _drain(self):
        while self.bufs and self.bufs[0][0].dwFlags & MHDR_DONE:
            h, mem = self.bufs.popleft()
            self.pending += mem.raw[:h.dwBytesRecorded]
            h.dwBytesRecorded = 0
            h.dwFlags &= ~MHDR_DONE
            self.mm.midiInAddBuffer(self.hin, ctypes.byref(h), ctypes.sizeof(h))
            self.bufs.append((h, mem))
        while True:                             # whole messages out of what came
            start = self.pending.find(0xF0)
            if start < 0:
                self.pending.clear()
                return
            end = self.pending.find(0xF7, start)
            if end < 0:
                del self.pending[:start]
                return
            self.msgs.append(bytes(self.pending[start:end + 1]))
            del self.pending[:end + 1]

    def receive(self, timeout):
        end = time.monotonic() + timeout
        while True:
            self._drain()
            if self.msgs:
                return self.msgs.popleft()
            if time.monotonic() >= end:
                return None
            time.sleep(0.002)

    def close(self):
        hin, hout, self.hin, self.hout = self.hin, self.hout, wintypes.HANDLE(), wintypes.HANDLE()
        if hin:
            self.mm.midiInStop(hin)
            self.mm.midiInReset(hin)
            for h, _mem in self.bufs:
                self.mm.midiInUnprepareHeader(hin, ctypes.byref(h), ctypes.sizeof(h))
            self.mm.midiInClose(hin)
        if hout:
            self.mm.midiOutClose(hout)
