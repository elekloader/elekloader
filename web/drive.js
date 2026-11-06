// SPDX-License-Identifier: GPL-2.0-or-later
// The +Drive mods view (#drive): a unit's /mods folder over Web MIDI (docs/WEB.md, "The +Drive mods view"). app.js
// imports this module the first time the view opens, and calls open() each time it does. The engine is
// js/'s TypeScript as the site serves it (engine/): the build-checker (drive.js), the data container (container.js)
// and the Data router's requests (datarpc.js), each the Python's, byte for byte. Every write goes through
// datarpc.Mods, whose paths are inside /mods; an upload and the set are checked against the unit's build first.

const V = new URL(import.meta.url).search;
const ENGINE = document.querySelector('meta[name="engine"]')?.content || 'engine';
const site = path => new URL(path + V, import.meta.url);
const [drive, datarpc] = await Promise.all([
  import(site(`./${ENGINE}/drive.js`).href), import(site(`./${ENGINE}/datarpc.js`).href)]);

const $ = id => document.getElementById(id);
function el(tag, attrs = {}, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v === null || v === undefined || v === false) continue;
    if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else e.setAttribute(k, v === true ? '' : v);
  }
  for (const k of kids.flat()) if (k !== null && k !== undefined && k !== false) e.append(k instanceof Node ? k : String(k));
  return e;
}
const kb = n => (n >= 10240 ? `${Math.round(n / 1024)} KB` : n >= 1024 ? `${(n / 1024).toFixed(1)} KB` : `${n} B`);
const hex8 = n => '0x' + (n >>> 0).toString(16).padStart(8, '0');
const say = (id, text, cls = '') => { const e = $(id); e.textContent = text; e.className = `small ${cls}`; };
const SPECIAL = new Set(['.build', '.status', 'active']);
const KINDS = ['core', 'flashed', 'loader'];

// ---- the port --------------------------------------------------------------------------------------------------

/** A Web MIDI input and output as datarpc's port: SysEx messages only, whole (F0 to F7). */
class MidiPort {
  constructor(input, output) {
    this.input = input; this.output = output; this.queue = []; this.waiting = null;
    input.onmidimessage = e => {
      if (e.data[0] !== 0xf0) return;
      const raw = new Uint8Array(e.data);
      if (this.waiting) { const w = this.waiting; this.waiting = null; w(raw); } else this.queue.push(raw);
    };
  }
  send(raw) { this.output.send(raw); }
  receive(ms) {
    if (this.queue.length) return Promise.resolve(this.queue.shift());
    return new Promise(res => {
      const t = setTimeout(() => { this.waiting = null; res(null); }, ms);
      this.waiting = raw => { clearTimeout(t); res(raw); };
    });
  }
  close() { this.input.onmidimessage = null; }
}

// ---- the state -------------------------------------------------------------------------------------------------

const S = {
  access: null, port: null, mods: null, build: null, head: null,
  files: [],            // [{name, size, raw, id, version, alone (a check result)}]
  chosen: new Set(),    // file names ticked for /mods/active
  order: [],            // /mods/active's order, then ticks in the order they were made
  busy: false,
};

async function busy(f) {
  if (S.busy) return;
  S.busy = true;
  for (const b of $('view-drive').querySelectorAll('button')) b.disabled = true;
  try { return await f(); } finally {
    S.busy = false;
    for (const b of $('view-drive').querySelectorAll('button')) b.disabled = false;
    renderSet();
  }
}

// ---- connect ---------------------------------------------------------------------------------------------------

async function listPorts() {
  const sel = $('dm-port');
  if (!navigator.requestMIDIAccess) {
    say('dm-conn', 'This browser has no Web MIDI. Chrome, Edge, Opera and Firefox have it.', 'bad');
    $('dm-connect-btn').disabled = true;
    return;
  }
  try {
    S.access = await navigator.requestMIDIAccess({ sysex: true });
  } catch (e) {
    say('dm-conn', `MIDI with SysEx was not allowed (${e.message || e}).`, 'bad');
    return;
  }
  const outs = [...S.access.outputs.values()], ins = [...S.access.inputs.values()];
  sel.replaceChildren(...outs.map(o => el('option', { value: o.id }, o.name)));
  const rytm = outs.find(o => /rytm/i.test(o.name));
  if (rytm) sel.value = rytm.id;
  if (!outs.length) say('dm-conn', 'No MIDI ports. Connect the unit over USB, with USB CONFIG set to a mode with MIDI.', 'warn');
  S.access.onstatechange = () => { if (!S.port) listPorts(); };
  return ins;
}

