# SPDX-License-Identifier: GPL-2.0-or-later
"""What each device's panel adapter offers the SDK tests: pick a machine, find and turn knobs, read
what the firmware stored and what the screen shows. Add a Device subclass here and every test in
test_sdk_alpha_*.py runs on it.

Operations drive the panel or read the firmware's memory; an observation the adapter does not have
yet raises CannotYet. Firmware addresses, screen geometry and the keys stay in this module.
"""
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader import devices                                  # noqa: E402
from elekloader.sdk.sdk_alpha_build import SDK_ALPHA_TARGETS    # noqa: E402


class CannotYet(AssertionError):
    """The adapter cannot drive or observe this on the device yet."""

    def __init__(self, what):
        super().__init__('cannot do this yet: %s' % what)


def c_defines(path):
    """name -> value of a C header's #defines that read as Python: numbers, and macros as functions."""
    found = {}
    for name, args, body in re.findall(r'^#define[ \t]+(\w+)(\([^)]*\))?[ \t]+(.*?)\s*(?:/\*.*)?$',
                                       path.read_text(), re.M):
        try:
            found[name] = eval(('lambda %s: ' % args[1:-1] if args else '') + body, dict(found))
        except Exception:
            pass                    # not a Python expression (a type, a suffixed literal): not needed here
    return found


def mod_defines(mod):
    """name -> number of a mod's `defsym` and `cflags` -D entries (mods/<mod>/mod.json)."""
    doc = json.loads((ROOT / 'mods' / mod / 'mod.json').read_text())
    flags = (f[2:].split('=', 1) for f in doc.get('cflags', ()) if f.startswith('-D') and '=' in f)
    return {**{k: int(v, 0) for k, v in doc.get('defsym', {}).items()}, **{k: int(v, 0) for k, v in flags}}


DIGIEMU = os.environ.get('ELEKLOADER_DIGIEMU', '')
DIGIKIT = os.environ.get('ELEKLOADER_DIGIKIT', '')
DIGI_EMUD = os.environ.get('ELEKLOADER_DIGI_EMUD', '')
DIGI_EMUD_COUPLED = os.environ.get('ELEKLOADER_DIGI_EMUD_COUPLED', '')    # a digi-emud with coupled audio
DN2_EMU_READY = os.environ.get('ELEKLOADER_DN2_EMU_READY', '')            # its ready inputs


class Digiemu:
    """digiemu (unicorn): the Digitakt's and the Digitone's emulator. Its Session is the panel."""
    cwd = DIGIEMU

    @staticmethod
    def check():
        if not (DIGIEMU and os.path.isdir(DIGIEMU)):
            raise unittest.SkipTest('needs ELEKLOADER_DIGIEMU')
        try:
            import unicorn  # noqa: F401
        except ImportError:
            raise unittest.SkipTest("needs digiemu's Python (uv run --project $ELEKLOADER_DIGIEMU)")

    def __init__(self, syx, stock, work):
        sys.path.insert(0, DIGIEMU)
        home = os.path.join(work, 'home')
        subprocess.run([sys.executable, '-m', 'emu.portable', '--home', home, '--add', syx, '--yes'],
                       cwd=DIGIEMU, check=True, stdout=subprocess.DEVNULL)
        from emu.bootstrap import FirmwarePaths
        root = os.path.join(home, 'firmware')
        (slug,) = os.listdir(root)
        with open(os.path.join(root, slug, 'firmware.json')) as fh:
            name = json.load(fh)['release']['syx_name']
        self.paths = FirmwarePaths(os.path.join(root, slug), name, os.path.join(DIGIEMU, 'devices'))
        self.card = self.paths.card                 # a test that saves a project writes to it
        self.pristine = self.card + '.pristine'
        shutil.copyfile(self.card, self.pristine)
        self.card_stamp = os.stat(self.card).st_mtime_ns

    def restore_card(self):
        if os.stat(self.card).st_mtime_ns != self.card_stamp:
            shutil.copyfile(self.pristine, self.card)
            self.card_stamp = os.stat(self.card).st_mtime_ns

    def session(self, audio=False):
        """0.25 s from the saved state."""
        from emu.session import Session
        os.environ.update(self.paths.env())
        return Session(self.paths.gui, self.paths.syx, audio=audio)

    @staticmethod
    def mem(session, addr, size):
        return bytes(session.m.uc.mem_read(addr, size))


DK_MS = 132000                  # DigiKit's ColdFire instructions per emulated millisecond


