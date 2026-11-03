# SPDX-License-Identifier: GPL-2.0-or-later
"""What each device's panel adapter offers the SDK tests: pick a machine, find and turn knobs, read
what the firmware stored and what the screen shows. Add a Device subclass here and every test in
test_sdk_alpha_*.py runs on it.

Operations drive the panel or read the firmware's memory; an observation the adapter does not have
yet raises CannotYet; known gaps are marked as expected failures in the tests. Firmware addresses, screen
geometry and the keys stay in this module.
"""
import os
import struct


class CannotYet(AssertionError):
    """The adapter cannot drive or observe this on the device yet."""

    def __init__(self, what):
        super().__init__('cannot do this yet: %s' % what)


DIGIEMU = os.environ.get('ELEKLOADER_DIGIEMU', '')


class Device:
    """What every device's adapter offers the tests."""
    key = stock_env = None
    bases = ()
    machine_claim = None

    @classmethod
    def machine_ids(cls, mod):
        return [int(n.split(':')[1]) for n in mod.names if n.split(':')[0] == cls.machine_claim]

    def __init__(self, session, symbols, machines, layouts=None):
        self.s, self.symbols = session, symbols
        self.machines = sorted(machines)    # the SDK machine ids in this build
        self.layouts = layouts

    def tap(self, name, after_ms=300):
        self.s.tap(self.s.code(name), after_ms=after_ms)

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

    def turn_until(self, encoder, step, done, limit=160):
        """Turn by step until done() holds; False if it never does."""
        for _ in range(limit):
            if done():
                return True
            self.turn(encoder, step)
        return done()

    def active_track(self):
        return self.read('fw_active_track')

    def peek32(self, addr):
        return struct.unpack('>I', bytes(self.s.m.uc.mem_read(addr, 4)))[0]

    def read(self, name, offset=0):
        return self.peek32(int(self.symbols[name], 16) + offset)

    def crop(self, x0, y0, width, height):
        frame = self.s.frames[-1][1]
        return [sum(((frame[7 - y // 8 + 8 * x] >> (y % 8)) & 1) << (width - 1 - x + x0)
                    for x in range(x0, x0 + width)) for y in range(y0, y0 + height)]

    def control_pixels(self, encoder):
        """The graphic and label in the SDK's four-column grid."""
        return self.crop(20 + 26 * (encoder % 4), 13 + 27 * (encoder // 4), 26, 26)

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

    # The keyboard on the trig keys: the bottom row is C D E F G A B C, the top row has the black keys.
    KEYS = {0: '9', 1: '2', 2: '10', 3: '3', 4: '11', 5: '12', 6: '5', 7: '13', 8: '6', 9: '14',
            10: '7', 11: '15', 12: '16'}

    def keyboard(self):
        """Make the trig keys play the active track's notes."""

    def press_key(self, key):
        self.s.press(self.s.code(self.KEYS[key]))

    def release_key(self, key):
        self.s.release(self.s.code(self.KEYS[key]))

    def select(self, track, machine_id):
        """Pick machine_id (0: the stock machine) for track through the device's own menu; False if
        no entry gives it."""
        for entry in range(1, 17):
            self.pick(track, entry)
            if self.machine(track) == machine_id:
                return True
        return False


class Digitakt1(Device):
    key, stock_env = 'digitakt-mk1', 'ELEKLOADER_STOCK_154'
    bases = ('mods/core', 'mods/machine-pages')
    machine_claim = 'machine'
    track_count = 8

    STOCK = (0, 1, 2, 3)            # the stock machines, first in the machine list
    sound_without_render = False    # an SDK machine without .render plays an empty voice
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
        """Which of the eight positions of an empty machine's page hold a knob (none)."""
        return tuple(bool(self.read('sdk_alpha_empty_layout', 4 * (2 + k))) for k in range(8))

    def switch_track(self, track):
        self.chord('TRK', str(track + 1))

    def keyboard(self):
        if not getattr(self, '_chromatic', False):
            self.chord('FUNC', 'TRK')   # the chromatic keyboard, as on the hardware
            self._chromatic = True

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
        return (self.peek32(self.kit() + 0x20 + 0xa2 * track + 0x7c) >> 8) & 255

    def raw_value(self, knob, track=0):
        slot = self.destination_for(knob)
        at = self.kit() + 0x20 + 0xa2 * track + 20 + 2 * slot
        return struct.unpack('>h', bytes(self.s.m.uc.mem_read(at, 2)))[0]

    def glyph_pixels(self):
        return tuple(self.crop(25, 13, 16, 16))

    def value_pixels(self, encoder=0):
        x = 18 + 26 * (encoder % 4)
        return self.crop(x, 30 + 27 * (encoder // 4), min(33, 128 - x), 9)

    def destination_for(self, knob):
        return 17 + knob if knob < 8 else 46 + knob - 8

    def destination(self, lfo, track=0):
        at = self.kit() + 0x20 + 0xa2 * track + 20 + 2 * (4 + 8 * lfo)
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
        """The label shown for a logical knob: machine-pages' copy of what .present gave."""
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
    bases = ('mods/core-dn1', 'mods/machines-dn1')
    machine_claim = 'dnmachine'
    track_count = 4

    sound_without_render = True     # FM plays
    SDK_PAGE, SYN2_FIRST = 30, 4    # machines-dn1's first page, and the first page of SYN2's stock list
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
        return self.machine(track) == machine_id

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

    def glyph_pixels(self):
        return tuple(self.crop(25, 13, 16, 16))

    def value_pixels(self, encoder=0):
        x = 18 + 26 * (encoder % 4)
        return self.crop(x, 30 + 27 * (encoder // 4), min(33, 128 - x), 9)

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
        at = int(self.symbols['dnm_sdk_labels'], 16) + 6 * knob
        return bytes(self.s.m.uc.mem_read(at, 6)).split(b'\0')[0].decode('ascii')

    def visible(self, knob):
        at = int(self.symbols['dnm_sdk_visibility'], 16) + knob
        return bool(bytes(self.s.m.uc.mem_read(at, 1))[0])

    def page_action(self, action):
        """Copy, clear or paste the page shown: its key held with RECORD, PLAY or STOP."""
        self.chord('SYN2', {'copy': 'RECORD', 'clear': 'PLAY', 'paste': 'STOP'}[action])