function inputFor(out) {
  const ins = [...S.access.inputs.values()];
  return ins.find(i => i.name === out.name) || ins.find(i => i.manufacturer === out.manufacturer && i.name.split(' ')[0] === out.name.split(' ')[0]);
}

async function connect() {
  const out = S.access && S.access.outputs.get($('dm-port').value);
  if (!out) return;
  const input = inputFor(out);
  if (!input) { say('dm-conn', `No input port goes with ${out.name}.`, 'bad'); return; }
  if (S.port) S.port.close();
  S.port = new MidiPort(input, out);
  S.mods = new datarpc.Mods(new datarpc.Client(S.port, 5000));
  await busy(async () => {
    say('dm-conn', `Reading ${out.name}…`);
    try {
      const [head, body] = await S.mods.get('.build');
      S.head = head;
      S.build = drive.loadBuild(body);
    } catch (e) {
      S.build = null;
      say('dm-conn', `${out.name}: ${e.message}. Is a build with the Drive Loader running on it?`, 'bad');
      return;
    }
    say('dm-conn', `Connected: ${out.name}`, 'ok');
    renderBuild();
    await refresh();
    catalog();
  });
}

// ---- the build -------------------------------------------------------------------------------------------------

function renderBuild() {
  const b = S.build;
  $('dm-build').hidden = false;
  $('dm-build-label').textContent = b.label();
  $('dm-modules').replaceChildren(el('tr', {}, el('th', {}, 'Module'), el('th', {}, 'Version'), el('th', {}, 'Kind'), el('th', { class: 'dm-num' }, 'Exports')),
    ...b.modules.map(m => el('tr', {}, el('td', {}, m.name), el('td', {}, m.version), el('td', {}, KINDS[m.kind] || m.kind),
      el('td', { class: 'dm-num' }, m.exports.length))));
  const more = b.tables.filter(t => t.flags & 1 && !(t.flags & 4));
  $('dm-tables').replaceChildren(el('tr', {}, el('th', {}, 'Table'), el('th', { class: 'dm-num' }, 'Entries'), el('th', { class: 'dm-num' }, 'Spare')),
    ...more.map(t => el('tr', {}, el('td', {}, el('code', {}, t.name)), el('td', { class: 'dm-num' }, `${t.count} of ${t.capacity}`),
      el('td', { class: 'dm-num' }, t.capacity - t.count))));
  renderRoom();
}

function renderRoom(r = null) {
  const b = S.build, used = r ? r.end - b.lo : 0;
  $('dm-room').replaceChildren(
    el('dt', {}, '+Drive area'), el('dd', {}, `${hex8(b.lo)}..${hex8(b.hi)} (${kb(b.hi - b.lo)})`),
    el('dt', {}, 'The chosen set'), el('dd', {}, `${kb(used)} used, ${kb(b.hi - b.lo - used)} free`),
    el('dt', {}, 'Largest file'), el('dd', {}, b.stagingSize ? kb(b.stagingSize) : 'no limit given'));
}

// ---- /mods -----------------------------------------------------------------------------------------------------

async function refresh() {
  const list = await S.mods.ls();
  const files = [];
  for (const [name, kind, size] of list) {
    if (kind !== 2 || SPECIAL.has(name)) continue;
    const f = { name, size, raw: null, id: null, version: null };
    try { f.raw = (await S.mods.get(name))[1]; } catch (e) { f.error = e.message; }
    files.push(f);
  }
  for (const f of files) {
    if (!f.raw) continue;
    f.alone = drive.check(S.build, [[f.name, f.raw]]).choices[0];
    f.id = f.alone.id;
    f.version = f.alone.mod ? f.alone.mod.version : null;
  }
  S.files = files;
  // the set: /mods/active as the unit reads it
  let active = [];
  try {
    const [, body] = await S.mods.get('active');
    active = drive.parseActive(new TextDecoder().decode(body)) || [];
  } catch (e) {
    if (!(e instanceof datarpc.RpcError)) throw e;          // none yet
  }
  S.order = active.map(([n]) => n);
  S.chosen = new Set(S.order.filter(n => files.some(f => f.name === n)));
  S.stale = active.filter(([n, crc]) => { const f = files.find(x => x.name === n); return !f || !f.raw || drive.fileCrc(f.raw) !== crc; }).map(([n]) => n);
  let status = '';
  try { status = new TextDecoder().decode((await S.mods.get('.status'))[1]); } catch (e) { if (!(e instanceof datarpc.RpcError)) throw e; }
  $('dm-status').hidden = false;
  $('dm-status-text').textContent = status.trim() || 'Nothing loaded at the last boot.';
  $('dm-files').hidden = false;
  renderFiles();
}

