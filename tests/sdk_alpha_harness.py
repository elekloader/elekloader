# SPDX-License-Identifier: GPL-2.0-or-later
"""A harness for tests that read like what they check.

A test file defines the SDK machines it needs as C, next to its tests:

    HIDING = sdk_machine("Hiding", '''
        ... plain SDK C, with .id = @ID@ ...
    ''')

    @sdk_tests
    class Hiding:
        def test_a_hidden_knob_ignores_its_encoder(self):
            hide, target = self.track.use(HIDING).knobs("HIDE", "TRGT")
            hide.set(127)
            self.assertFalse(target.visible)

`@sdk_tests` runs the class once for every device in sdk_alpha_devices.py. Every machine a test
file defines is built for the device and linked into one firmware, booted once; each test then
gets a fresh emulator from the saved state in a fraction of a second. A machine a device cannot
build fails the tests that use it, with the builder's message; an observation the device's
adapter does not have yet fails with "cannot do this yet".

Run one feature file at a time; a device whose files are not given is skipped. The Digitakt and the
Digitone mk1 run in digiemu, with its Python:

  ELEKLOADER_DIGIEMU=<digiemu> ELEKLOADER_STOCK_154=<Digitakt_OS1.54.syx> \\
  ELEKLOADER_STOCK_DN1_144=<Digitone_and_Digitone_Keys_OS1.44.syx> ELEKLOADER_CROSS=m68k-elf- \\
  uv run --project "$ELEKLOADER_DIGIEMU" python tests/test_sdk_alpha_hiding.py

The Digitone II runs in DigiKit's ColdFire emulator (sdk_alpha_devices.DigiKit), and its sound in
DigiKit's coupled emulator, with a digi-emud built with coupled audio and its ready inputs; Selache
assembles the SHARC renders:

  ELEKLOADER_DIGIKIT=<digikit> ELEKLOADER_DIGI_EMUD=<digi-emud> \\
  ELEKLOADER_DIGI_EMUD_COUPLED=<coupled digi-emud> ELEKLOADER_DN2_EMU_READY=<ready inputs> SELACHE=<selache> \\
  ELEKLOADER_DN2_SYX=<Digitone_II_OS1.12.syx> ELEKLOADER_CROSS=m68k-elf- \\
  python tests/test_sdk_alpha_sound.py
"""
import atexit
from contextlib import contextmanager
import gc
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from elekloader.sdk import build                              # noqa: E402
from elekloader.sdk.sdk_alpha_build import SDK_ALPHA_TARGETS, sdk_alpha_build    # noqa: E402
import sdk_alpha_devices                                      # noqa: E402
from sdk_alpha_devices import CannotYet, Device                # noqa: E402

RAW_MAX = 127 << 8              # a knob's native 8.8 range, whatever its bounds
DEVICES_LISTED = {}
for _device, _os in SDK_ALPHA_TARGETS:
    DEVICES_LISTED.setdefault(_device, []).append(_os)

_ids = iter(range(100, 128))    # the ids of the registered machines the test files write inline
REGISTRY = []


class SdkMachine:
    """An SDK mod a test uses: C written in the test file, or an example's folder."""

    def __init__(self, name, source=None, directory=None, substitutions=None, register=True, sharc=None):
        self.name, self.source, self.directory = name, source, directory
        self.substitutions = substitutions or {}
        self.sharc = sharc or {}            # file name -> SHARC assembly: its render on the Digitone II
        if source is None:
            self.id = None              # a folder's own id, once built
        elif register:
            self.id = next(_ids)
        else:
            self.id = 100               # built and linked alone, so it shares the id
        if register:
            REGISTRY.append(self)

    def __repr__(self):
        return 'SdkMachine(%s)' % self.name

    def write(self, folder):
        """The mod's folder, for sdk_alpha_build."""
        if self.directory is not None:
            return str(self.directory)
        path = Path(folder) / self.slug
        path.mkdir(parents=True)
        source = self.source.replace('@ID@', str(self.id))
        for key, text in self.substitutions.items():
            source = source.replace('@%s@' % key, text)
        (path / 'machine.c').write_text(source)
        for name, text in self.sharc.items():
            (path / name).write_text(text)
        (path / 'mod.json').write_text(json.dumps({
            'id': self.mod_id, 'version': '0.1', 'license': 'GPL-2.0-or-later',
            'devices': DEVICES_LISTED, 'sources': ['machine.c'],
            **({'sharc': {'sources': list(self.sharc)}} if self.sharc else {})}))
        return str(path)

    @property
    def slug(self):
        return self.name.lower().replace(' ', '-')

    @property
    def mod_id(self):
        return 'sdk-test-' + self.slug