class DigiKitSession:
    """DigiKit's daemon with the panel a digiemu Session offers the adapters: keys by name, encoders,
    emulated milliseconds, the frames and a halt. Each detent is a short burst: the Digitone II's
    decoder accelerates by event spacing, and one slow event moves a value by a fraction of a step."""

    BURST = 4                   # events a detent, 2 ms apart: about one step of a plain knob
    KEYS = dict(SYN=2, FLTR=3, AMP=4, FX=5, MOD=6, PRESET=7, SETTINGS=8, TEMPO=9, YES=10, UP=11,
                NO=12, LEFT=13, DOWN=14, RIGHT=15, TRK=16, FUNC=17, KEYBOARD=18, RECORD=19, PLAY=20,
                STOP=21, PAGE=22, PTN=23, SONG=24, **{str(n): 24 + n for n in range(1, 17)},
                **{'PUSH ' + c: 41 + i for i, c in enumerate('ABCDEFGH')})

    def __init__(self, emu, capture=None):
        self.emu, self.ms, self.frames, self.revision = emu, 0, [], None
        self.capture, self.record_audio = capture, capture is not None
        self._frame()

    @property
    def pcm(self):
        """What the DSP output since the session started, as digiemu's Session.pcm: 48 kHz stereo
        signed 16-bit LE (DigiKit's capture is 32-bit float)."""
        with open(self.capture, 'rb') as fh:
            data = fh.read()
        floats = struct.unpack('<%df' % (len(data) // 4), data[:len(data) - len(data) % 4])
        return struct.pack('<%dh' % len(floats), *(max(-32768, min(32767, round(x * 32768))) for x in floats))

    def code(self, name):
        return self.KEYS[name]

    @staticmethod
    def png_bytes(frame, scale):
        from tools.control.digi_client import oled_png
        return oled_png(frame, scale)

    def encoder(self, name):
        return 'ABCDEFGH'.index(name) + 1

    def _frame(self):
        revision, frame = self.emu.frame()
        if revision != self.revision:
            self.revision = revision
            self.frames[:] = [(self.ms, frame)]

    def run_ms(self, ms):
        while ms > 0:
            step = min(ms, 500)
            self.emu.step(int(step * DK_MS), timeout_wall_s=300)
            self.ms += step
            ms -= step
            self._frame()

    def press(self, code):
        self.emu.button(code, True)
        self.run_ms(20)

    def release(self, code):
        self.emu.button(code, False)
        self.run_ms(20)

    def tap(self, code, after_ms=150):
        self.emu.tap(code, hold=20 * DK_MS)
        self.run_ms(20 + after_ms)

    def turn(self, encoder, detents, after_ms=30):
        for _ in range(abs(detents) * self.BURST):
            self.emu.turn(encoder, 1 if detents > 0 else -1)
            self.run_ms(2)
        self.run_ms(after_ms)

    @property
    def halted(self):
        status = self.emu.status()
        cf = status['cf']
        if cf.get('error') or cf.get('phase') == 'fault':
            return 'fault: %s at %#x' % (cf.get('error'), cf.get('pc', 0))
        if status.get('heartbeat', {}).get('stalled'):
            return 'the main loop stalled'
        if (status.get('audio') or {}).get('halted'):
            return 'the DSP halted: %s' % status['audio']['halted']
        return None

    def close(self):
        pass                    # the daemon stays for the next test: DigiKit.session restores it


class DigiKit:
    """DigiKit's ColdFire-only daemon (no DSP): the Digitone II's emulator. One daemon a firmware. Its
    boot cache keeps the image's ready state (the main screen), and every firmware.load restores it:
    each test starts there, whatever the last one did. An audio session runs both processors in a
    second daemon, DigiKit's coupled emulator, whose boot cache restores both the same way (the first
    boot takes about a minute), and captures the DSP's output."""
    cwd = None
    STOCK_SHA256 = '7e5b8a733d4ab1fdf7e5d22757f7d8eddaa3cbf71d0858874a791844c02ceb35'   # OS 1.12

    @staticmethod
    def check():
        if not (DIGIKIT and os.path.isdir(DIGIKIT) and DIGI_EMUD and os.path.isfile(DIGI_EMUD)):
            raise unittest.SkipTest('needs ELEKLOADER_DIGIKIT and ELEKLOADER_DIGI_EMUD')

    def __init__(self, syx, stock, work):
        import atexit
        sys.path.insert(0, DIGIKIT)
        from tools.control.digi_client import Emu
        self.syx, self.stock, self.work, self.coupled = syx, stock, work, None
        self.emu = Emu.spawn(binary=DIGI_EMUD, timeout=300, run_dir=os.path.join(work, 'digikit'))
        atexit.register(self.emu.close)
        self._load()
        if not self.emu.status()['cf']['main_ui_reached']:
            raise RuntimeError('the firmware did not reach its main screen')

    def _load(self):
        self.emu.call('firmware.load', syx=self.syx, force=True, auto_no=True, start='auto',
                      base_sha256=self.STOCK_SHA256)

    def restore_card(self):
        pass                    # the ready state has the card as booted: session() puts it back

    def session(self, audio=False):
        if audio:
            return self._audio_session()
        self._load()
        session = DigiKitSession(self.emu)
        session.run_ms(300)     # the first UI-loop passes
        return session

    def _audio_session(self):
        if not (DIGI_EMUD_COUPLED and os.path.isfile(DIGI_EMUD_COUPLED) and DN2_EMU_READY
                and os.path.isdir(DN2_EMU_READY)):
            raise unittest.SkipTest('needs ELEKLOADER_DIGI_EMUD_COUPLED and ELEKLOADER_DN2_EMU_READY')
        if self.coupled is None:
            import atexit
            from tools.control.digi_client import Emu
            self.coupled = Emu.spawn_coupled(self.stock, DN2_EMU_READY, binary=DIGI_EMUD_COUPLED, timeout=300,
                                             run_dir=os.path.join(self.work, 'coupled'))
            atexit.register(self.coupled.close)
        self.coupled.call('firmware.load', syx=self.syx, start='auto', force=True, auto_no=True)
        capture = os.path.join(self.work, 'coupled', 'pcm.f32')
        self.coupled.call('pcm.tap', action='capture', path=capture)
        session = DigiKitSession(self.coupled, capture)
        session.run_ms(300)
        return session

    @staticmethod
    def mem(session, addr, size):
        start = addr & ~3
        words = b''.join(session.emu.peek32(a).to_bytes(4, 'big') for a in range(start, addr + size, 4))
        return words[addr - start:addr - start + size]


class Device:
    """What every device's adapter offers the tests. Its target and the builder's features
    come from SDK_ALPHA_TARGETS; features adds the ones only the adapter has."""
    key = stock_env = None
    backend = Digiemu
    lfo_count = 2
    settings_key = 'GLOBAL'
    features = frozenset()

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        cls.target, = [t for (key, _), t in SDK_ALPHA_TARGETS.items() if key == cls.key]
        cls.features = cls.target.features | cls.features

    @classmethod
    def bases(cls):
        """The mods/ folders of the target's companions, found by their mod.json's id and device."""
        found = {}
        for path in (ROOT / 'mods').glob('*/mod.json'):
            doc = json.loads(path.read_text())
            if doc.get('device') == cls.key and doc.get('id') in cls.target.companions:
                found[doc['id']] = path.parent
        return [found[c] for c in cls.target.companions]

    @classmethod
    def stock(cls):
        """The device's stock file, once it and the cross compiler are there; else a skip."""
        stock = os.environ.get(cls.stock_env, '')
        if not os.path.isfile(stock):
            raise unittest.SkipTest('needs %s' % cls.stock_env)
        profile, = [d for d in devices.devices() if d.key == cls.key]
        gcc = os.environ.get('ELEKLOADER_CROSS', profile.toolchain['prefix']) + 'gcc'
        if not shutil.which(gcc):
            raise unittest.SkipTest('needs the cross compiler %s (ELEKLOADER_CROSS)' % gcc)
        return stock

    def __init__(self, session, symbols, machines, layouts=None):
        self.s, self.symbols = session, symbols
        self.midi_sent = []
        self.machines = sorted(machines)    # the SDK machine ids in this build
        self.layouts = layouts

    def set_lfo_depth(self):
        raise CannotYet('LFO depth')

    def dsp_word(self, addr):
        raise CannotYet('the DSP\'s memory')

    def keyboard(self):
        raise CannotYet('the keyboard')

    def press_key(self, key):
        raise CannotYet('a note key')

    def release_key(self, key):
        raise CannotYet('a note key')

    def held_keys(self, track):
        """How many keys the firmware counts as held on the track."""
        raise CannotYet('the firmware\'s held keys')

    def sounding_voices(self, track):
        """How many voices sound for the track (its machine's, through the companion's envelope)."""
        raise CannotYet('the voices sounding')

    def midi_note(self, track, note, velocity):
        """A note on (velocity 0: its note off) into the MIDI input, on the track's channel."""
        raise CannotYet('MIDI in')

    def audio_position(self):
        if not getattr(self.s, 'record_audio', False):
            raise CannotYet('audio capture (set audio = True on the test class)')
        return len(self.s.pcm)

    def audio_since(self, start, skip_ms=0):
        # digiemu's 48 kHz stereo signed-16 PCM, left channel.
        pcm = bytes(self.s.pcm[start + 4 * 48 * skip_ms:])
        return [int.from_bytes(pcm[i:i + 2], 'little', signed=True)
                for i in range(0, len(pcm) - 3, 4)]

    def tap(self, name, after_ms=300):
        self.s.tap(self.s.code(name), after_ms=after_ms)

    def midi(self, data, after_ms=50):
        """MIDI bytes into the device's MIDI input, as digiemu's window sends them: its port, an event
        source of the session, puts them in the input's ring a moment later."""
        if self.backend is not Digiemu:
            raise CannotYet('MIDI in')
        port = getattr(self.s, 'sdk_midi_in', None)
        if port is None:
            self.midi_input()
            from emu import midi
            port = self.s.sdk_midi_in = midi.MidiIn()
            port.attach(self.s.m, self.s.ips)
            out = self.s.sdk_midi_out = midi.MidiOut(self.midi_sent.append)     # what the device sends
            out.install(self.s.m)
            out.attach(self.s.m, self.s.ips)
            self.s.sources = tuple(self.s.sources) + (port, out)
        sent = port.delivered + len(data)
        # The port maps host time to emulated time; a busy host slows the emulator below real time and
        # the mapping drifts ahead, so start it again: the bytes go in about 15 emulated ms from now.
        port.set_ips(self.s.ips)
        port.put(bytes(data))
        if not self.wait_until(lambda: port.delivered >= sent, limit_ms=1000):
            raise AssertionError('the MIDI input never took %s' % bytes(data).hex())
        self.wait(after_ms)

    def midi_input(self):
        """Let MIDI IN's bytes in (MIDI CONFIG > PORT CONFIG > INPUT FROM), where the setup leaves them out."""

    def knob_cc(self, knob):
        raise CannotYet('a CC for a knob')

    def cc(self, channel, number, value, after_ms=50):
        """A MIDI CC on `channel` (1..16)."""
        self.midi((0xB0 | (channel - 1), number, value), after_ms)

    def note(self, channel, note, velocity, after_ms=50):
        """A MIDI note on (velocity 0: off) on `channel` (1..16)."""
        self.midi((0x90 | (channel - 1), note, velocity), after_ms)

    def wait(self, ms):
        self.s.run_ms(ms)

    def wait_until(self, ready, limit_ms=6000):
        """Wait for an observed UI state; False if the firmware halts or the deadline expires."""
        deadline = self.s.ms + limit_ms
        while not ready():
            if self.s.halted is not None or self.s.ms >= deadline:
                return False
            self.s.run_ms(min(50, deadline - self.s.ms))
        return True

    @property
    def halted(self):
        return self.s.halted

    def screen(self):
        return self.s.frames[-1][1]

    def turn_now(self, encoder, detents, after_ms):
        self.s.turn(self.s.encoder(encoder), detents, after_ms=after_ms)

    def turn(self, encoder, detents, after_ms=30):
        """Turn one detent at a time: a burst is swallowed by the encoder's dead zone."""
        for _ in range(abs(detents)):
            self.s.turn(self.s.encoder(encoder), 1 if detents > 0 else -1, after_ms=after_ms)
        self.settle()

    def settle(self, quiet_ms=250, limit_ms=2000):
        """Run until the screen has not changed for quiet_ms: the firmware has caught up with the
        input, and .present has seen it."""
        start = self.s.ms
        while self.s.ms - start < limit_ms:
            self.s.run_ms(50)
            if not self.s.frames or self.s.ms - self.s.frames[-1][0] >= quiet_ms:
                return

    def active_track(self):
        return self.read('fw_active_track')

    def mem(self, addr, size):
        return self.backend.mem(self.s, addr, size)

    def peek32(self, addr):
        return struct.unpack('>I', self.mem(addr, 4))[0]

    def read(self, name, offset=0):
        return self.peek32(int(self.symbols[name], 16) + offset)

    def crop(self, x0, y0, width, height):
        frame = self.s.frames[-1][1]
        return [sum(((frame[7 - y // 8 + 8 * x] >> (y % 8)) & 1) << (width - 1 - x + x0)
                    for x in range(x0, x0 + width)) for y in range(y0, y0 + height)]

    def control_pixels(self, encoder):
        """The graphic and label in the SDK's four-column grid."""
        return self.crop(20 + 26 * (encoder % 4), 13 + 27 * (encoder // 4), 26, 26)

    def glyph_pixels(self, encoder=0):
        return tuple(self.crop(25 + 26 * (encoder % 4), 13 + 27 * (encoder // 4), 16, 16))

    def value_pixels(self, encoder=0):
        x = 18 + 26 * (encoder % 4)
        return self.crop(x, 30 + 27 * (encoder // 4), min(33, 128 - x), 9)

    def algorithm_pixels(self):
        return self.crop(18, 13, 6, 16)

    def open_settings(self):
        self.tap(self.settings_key)

    def close_settings(self):
        self.tap('NO')

    def settings_pixels(self):
        return self.crop(40, 0, 88, 64)

    def kit(self):
        return self.peek32(int(self.symbols['fw_kit'], 16))

    def chord(self, held, key):
        self.s.press(self.s.code(held))
        self.s.run_ms(60)
        self.s.tap(self.s.code(key))
        self.s.release(self.s.code(held))
        self.s.run_ms(300)

    def _top(self, presses=8):
        for _ in range(presses):
            self.tap('UP', after_ms=60)

    def _project_menu(self):
        """SETTINGS > PROJECT, at its first entry (the menus remember where they were). The
        Digitakt's and the Digitone's menus are the same: GLOBAL opens SETTINGS on both."""
        self.tap('GLOBAL')
        self._top()
        self.tap('YES')
        self._top()

    def _leave_settings(self):
        """Back out of the dialog and the menus to the main page (NO on the main page does nothing)."""
        for _ in range(6):
            self.tap('NO', after_ms=400)

    def _slot(self, slot):
        self._top(12)
        for _ in range(slot - 1):
            self.tap('DOWN')

    def save_project(self, slot):
        """SAVE PROJECT AS the slot (2 to 128; 1 is the write-protected presets)."""
        self._project_menu()
        self.tap('DOWN')
        self.tap('YES')
        self._slot(slot)
        self.tap('YES')                 # the name editor
        self.tap('YES')                 # save
        self.settle(quiet_ms=5000, limit_ms=120000)
        self._leave_settings()

    def load_project(self, slot):
        """LOAD PROJECT from the slot, answering the question: any changes are lost."""
        self._project_menu()
        self.tap('YES')
        self._slot(slot)
        self.tap('YES')
        self.tap('YES')
        self.settle(quiet_ms=20000, limit_ms=300000)    # the progress bar stands still for a while
        self._leave_settings()

    def select(self, track, machine_id):
        """Pick machine_id (0: the stock machine) for track through the device's own menu; False if
        no entry gives it."""
        for entry in range(1, 17):
            self.pick(track, entry)
            if self.machine(track) == machine_id:
                return True
        return False


DT1 = c_defines(ROOT / 'elekloader/sdk/include/digitakt-mk1/core3.h')   # FW_SOUND, FW_SOUND_MACHINE, ...
DT1_SLOT_A = 17                     # FW_SOUND_PARAM(k) is slot 17 + k: core3.h says so only in a comment
DT1_SLOTS = DT1['FW_SOUND_PARAM'](-DT1_SLOT_A)                          # a sound's slot s at + DT1_SLOTS + 2s
DT1_EXTRA_SLOT = c_defines(ROOT / 'mods/machine-pages/src.h')['MP_EXTRA_SLOT']


class Digitakt1(Device):
    key, stock_env = 'digitakt-mk1', 'ELEKLOADER_STOCK_154'
    track_count = 8

    features = frozenset(('knob_ccs_past_h',))
    sound_without_render = False
    KEYS = {0: '9', 1: '2', 2: '10', 3: '3', 4: '11', 5: '12', 6: '5', 7: '13',
            8: '6', 9: '14', 10: '7', 11: '15', 12: '16'}

    def keyboard(self):
        if not getattr(self, '_chromatic', False):
            self.chord('FUNC', 'TRK')
            self._chromatic = True

    def press_key(self, key):
        self.s.press(self.s.code(self.KEYS[key]))

    def release_key(self, key):
        self.s.release(self.s.code(self.KEYS[key]))

    HELD = 0x4219cc98               # OS 1.54's count of the keys held on each track (core's NOTE_ON adds one)

    def held_keys(self, track):
        return self.peek32(self.HELD + 4 * track)

    STOCK = (0, 1, 2, 3)            # the stock machines, first in the machine list
    PAGE_LEDS = {'SRC': 19, 'LFO': 22}

    def page_visible(self, page):
        from emu import panelleds
        leds = panelleds.seed(self.s.m.uc.mem_read, '1.54', 'Digitakt')
        if leds is None:
            raise CannotYet('page indicators could not be observed')
        red, green, blue = leds.colours()[self.PAGE_LEDS[page]]
        return red > max(green, blue)

    def open_page(self):
        """Show the machine's page (the one the track showed last); True when it is the SDK's."""
        self.tap('FLTR')            # SRC again flips the page; from another page it shows the track's
        self.tap('SRC')
        return self.page_visible('SRC') and self.machine(self.active_track()) in self.machines

    def next_page(self):
        self.tap('SRC')             # SRC again flips an SDK machine's two pages

    def shown_page(self, track):
        """The SDK page machine-pages shows for the track (its `selected` array)."""
        return bytes(self.s.m.uc.mem_read(int(self.symbols['selected'], 16) + track, 1))[0]

    def empty_positions(self):
        """Which of the eight positions of the page shown hold a knob: machine-pages' layout of it
        (pages.c's page_now, struct page: lay[2][11] at +20) for its selected page, not the screen."""
        page = self.read('page_now')
        if not page:
            raise CannotYet('machine-pages shows no page of its own')
        lay = page + 20 + 44 * self.shown_page(self.active_track())
        return tuple(bool(self.peek32(lay + 4 * (2 + k))) for k in range(8))

    def switch_track(self, track):
        self.chord('TRK', str(track + 1))

    def select(self, track, machine_id):
        # The list is the stock machines, then the build's by id; it opens at the track's own.
        rows = list(self.STOCK) + self.machines
        if self.active_track() != track:
            self.switch_track(track)
        current = self.machine(track)
        if machine_id not in rows or current not in rows:
            return False
        delta = rows.index(machine_id) - rows.index(current)
        self.chord('FUNC', 'SRC')
        for _ in range(abs(delta)):
            self.tap('DOWN' if delta > 0 else 'UP')
        self.tap('YES')
        return self.machine(track) == machine_id

    def machine(self, track):
        return bytes(self.s.m.uc.mem_read(DT1['FW_SOUND'](self.kit(), track) + DT1['FW_SOUND_MACHINE'], 1))[0]

    def raw_value(self, knob, track=0):
        slot = self.destination_for(knob)
        at = DT1['FW_SOUND'](self.kit(), track) + DT1_SLOTS + 2 * slot
        return struct.unpack('>h', bytes(self.s.m.uc.mem_read(at, 2)))[0]

    def destination_for(self, knob):
        return DT1_SLOT_A + knob if knob < 8 else DT1_EXTRA_SLOT(knob - 8)

    def knob_cc(self, knob):
        """The CC that sets the knob on its track: SRC A-H's, 16-23, then machine-pages' 24-30."""
        return 16 + knob

    def destination(self, lfo, track=0):
        at = DT1['FW_SOUND'](self.kit(), track) + DT1_SLOTS + 2 * (4 + 8 * lfo)
        return struct.unpack('>H', bytes(self.s.m.uc.mem_read(at, 2)))[0] >> 8

    def open_lfo(self, lfo):
        self.tap('NO')
        self.tap('SRC')
        self.tap('LFO')
        if getattr(self, '_lfo', 0) != lfo:
            self.tap('LFO')
        self._lfo = lfo
        self.tap('D')               # encoder D push opens the destination list
        return self.page_visible('LFO')

    def choose_destination(self, lfo, knob):
        target = self.destination_for(knob)
        # The selector orders parameter IDs, not sound slots; keep one direction through stock groups.
        step = 1 if self.destination(lfo) < target else -1
        for _ in range(600):
            if self.destination(lfo) == target:
                return True
            self.turn('D', step)
        return self.destination(lfo) == target

    def destination_knob(self, lfo):
        slot = self.destination(lfo)
        return next((k for k in range(15) if self.destination_for(k) == slot), None)

    def confirm_destination(self):
        self.tap('YES')

    def label(self, knob):
        """A logical knob's label: the adapter's copy of what .present gave, not the screen."""
        at = int(self.symbols['sdk_alpha_labels'], 16) + 6 * knob   # [15][ELEK_UI_LABEL_MAX + 1]
        return bytes(self.s.m.uc.mem_read(at, 6)).split(b'\0')[0].decode('ascii')

    def lfo_label(self, knob):
        return self.label(knob)

    def visible(self, knob):
        return bool(bytes(self.s.m.uc.mem_read(int(self.symbols['sdk_alpha_visibility'], 16) + knob, 1))[0])

    def page_action(self, action):
        """Copy, clear or paste the page shown: SRC held with RECORD, PLAY or STOP."""
        self.chord('SRC', {'copy': 'RECORD', 'clear': 'PLAY', 'paste': 'STOP'}[action])


class Digitone1(Device):
    key, stock_env = 'digitone-mk1', 'ELEKLOADER_STOCK_DN1_144'
    track_count = 4
    features = frozenset(('polyphony', 'audio_modulation', 'midi_in'))
    sound_without_render = True
    KEYS = Digitakt1.KEYS

    def set_lfo_depth(self):
        self.turn('H', 64)

    def midi_input(self):
        """INPUT FROM is USB after the setup: SETTINGS > MIDI CONFIG > PORT CONFIG, its fourth row, to
        MIDI+USB (the last choice: RIGHT stops there)."""
        self.tap('GLOBAL')
        self._top()
        for key in ('DOWN',) * 4 + ('YES',):            # MIDI CONFIG
            self.tap(key)
        self._top()
        self.tap('DOWN')
        self.tap('YES')                                 # PORT CONFIG
        self._top(12)
        for key in ('DOWN',) * 3 + ('RIGHT',) * 3:
            self.tap(key)
        self._leave_settings()

    def knob_cc(self, knob):
        """The CC that sets the knob on its track: knob 0 is SYN2's A, CC 75."""
        if knob:
            raise CannotYet('a CC for a knob past the first')
        return 75

    def keyboard(self):
        pass                        # DN1's trig keys already play its chromatic keyboard.

    def press_key(self, key):
        self.s.press(self.s.code(self.KEYS[key]))

    def release_key(self, key):
        self.s.release(self.s.code(self.KEYS[key]))

    HELD = 0x41994e80               # OS 1.44's count of the keys held on each track (core's NOTE_ON adds one)

    def held_keys(self, track):
        return self.peek32(self.HELD + 4 * track)

    def sounding_voices(self, track):
        """The voices playing a machine (slot 0 of fw_voice_params) whose envelope in machines-dn1's vs
        (struct dnm_voice, then its stage and level) is not over, on the track."""
        at, params = int(self.symbols['machines:vs'], 16), int(self.symbols['fw_voice_params'], 16)
        machine = lambda v: struct.unpack('>h', bytes(self.s.m.uc.mem_read(params + 18 + 158 * v, 2)))[0]
        return sum(machine(v) > 0 and self.peek32(at + 32 * v + 12) == track and
                   bool(self.peek32(at + 32 * v + 24) or self.peek32(at + 32 * v + 28)) for v in range(8))

    MIDI_IN_FROM = 0x41368d64       # OS 1.44's MIDI CONFIG > INPUT FROM: bit 0 DIN, bit 1 USB
    MIDI_NOTES = 0x41368d88         # and RECEIVE NOTES

    def midi_note(self, track, note, velocity):
        from emu import midi
        from unicorn.m68k_const import UC_M68K_REG_PC
        m = self.s.m
        m.uc.mem_write(self.MIDI_IN_FROM, struct.pack('>I', self.peek32(self.MIDI_IN_FROM) | 1))
        m.uc.mem_write(self.MIDI_NOTES, struct.pack('>I', 1))
        channel = self.read('fw_midi_chan', 4 * track)
        midi.feed(m, bytes((0x90 | channel, note, velocity)))
        for _ in range(100):            # vector 156 drains the ring, once the CPU's level lets it
            if midi._take(m, 156):
                self.s.pc = m.uc.reg_read(UC_M68K_REG_PC)     # the session resumes at its own pc
                return
            self.s.run_ms(1)
        raise AssertionError('MIDI in: vector 156 was never taken')

    SDK_PAGE, SYN2_FIRST = 29, 4    # machines-dn1's first page, and the first page of SYN2's stock list
    # machines-dn1's sdk_pos: the SYN parameter that holds each logical knob, whichever page shows it
    SDK_PARAMS = (119, 120, 121, 122, 123, 124, 125, 126, 112, 113, 114, 127, 130, 117, 118)

    def open_page(self):
        """Show the machine's page; True when SYN2 shows machines-dn1's SDK pages."""
        # SYN2 again steps to its next page; from another page it opens on the first.
        self.s.tap(self.s.code('SYN1'))
        self.s.tap(self.s.code('SYN2'))
        self.s.run_ms(100)
        return (self.syn2_pages() or [None])[0] == self.SDK_PAGE

    def next_page(self):
        self.tap('SYN2')            # SYN2 again steps to its next page

    def shown_page(self, track):
        """The SDK page machines-dn1 keeps for the track (dnm_sdk_shown)."""
        return self.read('dnm_sdk_shown', 4 * track)

    def empty_positions(self):
        """machines-dn1's page table (dnm_sdk_pages), not the screen."""
        return tuple(bool(self.read('dnm_sdk_pages', 16 + 4 * k)) for k in range(8))

    def switch_track(self, track):
        self.tap('T%d' % (track + 1))

    def entries(self):
        """MACHINES' list: FM (0), then machines-dn1's table, then the SDK machines' table."""
        ids = [0]
        for table in ('dn_machines', 'sdk_alpha_machines'):
            at = int(self.symbols[table], 16)
            while self.peek32(at):
                ids.append(self.peek32(self.peek32(at)))     # each record starts with its id
                at += 4
        return ids

    def select(self, track, machine_id):
        entries = self.entries()
        if machine_id not in entries:
            return False
        self.pick(track, entries.index(machine_id) + 1)
        for _ in range(20):                 # the sound changes a frame or two after the list closes
            if self.machine(track) == machine_id:
                return True
            self.s.run_ms(50)
        return False

    def syn2_pages(self):
        """SYN2's list of pages now, from the view core-dn1 notes (core_views)."""
        views = int(self.symbols['core_views'], 16)
        for i in range(16):
            view, first = self.peek32(views + 8 * i), self.peek32(views + 8 * i + 4)
            if view and first == self.SYN2_FIRST:
                begin, end = self.peek32(view + 124), self.peek32(view + 128)
                return [self.peek32(a) for a in range(begin, end, 4)]
        return None

    def pick(self, track, entry):
        # Holding a track key opens core-dn1's Mod Menu; MACHINES is machines-dn1's only entry.
        # In its list trig key N picks entry N: 1 is FM, then the machines.
        t = self.s.code('T%d' % (track + 1))
        self.s.press(t)
        self.s.run_ms(700)
        self.s.release(t)
        self.s.run_ms(300)
        self.s.tap(self.s.code('YES'))
        self.s.tap(self.s.code(str(entry)))

    def machine(self, track):
        return self.peek32(self.kit() + 326 * track + 0x2c) >> 24   # sound slot 0 = id << 8

    def raw_value(self, knob, track=0):
        slot = self.destination_for(knob, track)
        at = self.kit() + 326 * track + 0x2c + 2 * slot
        return struct.unpack('>h', bytes(self.s.m.uc.mem_read(at, 2)))[0]

    def destination_for(self, knob, track=0):
        return self.peek32(int(self.symbols['fw_params'], 16) + 60 * self.parameter_for(knob, track) + 4)

    def parameter_for(self, knob, track=0):
        return self.SDK_PARAMS[knob]

    def open_lfo(self, lfo):
        if not hasattr(self, '_lfo_labels'):
            # OS 1.44's native list formatter returns the text its row renderer draws.
            from unicorn import UC_HOOK_CODE
            from unicorn.m68k_const import UC_M68K_REG_A7, UC_M68K_REG_D2, UC_M68K_REG_D3

            def named(uc, address, size, data):
                if address == 0x400c6a3c:
                    param = uc.reg_read(UC_M68K_REG_D2)
                    string = self.peek32(uc.reg_read(UC_M68K_REG_D3))
                    self._lfo_labels[param] = bytes(uc.mem_read(string, 64)).split(b'\0')[0].decode('ascii')
                else:           # the group and name arguments passed to the native DEST box renderer
                    string = self.peek32(uc.reg_read(UC_M68K_REG_A7) + 32)
                    self._lfo_box[address] = bytes(uc.mem_read(string, 64)).split(b'\0')[0].decode('ascii')

            self._lfo_box = {}
            # OS 1.44's list formatter and DEST box renderer: only this probe hooks them, so no profile has them
            for address in (0x400c6a3c, 0x40089bb0, 0x40089bf0):
                self.s.m.uc.hook_add(UC_HOOK_CODE, named, begin=address, end=address)
            self.s.m.uc.ctl_remove_cache(0x400c6918, 0x400c6a48)
            self.s.m.uc.ctl_remove_cache(0x40089aa2, 0x40089c0a)
        self._lfo_labels = {}
        self._lfo = lfo
        self.tap('SYN1')
        self.tap('LFO')
        view = next(self.read('core_views', 8 * i) for i in range(16)
                    if self.read('core_views', 8 * i + 4) == 10)
        if self.peek32(view + 144) != lfo:
            self.tap('LFO')
        self.tap('D')
        return self.wait_until(lambda: self.peek32(view + 144) == lfo and bool(self._lfo_labels))

    def destination(self, lfo, track=0):
        at = self.kit() + 326 * track + 0x2c + 2 * (4 + 8 * lfo)
        return struct.unpack('>H', bytes(self.s.m.uc.mem_read(at, 2)))[0] >> 8

    def choose_destination(self, lfo, knob):
        target = self.destination_for(knob, self.active_track())
        # Native sound parameters, excluding the alternate multiplier records (57 and 66).
        params = list(range(24, 57)) + list(range(58, 66)) + list(range(107, 142))
        base = int(self.symbols['fw_params'], 16)
        order = {self.peek32(base + 60 * i + 4): (self.peek32(base + 60 * i), i) for i in params}
        order[0] = (-1, 0)         # NONE precedes every parameter group
        current = self.destination(lfo, self.active_track())
        step = 1 if order[current] < order[target] else -1
        # The guess can be wrong (the list puts the ratio offsets after the delays): then the other way.
        for step, limit in ((step, 600), (-step, 1200)):
            for _ in range(limit):
                if self.destination(lfo, self.active_track()) == target:
                    self.settle()
                    return True
                self.turn_now('D', step, after_ms=30)
        return False

    def destination_knob(self, lfo):
        slot = self.destination(lfo, self.active_track())
        layout = self.layouts[self.machine(self.active_track())]
        return next((spec['id'] for page in layout['pages'] for spec in page['knobs'].values()
                     if self.destination_for(spec['id'], self.active_track()) == slot), None)

    def confirm_destination(self):
        knob = self.destination_knob(self._lfo)
        param = self.parameter_for(knob, self.active_track())
        group, label = self._lfo_labels[param].split(':', 1)
        self._lfo_box = {}
        self.tap('YES')
        expected = {0x40089bb0: group.upper(), 0x40089bf0: label}
        if not self.wait_until(lambda: self._lfo_box == expected):
            raise AssertionError('the native DEST box shows %r, expected %r' % (self._lfo_box, expected))

    def lfo_label(self, knob):
        param = self.parameter_for(knob, self.active_track())
        if not self.wait_until(lambda: param in self._lfo_labels):
            raise CannotYet('the native destination list did not name parameter %d' % param)
        return self._lfo_labels[param].split(':', 1)[-1]

    def label(self, knob):
        """The adapter's copy (sdk_alpha_labels, which dev.h renames for this read), not the screen."""
        at = int(self.symbols['dnm_sdk_labels'], 16) + 6 * knob
        return bytes(self.s.m.uc.mem_read(at, 6)).split(b'\0')[0].decode('ascii')

    def visible(self, knob):
        at = int(self.symbols['dnm_sdk_visibility'], 16) + knob
        return bool(bytes(self.s.m.uc.mem_read(at, 1))[0])

    def page_action(self, action):
        """Copy, clear or paste the page shown: its key held with RECORD, PLAY or STOP."""
        self.chord('SYN2', {'copy': 'RECORD', 'clear': 'PLAY', 'paste': 'STOP'}[action])


class Digitone2(Device):
    """The Digitone II OS 1.12 in DigiKit. SDK machines are synths in MACHINE SEL (machines-dn2's
    sdk_alpha.c) whose pages are SYN's; logical knob i is sound slot 25 + i."""
    key, stock_env = 'digitone-mk2', 'ELEKLOADER_DN2_SYX'
    backend = DigiKit
    track_count = 16
    lfo_count = 3
    settings_key = 'SETTINGS'
    features = frozenset(('render', 'polyphony', 'audio_modulation', 'trk_setup'))
    sound_without_render = True     # FM TONE plays an SDK machine without a render
    KEYS = Digitakt1.KEYS           # KEYBOARD's chromatic keys, from the octave's C

    def keyboard(self):
        if not getattr(self, '_keyboard', False):
            self.tap('KEYBOARD')
            self._keyboard = True

    def press_key(self, key):
        self.s.press(self.s.code(self.KEYS[key]))

    def release_key(self, key):
        self.s.release(self.s.code(self.KEYS[key]))

    def set_lfo_depth(self):
        self.turn('H', 64)

    def knob_cc(self, knob):
        """The CC that sets the knob on its track: knob 0 is SYN's A, CC 40 (FM TONE's)."""
        if knob:
            raise CannotYet('a CC for a knob past the first')
        return 40

    def midi(self, data, after_ms=50):
        """MIDI bytes into the firmware's USB MIDI receive callback (DigiKit's midi.usb.in), which INPUT
        FROM's setup (USB) lets in."""
        self.s.emu.call('midi.usb.in', bytes=bytes(data).hex())
        self.wait(after_ms)

    def dsp_word(self, addr):
        """A word of the DSP's memory (little-endian), in an audio session."""
        return struct.unpack('<I', bytes.fromhex(self.s.emu.call('dsp.peek', addr=addr, len=4)['hex']))[0]

    MOD, H = mod_defines('machines-dn2'), c_defines(ROOT / 'mods/machines-dn2/dn2.h')
    ACTIVE_TRACK = MOD['ACTIVE_TRACK']  # a byte: the track the panel edits (checked on a unit with DigiHealth)
    SOUNDS, SOUND_SIZE = MOD['SOUNDS'], MOD['SOUND_SIZE']  # the machine at +0xde, slot s at +20 + 2s
    SLOT = H['SLOT']

    def active_track(self):
        return self.mem(self.ACTIVE_TRACK, 1)[0]

    def sound(self, track):
        return self.SOUNDS + self.SOUND_SIZE * track

    def machine(self, track):
        return self.mem(self.sound(track) + 0xde, 1)[0]

    def raw_value(self, knob, track=0):
        return struct.unpack('>h', self.mem(self.sound(track) + 20 + 2 * (self.SLOT + knob), 2))[0]

    def switch_track(self, track):
        self.chord('TRK', str(track + 1))

    HELD = 0x445e3364               # OS 1.12's count of each track's keys held, by note (note-on 0x40121db2 adds one)
    VOICES = 0x40288306             # OS 1.12's voice slots: slot v's track, a signed byte at + 0x14 v (-1: none)

    def held_keys(self, track):
        return sum(struct.unpack('>128I', self.mem(self.HELD + 512 * track, 512)))

    def sounding_voices(self, track):
        """The voice slots the OS has given the track."""
        return sum(struct.unpack('b', self.mem(self.VOICES + 0x14 * v, 1))[0] == track for v in range(16))

    def trk_setup(self, track):
        """The track's TRK SETUP PLAY MODE (0 POLY, 1 POLY M.LFO, 2 MONO, 3 MONO LEG.) and MONO NOTE PRIO
        (0 LAST, 1 LOW, 2 HIGH): big-endian words at + 0x146 and + 0x14a of its sound."""
        return struct.unpack('>ii', self.mem(self.sound(track) + 0x146, 8))

    def step_play_mode(self):
        """FUNC+FLTR's SETUP, its sixth row (the active track's PLAY MODE), one step right, then closed."""
        self.chord('FUNC', 'FLTR')
        for _ in range(5):
            self.tap('DOWN')
        self.tap('RIGHT')
        self.tap('NO')
        self.tap('NO')

    def entries(self):
        """MACHINE SEL's synths, in its order (machines-dn2's dn2_list)."""
        count = self.read('dn2_list_bytes') // 4
        return [self.read('dn2_list', 4 * i) for i in range(count)]

    def select(self, track, machine_id):
        if self.active_track() != track:
            self.switch_track(track)
        rows, current = self.entries(), self.machine(track)
        if machine_id not in rows or current not in rows:
            return False
        delta = rows.index(machine_id) - rows.index(current)
        self.chord('FUNC', 'SYN')           # MACHINE SEL opens at the track's own machine
        for _ in range(abs(delta)):
            self.tap('DOWN' if delta > 0 else 'UP', after_ms=60)
        self.tap('YES')
        self.tap('NO')
        return self.machine(track) == machine_id

    def open_page(self):
        """Show SYN at the track's page; True when the track plays an SDK machine."""
        self.tap('FLTR')            # SYN again steps to its next page; from another page it shows the track's
        self.tap('SYN')
        return self.machine(self.active_track()) in self.machines

    def next_page(self):
        self.tap('SYN')

    def shown_page(self, track):
        """Read the active native view; inactive tracks keep their page in the SDK."""
        if track == self.active_track():
            view = self.read('dn2_syn_view')
            if not view:
                raise CannotYet('the native SYN view has not been observed')
            return self.peek32(view + 144)
        return self.read('dn2_sdk_shown', 4 * track)

    def empty_positions(self):
        """Which of the eight positions of the page SYN shows hold a parameter: machines-dn2's
        dn2_sdk_ids, not the screen."""
        return tuple(bool(self.read('dn2_sdk_ids', 4 * k)) for k in range(8))

    def label(self, knob):
        """The adapter's copy (sdk_alpha_labels, which dev.h renames for this read), not the screen."""
        return self.mem(int(self.symbols['dn2_sdk_labels'], 16) + 6 * knob, 6).split(b'\0')[0].decode('ascii')

    def visible(self, knob):
        return bool(self.mem(int(self.symbols['dn2_sdk_visibility'], 16) + knob, 1)[0])

    def page_action(self, action):
        """Copy, clear or paste the page shown: SYN held with RECORD, PLAY or STOP."""
        self.chord('SYN', {'copy': 'RECORD', 'clear': 'PLAY', 'paste': 'STOP'}[action])

    def _project_menu(self):
        """SETTINGS > PROJECT, at its first entry: SETTINGS is the Digitakt's GLOBAL here."""
        self.tap('SETTINGS')
        self._top()
        self.tap('YES')
        self._top()

    def load_project(self, slot):
        """LOAD PROJECT lists the saved projects only, then CREATE NEW. Each test starts from a card with
        none and saves one, so it is the first row whatever the slot. YES to "any changes will be lost"."""
        self._project_menu()
        self.tap('YES')
        self._top(12)
        self.tap('YES')
        self.tap('YES')
        self.settle(quiet_ms=20000, limit_ms=300000)
        self._leave_settings()

    MOD_PAGE = 0x447c0890           # the LFO MOD shows (0-2): the MOD view's page; only this test reads it
    ROW, ROW_NAME = 60, 40          # machines.c's struct row (dn2_t1): its size, its name; a layout, no #define
    ROWS, SDK_ROW = H['ROWS'], H['SDK_ROW']     # knob i's row is ROWS + SDK_ROW + i

    def open_lfo(self, lfo):
        """MOD at the LFO's page, its DEST list open (PUSH D)."""
        self.tap('FLTR')
        self.tap('MOD')             # MOD again steps to the next LFO
        for _ in range(3):
            if self.peek32(self.MOD_PAGE) == lfo:
                break
            self.tap('MOD')
        self.tap('PUSH D', after_ms=300)
        return self.peek32(self.MOD_PAGE) == lfo

    def destination(self, lfo, track=None):
        track = self.active_track() if track is None else track
        return self.mem(self.sound(track) + 20 + 2 * (4 + 8 * lfo), 1)[0]

    def choose_destination(self, lfo, knob):
        """Step the list (UP, DOWN: each step is the LFO's destination) until it is the knob's slot.
        From NONE the SDK knobs come first; from anywhere else the other way may be shorter."""
        target = self.SLOT + knob
        for key, limit in (('DOWN', 40), ('UP', 80)):
            for _ in range(limit):
                if self.destination(lfo) == target:
                    self.settle()
                    return True
                self.tap(key, after_ms=60)
        return self.destination(lfo) == target

    def destination_knob(self, lfo):
        knob = self.destination(lfo) - self.SLOT
        return knob if 0 <= knob < 15 else None

    def confirm_destination(self):
        self.tap('YES', after_ms=300)

    def lfo_label(self, knob):
        """The name the DEST list draws for the knob: its row's (the list draws the rows' names)."""
        row = int(self.symbols['dn2_t1'], 16) + self.ROW * (self.ROWS + self.SDK_ROW + knob)
        return self.mem(self.peek32(row + self.ROW_NAME), 6).split(b'\0')[0].decode('ascii')