function renderFiles() {
  $('dm-file-list').replaceChildren(
    el('tr', {}, el('th', {}, 'Load'), el('th', {}, 'File'), el('th', {}, 'Mod'), el('th', { class: 'dm-num' }, 'Size'), el('th', {}, 'Alone on this build'), el('th', {}, '')),
    ...S.files.map(f => el('tr', {},
      el('td', {}, el('input', { type: 'checkbox', 'aria-label': `Load ${f.name}`, checked: S.chosen.has(f.name), disabled: !f.raw,
        onchange: e => { if (e.target.checked) { S.chosen.add(f.name); if (!S.order.includes(f.name)) S.order.push(f.name); } else S.chosen.delete(f.name); renderSet(); } })),
      el('td', {}, el('code', {}, f.name)),
      el('td', {}, f.id ? `${f.id} ${f.version ?? ''}` : ''),
      el('td', { class: 'dm-num' }, kb(f.size)),
      el('td', { class: f.error ? 'bad' : f.alone.state === drive.LOADED ? 'ok' : 'warn' }, f.error || (f.alone.state === drive.LOADED ? 'loads' : f.alone.why())),
      el('td', {}, el('button', { type: 'button', class: 'dm-mini', disabled: !f.raw, onclick: () => download(f.name, f.raw) }, 'Download'), ' ',
        el('button', { type: 'button', class: 'dm-mini dm-danger', onclick: e => remove(f, e.target) }, 'Delete')))));
  if (!S.files.length) $('dm-file-list').append(el('tr', {}, el('td', { colspan: 6, class: 'muted' }, 'No mods in /mods yet.')));
  renderSet();
}

function chosenFiles() {
  return S.order.filter(n => S.chosen.has(n)).map(n => S.files.find(f => f.name === n)).filter(f => f && f.raw);
}

function renderSet() {
  if (!S.build || $('dm-files').hidden) return;
  const set = chosenFiles();
  const r = drive.check(S.build, set.map(f => [f.name, f.raw]));
  const lines = drive.resultLines(r);
  if (S.stale && S.stale.length) lines.push(`/mods/active names ${S.stale.join(', ')}, which the unit will drop (missing or changed)`);
  $('dm-set-check').textContent = lines.join('\n');
  renderRoom(r);
  const ok = r.choices.every(c => c.state === drive.LOADED);
  $('dm-save-set').disabled = S.busy || !ok;
  return r;
}

async function saveSet() {
  await busy(async () => {
    const set = chosenFiles();
    const r = drive.check(S.build, set.map(f => [f.name, f.raw]));
    if (!r.choices.every(c => c.state === drive.LOADED)) { say('dm-set-msg', 'REFUSED: /mods/active is unchanged', 'bad'); return; }
    const text = drive.activeText(set.map(f => [f.name, f.raw]));
    try {
      await S.mods.put('active', new TextEncoder().encode(text), S.head);
    } catch (e) { say('dm-set-msg', e.message, 'bad'); return; }
    say('dm-set-msg', `Saved: ${set.length} mod${set.length === 1 ? '' : 's'} at the next boot.`, 'ok');
    await refresh();
  });
}

function download(name, raw) {
  const a = el('a', { href: URL.createObjectURL(new Blob([raw], { type: 'application/json' })), download: name });
  document.body.append(a);
  a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
}

async function remove(f, button) {
  if (button.dataset.sure !== '1') {                     // a second press deletes
    button.dataset.sure = '1';
    button.textContent = S.chosen.has(f.name) ? 'In the set: delete?' : 'Delete?';
    setTimeout(() => { if (button.isConnected) { button.dataset.sure = ''; button.textContent = 'Delete'; } }, 4000);
    return;
  }
  await busy(async () => {
    try { await S.mods.rm(f.name); } catch (e) { say('dm-upload-msg', e.message, 'bad'); return; }
    say('dm-upload-msg', `Deleted ${S.mods.path(f.name)}.`, 'ok');
    await refresh();
  });
}

// ---- uploads ---------------------------------------------------------------------------------------------------

/** The files a set of new ones needs from /mods to check: what they require, and what that requires. */
function withRequired(news) {
  const out = [...news], ids = new Set(news.map(n => n.id).filter(Boolean));
  for (let i = 0; i < out.length; i++) {
    for (const r of out[i].requires || []) {
      if (ids.has(r)) continue;
      const f = S.files.find(x => x.id === r && x.raw && !news.some(n => n.name === x.name));
      if (f) { ids.add(r); out.push({ name: f.name, raw: f.raw, id: f.id, requires: f.alone.mod ? f.alone.mod.requires : [] }); }
    }
  }
  return out;
}