def sdk_machine(name, c, sharc=None, **substitutions):
    """A machine written as SDK C in the test file. `@ID@` stands for the id the harness gives it;
    any other `@NAME@` is replaced by the keyword argument of that name. `sharc` maps file names to
    the SHARC assembly of its render on the Digitone II (docs/SDK.md, "render on the Digitone II")."""
    return SdkMachine(name, source=textwrap.dedent(c), sharc=sharc,
                      substitutions={key.upper(): text for key, text in substitutions.items()})


def sdk_example(name, directory):
    """An existing SDK mod folder (its own .id)."""
    return SdkMachine(name, directory=ROOT / directory)


class Firmware:
    """One device's firmware: the device's core, its companion and every registered machine,
    booted once and saved as a snapshot by the device's emulator (its backend)."""

    def __init__(self, device):
        stock = device.stock()
        device.backend.check()
        self.device, self.work = device, tempfile.mkdtemp(prefix='sdk-harness-')
        atexit.register(shutil.rmtree, self.work, True)
        mods = [build.build(str(path), stock, self.work)[0] for path in device.bases()]
        self.refused, self.layouts, ids = {}, {}, []
        for machine in REGISTRY:
            try:
                path, mod, layouts = sdk_alpha_build(machine.write(self.work), stock, self.work)
            except build.BuildError as error:
                self.refused[machine] = str(error).strip().splitlines()[0]
                continue
            mods.append(path)
            found = list(layouts)
            machine.id = found[0]
            ids += found
            self.layouts.update(layouts)
        self.machine_ids = ids
        syx = os.path.join(self.work, 'firmware.syx')
        done = subprocess.run([sys.executable, '-m', 'elekloader.patch', '--stock', stock,
                               *sum((['--mod', p] for p in mods), []), '--out', syx,
                               '--version', 'SH01'], cwd=ROOT, capture_output=True, text=True)
        if done.returncode:
            raise RuntimeError('%s: patch failed\n%s' % (device.key, done.stdout[-2000:]))
        with open(syx + '.map.json') as fh:
            self.symbols = json.load(fh)
        self.dsp_symbols = (dsp_symbols(syx, [m for m in REGISTRY if m.sharc and m not in self.refused], self.work)
                            if 'kernels' in device.features else {})
        self.booted = device.backend(syx, stock, self.work)
        self.cwd = self.booted.cwd

    def restore_card(self):
        """Put the card back as booted, if a test saved to it."""
        self.booted.restore_card()

    def session(self, audio=False):
        """A new emulator at the settled user interface, whatever happened to the last."""
        return self.booted.session(audio=audio)


def dsp_symbols(syx, machines, work):
    """name -> DSP byte address of the data labels of the machines' SHARC renders, where the linker
    placed them: the same sources linked again for their symbols, at the mod's place in the build."""
    if not machines:
        return {}
    from elekloader.sdk import sharc
    with open(syx + '.json') as fh:
        placed = {m['id']: m['data'][0] for m in json.load(fh)['link']['layout']['sharc']['kernels']['mods']}
    tools, found = sharc.selache_tools(), {}
    for machine in machines:
        folder = Path(work) / machine.slug
        objects = [folder / (name + '.doj') for name in machine.sharc]
        for obj in objects:
            subprocess.run([str(tools / 'selas'), '-proc', 'ADSP-21569', '-o', str(obj), str(obj)[:-4]], check=True)
        with tempfile.TemporaryDirectory() as tmp:
            symbols = sharc._relocatable(tools, objects, tmp)['symbols']
        found.update({name[:-1]: placed[machine.mod_id] + at for name, (kind, at) in symbols.items()
                      if kind == 'data' and name.endswith('.')})    # a C name's SHARC label ends in a dot
    return found


_firmware = {}


def firmware(device):
    if device.key not in _firmware:
        try:
            _firmware[device.key] = Firmware(device)
        except unittest.SkipTest:
            raise
        except Exception as error:              # every test of the device reports the same failure
            _firmware[device.key] = error
    if isinstance(_firmware[device.key], Exception):
        raise _firmware[device.key]
    return _firmware[device.key]


