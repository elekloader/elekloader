// SPDX-License-Identifier: GPL-2.0-or-later
// The web patcher's engine under Node, from an assembled site (packaging/build_web.py),
// needing no firmware:
//   1. tests/test_units.py, in the site's Pyodide, against the site's elekloader.zip;
//   2. the page's bridge (bridge.py), loaded as worker.js loads it, with the site's
//      cores: every core is listed and checked against core/index.json, a file that is
//      no stock firmware and a file that is no mod are refused, and the check and the
//      build refuse to start without a stock file.
//
//   node tests/test_web.mjs build/site
import { readFileSync, readdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const read = p => new Uint8Array(readFileSync(p));   // Pyodide takes typed arrays, not Buffers
const SITE = resolve(process.argv[2] || 'build/site');
const { loadPyodide } = await import(pathToFileURL(join(SITE, 'pyodide', 'pyodide.mjs')).href);

let failed = 0;
function check(what, ok, detail = '') {
  console.log(`${ok ? 'ok    ' : 'FAIL  '} ${what}${detail ? ': ' + detail : ''}`);
  if (!ok) failed++;
}

const t0 = performance.now();
const py = await loadPyodide({ indexURL: join(SITE, 'pyodide') + '/', env: { HOME: '/work' } });
py.setStdout({ batched: s => console.log('   ' + s) });
py.setStderr({ batched: s => console.error('   ' + s) });
py.unpackArchive(read(join(SITE, 'elekloader.zip')), 'zip', { extractDir: '/elek' });
py.runPython("import sys; sys.path.insert(0, '/elek')");
const where = py.runPython('import elekloader, sys; elekloader.__file__ + " " + elekloader.__version__ + ", Python " + sys.version.split()[0]');
check('elekloader from the site\'s zip', where.startsWith('/elek/elekloader/'), where);
console.log(`   Pyodide ${py.version} loaded in ${Math.round(performance.now() - t0)} ms`);

// 1. the unit tests
py.FS.mkdirTree('/work/tests');
py.FS.writeFile('/work/tests/test_units.py', read(join(HERE, 'test_units.py')));
// test_units runs tests/test_link_refusals.py, which reads by path, from the checkout's root,
// the linker's and the loader's sources (the zip's) and test_octatrack.py's DSP fixtures
py.FS.writeFile('/work/tests/test_link_refusals.py', read(join(HERE, 'test_link_refusals.py')));
py.FS.writeFile('/work/tests/test_octatrack.py', read(join(HERE, 'test_octatrack.py')));
// The SHARC suite also needs its author-owned fixtures and example checks, and
// the DN2 machine registry suite machines-dn2's mod.json and machines.c. The DN2
// DSP peek test and the SDK host run are imported (they skip here: Pyodide has no C compiler), and the SDK
// builder suite with its harness (its build checks skip: no stock files).
for (const file of [
  ...readdirSync(HERE).filter(name => /^test_sharc.*\.py$/.test(name)).map(name => 'tests/' + name),
  'tests/test_dn2_machines.py', 'mods/machines-dn2/mod.json', 'mods/machines-dn2/machines.c',
  'mods/machines-dn2/dispatch.sharc.json', 'tests/test_digitakt2.py',
  'mods/machines-dn2/dn2.h', 'mods/machine-pages/src.h',
  'tests/test_dn2_dsp_peek.py',
  'tests/test_sdk_alpha.py', 'tests/sdk_alpha_harness.py', 'tests/sdk_alpha_devices.py',
  'tests/test_sdk_alpha_render.py', 'tests/test_sdk_alpha_host.py',
  'tests/fixtures/sharc.json',
  'examples/sharc-sine/sine.sharc.json', 'examples/sharc-sine/build.py',
  'examples/sharc-sine-probe/execute.py', 'examples/sharc-sine-probe/h1_spills.py',
  'examples/sharc-sine-probe/qualify.py',
]) {
  py.FS.mkdirTree(dirname('/work/' + file));
  py.FS.writeFile('/work/' + file, read(join(HERE, '..', file)));
}
py.FS.symlink('/elek/elekloader', '/work/elekloader');
const code = py.runPython(`
import runpy
try:
    runpy.run_path('/work/tests/test_units.py', run_name='__main__')
    code = 0
except SystemExit as e:
    code = e.code or 0
code`);
check('tests/test_units.py under Pyodide', code === 0, `exit ${code}`);

// 2. the bridge, as worker.js loads it
py.FS.writeFile('/elek/bridge.py', read(join(SITE, 'bridge.py')));
const bridge = py.pyimport('bridge');
const call = (name, args = {}, data = undefined) => JSON.parse(bridge.call(name, JSON.stringify(args), data));
const info = call('info');
check('info', info.version && info.devices.length >= 3, `elekloader ${info.version}; ${info.supported}`);
const cores = JSON.parse(readFileSync(join(SITE, 'core', 'index.json'), 'utf8'));
for (const c of cores) {
  const d = call('add_core', { name: c.file, sha256: c.sha256 }, read(join(SITE, 'core', c.file)));
  check(`core ${c.file}`, d.id === 'core' && d.builtin && d.sha256 === c.sha256 && !d.error,
    `${d.for_label}, ${d.sites.length} sites`);
}
let tampered = false;
try {
  if (cores.length) call('add_core', { name: cores[0].file, sha256: '0'.repeat(64) }, read(join(SITE, 'core', cores[0].file)));
} catch (e) {
  tampered = /not the file the site lists/.test(String(e));
}
check('a core that is not the listed file is refused', tampered || !cores.length);
const listed = call('mods');
check('the cores are listed, made for no stock yet', listed.length === cores.length && listed.every(d => !d.fits));
const st = call('set_stock', { name: 'not-firmware.syx' }, new TextEncoder().encode('\xf0 nope \xf7'));
check('an unknown file is refused as the stock', !st.ok && /not a stock firmware/.test(st.error), st.error.slice(0, 90));
const junk = call('add_mod', { name: 'junk.elemod' }, new TextEncoder().encode('{"elemod": 2}'));
check('a broken mod is refused, not kept', !junk.ok && call('mods').length === cores.length, junk.error);
const notmod = call('add_mod', { name: 'readme.txt' }, new TextEncoder().encode('hi'));
check('a file that is no mod is refused by its name', !notmod.ok, notmod.error);
const chk = call('check', { enabled: listed.map(d => d.path) });
check('the check asks for the stock file first', !chk.ok && /stock firmware first/.test(chk.headline), chk.headline);
const b = JSON.parse(bridge.call('build', JSON.stringify({ enabled: [], version: '2.0a', name: 'x.syx' }), undefined, null));
check('the build asks for the stock file first', !b.ok && /stock firmware first/.test(b.error), b.error);

// the Splash screen maker (web/splash.js): its frames -> a mod the page adds as a file of yours
const sp = info.splash || { targets: [] };
check('the splash maker knows its devices', sp.targets.length >= 4 && sp.rate === 15,
  sp.targets.map(t => `${t.device} ${t.os}`).join(', '));
const frames = new Uint8Array(2048);
frames.fill(0xff, 1024);
for (const t of sp.targets) {
  const m = call('make_splash', { device: t.device, os: t.os, sequence: [0, 1, 1], loop: true, title: 'Test splash',
    source: 'logo.gif' }, frames);
  const r = m.ok ? call('add_mod', { name: m.name }, new TextEncoder().encode(m.text)) : m;
  check(`a splash for ${t.name} ${t.os}`, r.ok && r.mod.id === 'splash' && r.mod.title === 'Test splash',
    r.ok ? `${m.name}, ${r.mod.sites.length} sites` : r.error);
  if (r.ok) call('remove_mod', { path: r.mod.path });
}
const bad = call('make_splash', { device: 'digitakt-mk1', os: '1.54', sequence: [2] }, frames);
check('a splash naming a frame it has not is refused', !bad.ok && /sequence names frames/.test(bad.error), bad.error);
const torn = call('make_splash', { device: 'digitakt-mk1', os: '1.54' }, frames.subarray(0, 1000));
check('torn frames are refused', !torn.ok, torn.error);

// 3. the mod shop: every listed file is there, is the one listed, and loads
let items = [];
try { items = JSON.parse(readFileSync(join(SITE, 'shop', 'index.json'), 'utf8')); } catch { /* no shop */ }
for (const e of items) {
  if (!e.available) { console.log(`   (shop: ${e.file} listed as not available)`); continue; }
  const r = call('add_mod', { name: e.file, sha256: e.sha256 }, read(join(SITE, 'shop', e.file)));
  check(`shop ${e.file}`, r.ok && r.mod.sha256 === e.sha256 && r.mod.id === e.id && r.mod.version === e.version
    && r.mod.for_device === e.device, r.ok ? `${r.mod.for_label}, ${e.license}` : r.error);
  call('remove_mod', { path: r.ok ? r.mod.path : '/work/mods/' + e.file });
}
const first = items.find(e => e.available);
if (first) {
  const raw = read(join(SITE, 'shop', first.file));
  const r = call('add_mod', { name: first.file, sha256: '0'.repeat(64) }, raw);
  check('a shop file that is not the listed one is refused', !r.ok && /not the file the shop lists/.test(r.error), r.error);
}

console.log(failed ? `${failed} failed` : 'all passed');
process.exit(failed ? 1 : 0);