/** Check `files` ([{name, raw}]) against the build, with what they require from /mods, then upload them all, or none. */
async function upload(files) {
  if (!S.build) return;
  await busy(async () => {
    for (const f of files) {
      if (!/\.elemod$/.test(f.name)) { say('dm-upload-msg', `REFUSED: ${f.name} is not an .elemod file; nothing was uploaded`, 'bad'); return; }
      const c = drive.check(S.build, [[f.name, f.raw]]).choices[0];
      f.id = c.id;
      f.requires = c.mod ? c.mod.requires : [];
    }
    const set = withRequired(files);
    const r = drive.check(S.build, set.map(f => [f.name, f.raw]));
    const bad = r.choices.filter(c => files.some(f => f.name === c.name) && c.state !== drive.LOADED);
    if (bad.length) {
      say('dm-upload-msg', `REFUSED: nothing was uploaded. ${bad.map(c => `${c.name}: ${c.why()}`).join('; ')}`, 'bad');
      return;
    }
    for (const f of files) {
      try {
        await S.mods.put(f.name, f.raw, S.head, (sent, total) => say('dm-upload-msg', `Uploading ${f.name}: ${Math.round(100 * sent / total)}%`));
      } catch (e) { say('dm-upload-msg', `${f.name}: ${e.message}`, 'bad'); await refresh(); return; }
    }
    say('dm-upload-msg', `Uploaded ${files.map(f => S.mods.path(f.name)).join(', ')}. Tick ${files.length === 1 ? 'it' : 'them'} to load at the next boot.`, 'ok');
    for (const f of files) if (!S.order.includes(f.name)) S.order.push(f.name);
    await refresh();
  });
}

async function readFiles(list) {
  return Promise.all([...list].map(async f => ({ name: f.name, raw: new Uint8Array(await f.arrayBuffer()) })));
}

// ---- the shop --------------------------------------------------------------------------------------------------

async function catalog() {
  let items;
  try {
    const res = await fetch(site('./shop/index.json'));
    if (!res.ok) return;
    items = await res.json();
  } catch { return; }
  items = items.filter(e => e.available && e.device === S.build.deviceKey && e.os === S.build.os);
  if (!items.length) return;
  const rows = [];
  for (const e of items) {
    let raw = null, kind = 'flash', why = '';
    try {
      raw = new Uint8Array(await (await fetch(site(`./shop/${e.file}`))).arrayBuffer());
      const only = drive.flashOnly(JSON.parse(new TextDecoder().decode(raw)));
      if (only === null) kind = '+Drive'; else why = `uses ${only}`;
    } catch { why = 'not readable'; }
    const c = kind === '+Drive' ? drive.check(S.build, [[e.file, raw]]).choices[0] : null;
    rows.push(el('tr', {},
      el('td', {}, e.title || e.id, el('div', { class: 'muted small' }, `${e.id} ${e.version}`)),
      el('td', {}, el('span', { class: `dm-tag ${kind === '+Drive' ? 'dm-drive' : ''}` }, kind), why ? el('div', { class: 'muted small' }, why) : ''),
      el('td', { class: c ? (c.state === drive.LOADED ? 'ok' : 'warn') : 'muted' }, c ? (c.state === drive.LOADED ? 'loads alone' : c.why()) : ''),
      el('td', {}, kind === '+Drive' ? el('button', { type: 'button', class: 'dm-mini', onclick: () => upload([{ name: e.file, raw }]) },
        S.files.some(f => f.name === e.file) ? 'Upload again' : 'Upload') : '')));
  }
  $('dm-catalog').hidden = false;
  $('dm-catalog-list').replaceChildren(el('tr', {}, el('th', {}, 'Mod'), el('th', {}, 'Where'), el('th', {}, 'On this build'), el('th', {}, '')), ...rows);
}

// ---- wiring ----------------------------------------------------------------------------------------------------

$('dm-connect-btn').addEventListener('click', connect);
$('dm-save-set').addEventListener('click', saveSet);
$('dm-pick').addEventListener('change', async e => { const f = await readFiles(e.target.files); e.target.value = ''; upload(f); });
const drop = $('dm-drop');
drop.addEventListener('dragover', e => { e.preventDefault(); drop.classList.add('dm-over'); });
drop.addEventListener('dragleave', () => drop.classList.remove('dm-over'));
drop.addEventListener('drop', async e => { e.preventDefault(); drop.classList.remove('dm-over'); upload(await readFiles(e.dataTransfer.files)); });
drop.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); $('dm-pick').click(); } });
let started = false;
/** The view is open: ask for MIDI the first time (the browser's prompt comes then, not when the page loads). */
export function open() {
  if (started) return;
  started = true;
  listPorts();
}