class Knob:
    """One of a machine's knobs on a track, found by the label it declares."""

    def __init__(self, track, machine, spec, places):
        self.track, self.machine, self.spec, self.places = track, machine, spec, places

    def __repr__(self):
        return 'knob %s of %s on track %d' % (self.spec['label'], self.machine.name, self.track.number)

    @property
    def raw(self):
        """The stored value, native 8.8: 0..RAW_MAX whatever the bounds."""
        return self.track.case.panel.raw_value(self.spec['id'], self.track.index)

    @property
    def value(self):
        """The raw value mapped onto the knob's bounds as sdk_alpha_map does: up to 128 values, an equal
        share of the 128 positions each, from raw's nearest position; more, rounded to nearest (halves up)."""
        low, high = self.spec['min'], self.spec['max']
        if high - low < 128:
            return low + ((max(0, min(self.raw, RAW_MAX)) + 128) >> 8) * (high - low + 1) // 128
        return low + ((high - low) * self.raw + RAW_MAX // 2) // RAW_MAX

    def _settled(self):
        """Let the firmware finish reacting: .present's result shows about 300 ms after an edit."""
        self.track.show()
        self.track.case.panel.settle(quiet_ms=600, limit_ms=3000)

    @property
    def label(self):
        """The label .present left, as the adapter's copy holds it (the device's sdk_alpha_labels),
        not the screen's pixels: test_sdk_alpha_labels compares those."""
        self.track.case.say('what is %s labelled?' % self.spec['label'])
        self._settled()
        return self.track.case.panel.label(self.spec['id'])

    @property
    def visible(self):
        """Whether .present left the knob shown, as the adapter's copy holds it (the device's
        sdk_alpha_visibility), not the screen's pixels: `pixels` is those."""
        self.track.case.say('is %s shown?' % self.spec['label'])
        self._settled()
        return self.track.case.panel.visible(self.spec['id'])

    def turn(self, detents, page=None, settle=True):
        """Turn its encoder, one detent at a time; on `page` if it sits on several. A hidden knob's
        encoder does nothing. `settle=False` does not wait for the screen between detents."""
        page, encoder = next(place for place in self.places if page in (None, place[0]))
        self.track.show(page)
        self.track.case.say('%s %+d' % (self.spec['label'], detents))
        panel = self.track.case.panel
        if settle:
            panel.turn('ABCDEFGH'[encoder], detents)
        else:
            for _ in range(abs(detents)):
                panel.turn_now('ABCDEFGH'[encoder], 1 if detents > 0 else -1, after_ms=50)

    @property
    def label_pixels(self):
        """The screen where the knob's label is: it shows the value while the knob turns."""
        self.track.show()
        return self.track.case.panel.value_pixels(self.places[0][1])

    @property
    def pixels(self):
        """The knob's graphic and label, including an empty position when it is hidden."""
        self._settled()
        return self.track.case.panel.control_pixels(self.places[0][1])

    @property
    def glyph_on_screen(self):
        """The 16 rows of pixels drawn for the knob's graphic (bit 15 is the left pixel)."""
        self._first_encoder()
        self.track.show()
        self.track.case.panel.settle(quiet_ms=600, limit_ms=3000)
        return self.track.case.panel.glyph_pixels()

    def _first_encoder(self):
        if self.places[0][1] != 0:
            raise CannotYet('the screen is read for encoder A only')

    def set(self, value):
        """Turn it until it reads `value`; fails if it cannot get there."""
        self.track.case.say('%s.set(%s)' % (self.spec['label'], value))
        for _ in range(500):
            now = self.value
            if now == value:
                self._settled()         # so what the machine does about it is in effect
                return
            self.turn((4 if abs(now - value) > 8 else 1) * (1 if now < value else -1))
        self.track.case.fail('%r would not reach %s (at %s)' % (self, value, self.value))


class UsedMachine:
    """A machine picked for a track."""

    def __init__(self, track, machine):
        self.track, self.machine = track, machine
        self.layout = track.case.fw.layouts[machine.id]

    def knob(self, label):
        places, spec = [], None
        for page, entry in enumerate(self.layout['pages']):
            for encoder, knob in sorted(entry['knobs'].items()):
                if knob['label'] == label:
                    places.append((page, encoder))
                    spec = knob
        if spec is None:
            self.track.case.fail('%s declares no knob %r' % (self.machine.name, label))
        return Knob(self.track, self.machine, spec, places)

    def knobs(self, *labels):
        return [self.knob(label) for label in labels]

    def all_knobs(self):
        """Every knob the machine declares, by logical id."""
        return self.knobs(*[knob['label'] for knob in self.layout['knobs']])


class Lfo:
    """An LFO of a track, whose destination list names the knobs of the track's machine."""

    def __init__(self, track, index):
        self.track, self.index = track, index

    def choose(self, knob):
        """Pick the knob in the LFO's destination list, opening the list if it is not open."""
        track, panel = self.track, self.track.case.panel
        track.case.say('LFO %d points at %s' % (self.index + 1, knob.spec['label']))
        if track.open_list not in (None, self.index):
            self.confirm()
        if track.open_list != self.index:
            track.case.assertTrue(panel.open_lfo(self.index), 'the LFO page did not open')
            track.open_list, track.shown = self.index, None
        track.case.assertTrue(panel.choose_destination(self.index, knob.spec['id']),
                              'the LFO list would not reach %s' % knob.spec['label'])

    def label_of(self, knob):
        """The name the destination list shows for the knob (the machine's label, as it stands)."""
        return self.track.case.panel.lfo_label(knob.spec['id'])

    @property
    def target(self):
        """The knob the LFO points at, once chosen."""
        wanted = self.track.case.panel.destination_knob(self.index)
        return next((knob for knob in self.track.used.all_knobs() if knob.spec['id'] == wanted), None)

    def confirm(self):
        """Leave the destination list with the choice made."""
        self.track.case.panel.confirm_destination()
        self.track.open_list = None


class Sound:
    """What the device played while a key was held: its left output, 48 kHz."""
    RATE = 48000

    def __init__(self, samples):
        self.samples = samples

    @property
    def peak(self):
        return max((abs(x) for x in self.samples), default=0)

    @property
    def frequency(self):
        """The tone's pitch in Hz, from its rising zero crossings (interpolated) where it is loud;
        None with fewer than three."""
        loud = [i for i, x in enumerate(self.samples) if abs(x) > self.peak // 8]
        if not loud:
            return None
        part = self.samples[loud[0]:loud[-1] + 1]
        ups = [i + a / (a - b) for i, (a, b) in enumerate(zip(part, part[1:])) if a < 0 <= b]
        return (len(ups) - 1) * self.RATE / (ups[-1] - ups[0]) if len(ups) >= 3 else None


class Track:
    def __init__(self, case, index):
        self.case, self.index, self.shown, self.open_list = case, index, None, None

    @property
    def number(self):
        return self.index + 1

    def use(self, machine):
        """Pick the machine for this track through the device's own menu."""
        case = self.case
        case.say('track %d uses %s' % (self.number, machine.name))
        if machine in case.fw.refused:
            case.fail('%s cannot build %s: %s' % (case.device.key, machine.name, case.fw.refused[machine]))
        case.assertTrue(case.panel.select(self.index, machine.id),
                        '%s: %s could not be picked on track %d' % (case.device.key, machine.name, self.number))
        self.shown = None
        self.used = UsedMachine(self, machine)
        return self.used

    def lfo(self, number):
        """One of the track's LFOs, numbered from 1, to point at a knob."""
        return Lfo(self, number - 1)

    @property
    def page(self):
        """The page of the machine on the screen for this track."""
        return self.case.panel.shown_page(self.index)

    def empty_positions(self):
        """Which of the page's eight positions hold a knob, as the adapter's page table holds it."""
        self.show(0)
        return self.case.panel.empty_positions()

    def turn_encoder(self, encoder, detents):
        """Turn an encoder by its letter, whatever it holds."""
        self.show(self.shown or 0)
        self.case.panel.turn(encoder, detents)

    def use_stock(self):
        """Put the device's own machine back on this track."""
        case = self.case
        case.assertTrue(case.panel.select(self.index, 0),
                        '%s: the stock machine could not be picked on track %d' % (case.device.key, self.number))
        self.shown = None

    @property
    def machine_id(self):
        """The id of the machine on this track (0 for the device's own)."""
        return self.case.panel.machine(self.index)

    def show(self, page=0):
        """Make this track's machine page the one on the screen."""
        panel = self.case.panel
        if panel.active_track() != self.index:
            panel.switch_track(self.index)
            self.shown = None
        if self.shown != page or self.open_list is not None:
            self.open_list = None
            panel.open_page()
            for _ in range(3):
                if panel.shown_page(self.index) == page:
                    break
                panel.next_page()
            self.shown = page

    def play(self, key, hold_ms=300):
        """Hold a keyboard key and measure its sound after the first 20 ms of attack."""
        panel = self.case.panel
        if panel.active_track() != self.index:
            panel.switch_track(self.index)
            self.shown = None
        panel.keyboard()
        panel.wait(20)
        start = panel.audio_position()
        panel.press_key(key)
        panel.wait(hold_ms)
        sound = Sound(panel.audio_since(start, skip_ms=20))
        panel.release_key(key)
        panel.wait(200)
        return sound

    def page_action(self, action):
        """Copy, clear or paste the page on the screen."""
        self.case.say('%s the page' % action)
        self.show(self.shown or 0)
        self.case.panel.page_action(action)
        self.case.panel.settle(quiet_ms=600, limit_ms=3000)


class SdkTestCase(unittest.TestCase):
    """The base of every generated test class: a fresh emulator on the device's firmware."""
    device = None
    audio = False
    requires = ()

    def setUp(self):
        missing = set(self.requires) - self.device.features
        if missing:
            self.skipTest('%s: backend does not implement %s yet' % (self.device.key, ', '.join(sorted(missing))))
        self.fw = firmware(self.device)
        if self.fw.cwd:
            cwd = os.getcwd()
            os.chdir(self.fw.cwd)
            self.addCleanup(os.chdir, cwd)
        self.session = self.fw.session(audio=self.audio)
        self.addCleanup(self._close)
        self.panel = self.device(self.session, self.fw.symbols, self.fw.machine_ids, self.fw.layouts)
        self.tracks = [Track(self, i) for i in range(self.device.track_count)]
        self.track = self.tracks[0]
        from sdk_alpha_viewer import watcher
        self.viewer = watcher()         # SDK_WATCH=1: show the test running in a window
        self.failure = None
        if self.viewer:
            self.viewer.attach(self.session, self.id().split('.', 1)[-1], self.device.key)

    def say(self, text):
        """Tell the window what the test is doing (nothing, when it is not watched)."""
        if self.viewer:
            self.viewer.say(text)

    def _note(self, error):
        self.failure = self.failure or (str(error).splitlines() or [type(error).__name__])[0]

    def _callTestMethod(self, method):
        try:
            method()
        except Exception as error:
            self._note(error)
            raise

    @contextmanager
    def subTest(self, *args, **kwargs):
        with super().subTest(*args, **kwargs):
            try:
                yield
            except Exception as error:
                self._note(error)
                raise

    def read(self, symbol, index=0):
        """A word of a variable a test machine keeps for the test (an array's `index`th): its SHARC
        render's, on the Digitone II."""
        if symbol in self.fw.dsp_symbols:
            return self.panel.dsp_word(self.fw.dsp_symbols[symbol] + 4 * index)
        return self.panel.read(symbol, 4 * index)

    def wait(self, ms):
        self.say('waiting %.1f s' % (ms / 1000))
        self.panel.wait(ms)

    def save_project(self, slot=2):
        """Save the project through the device's menus (the card is put back after the test)."""
        self.say('saving the project')
        self.panel.save_project(slot)
        self._forget_pages()

    def load_project(self, slot=2):
        """Load a saved project through the device's menus."""
        self.say('loading the project')
        self.panel.load_project(slot)
        self._forget_pages()

    def _forget_pages(self):
        for track in self.tracks:
            track.shown, track.open_list = None, None

    def _close(self):
        self.session.close()
        gc.collect()
        self.fw.restore_card()

    def tearDown(self):
        try:
            self.assertIsNone(self.session.halted, '%s: the firmware halted' % self.device.key)
        except AssertionError as error:
            self._note(error)
            raise
        finally:
            if self.viewer:
                self.viewer.finish(self.failure)


def _devices(base=Device):
    for device in base.__subclasses__():
        if device.key:
            yield device
        yield from _devices(device)


def sdk_tests(cls):
    """Run the plain class `cls` once for every device, as test classes of this module."""
    module = sys.modules[cls.__module__]
    for device in _devices():
        name = '%s_%s' % (cls.__name__, device.key.replace('-', '_'))
        attrs = {'device': device, '__module__': cls.__module__}
        setattr(module, name, type(name, (cls, SdkTestCase), attrs))
    return cls
