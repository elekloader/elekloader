// SPDX-License-Identifier: GPL-2.0-or-later
// The +Drive mods view (#drive): a unit's /mods folder over Web MIDI (docs/WEB.md, "The +Drive mods view"). app.js
// imports this module the first time the view opens, and calls open() each time it does. The engine is
// js/'s TypeScript as the site serves it (engine/): the build-checker (drive.js), the data container (container.js)
// and the Data router's requests (datarpc.js), each the Python's, byte for byte. Every write goes through
// datarpc.Mods, whose paths are inside /mods; an upload and the set are checked against the unit's build first.
// /mods shows one folder at a time (to datarpc.DEPTH deep): its path on top, each part of which opens that folder, then its
// folders and files. Files dropped on a folder row or a part of the path go into that folder, and a row dragged there moves
// there (datarpc.Mods.move); Rename renames a row in place. The view only puts files on the
// unit: the build (/mods/active) is chosen on the unit's MODS page, and a mod in it can't be deleted here (MOD IN USE).

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
const SEG_MAX = 63;                       // a folder or file name, in bytes (the loader's DL_SEG_MAX)
const ulen = s => new TextEncoder().encode(s).length;
const depth = p => (p ? p.split('/').length : 0);
const parent = p => p.split('/').slice(0, -1).join('/');
const base = p => p.split('/').pop();
const inside = (p, dir) => (dir ? p.startsWith(dir + '/') : true);
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
  files: [],            // [{name, size, raw, id, version, alone (a check result)}], name a path below /mods
  folders: [],          // the unit's folders below /mods, as paths, to datarpc.DEPTH deep
  cwd: '',              // the open folder, '' for /mods itself
  shop: null,           // the shop's mods for this build: [{e, raw, kind, why}]
  inUse: new Set(),     // the files /mods/active names: the saved build, which the view never deletes
  busy: false,
};

async function busy(f) {
  if (S.busy) return;
  S.busy = true;
  $('view-drive').dataset.busy = '1';                    // for tests: a request is under way
  const held = [...$('view-drive').querySelectorAll('button')].filter(b => !b.disabled);
  for (const b of held) b.disabled = true;
  try { return await f(); } finally {
    S.busy = false;
    delete $('view-drive').dataset.busy;
    for (const b of held) b.disabled = false;   // a button disabled for its own reason stays so
    if (S.build && !$('dm-files').hidden) renderCrumbs();
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

/** The room: the area, and the most one upload or read takes now, the live stage of the unit's .status (with the
 * stage at the area's top, the free tail above the set the last boot loaded; it changes with each boot's set). */
function renderRoom() {
  const b = S.build, room = S.room ?? drive.uploadRoom(b, null);
  $('dm-room').replaceChildren(
    el('dt', {}, '+Drive area'), el('dd', {}, `${kb(b.hi - b.lo)} (${hex8(b.lo)}..${hex8(b.hi)})`),
    ...(b.stageTop ? [el('dt', {}, 'Largest file the boot links'), el('dd', {}, kb(b.hi - b.lo))] : []),
    el('dt', {}, b.stageTop ? 'Largest upload now' : 'Largest file'),
    el('dd', {}, room ? kb(room) : 'no limit given',
      b.stageTop ? el('div', { class: 'muted small' }, 'the free room above the mods the unit loaded at its last boot') : ''));
}

// ---- /mods -----------------------------------------------------------------------------------------------------

/** Every folder and file below /mods, to datarpc.DEPTH folders deep, one listing a folder, as datarpc.Mods.walk
 * reads them, but keeping the folders, so that an empty one shows. -> [[path, size]], [path] */
async function walk() {
  const files = [], folders = [], todo = [''];
  while (todo.length) {
    const sub = todo.shift();
    for (const [name, kind, size] of await S.mods.ls(sub)) {
      const p = sub ? `${sub}/${name}` : name;
      if (kind !== 1) files.push([p, size]);
      else if (depth(p) <= datarpc.DEPTH) { folders.push(p); todo.push(p); }
    }
  }
  return [files, folders];
}

async function refresh() {
  const [list, folders] = await walk();
  S.folders = folders;
  while (S.cwd && !folders.includes(S.cwd)) S.cwd = parent(S.cwd);
  const files = [];
  for (const [name, size] of list) {
    if (SPECIAL.has(name)) continue;
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
  // the saved build, /mods/active, only so that its mods are not deleted from here
  let active = [];
  try {
    const [, body] = await S.mods.get('active');
    active = drive.parseActive(new TextDecoder().decode(body)) || [];
  } catch (e) {
    if (!(e instanceof datarpc.RpcError)) throw e;          // none yet
  }
  S.inUse = new Set(active.map(([n]) => n));
  let status = '', read = false;
  try { status = new TextDecoder().decode((await S.mods.get('.status'))[1]); read = true; } catch (e) { if (!(e instanceof datarpc.RpcError)) throw e; }
  S.room = drive.uploadRoom(S.build, read ? status : null);   // the live stage
  renderRoom();
  $('dm-status').hidden = false;
  $('dm-status-text').textContent = status.trim() || 'Nothing loaded at the last boot.';
  $('dm-files').hidden = false;
  renderFiles();
  renderShop();
}

/** The unit's folders below /mods. */
function allFolders() { return [...S.folders].sort(); }

function open_(dir) { S.cwd = dir; say('dm-upload-msg', ''); renderFiles(); renderShop(); }

function renderCrumbs() {
  const parts = S.cwd ? S.cwd.split('/') : [];
  const crumbs = [el('button', { type: 'button', 'data-dir': '', 'aria-current': parts.length ? null : 'location', onclick: () => open_(''), ...dropTarget('') }, '/mods')];
  parts.forEach((p, k) => {
    const dir = parts.slice(0, k + 1).join('/');
    crumbs.push('/', el('button', { type: 'button', 'data-dir': dir, 'aria-current': k === parts.length - 1 ? 'location' : null, onclick: () => open_(dir), ...dropTarget(dir) }, p));
  });
  $('dm-crumbs').replaceChildren(...crumbs);
  $('dm-into').textContent = S.cwd ? `/mods/${S.cwd}` : '/mods';
  const deep = depth(S.cwd) >= datarpc.DEPTH;
  $('dm-new-folder').disabled = deep;
  $('dm-new-folder-btn').disabled = deep;
  $('dm-new-folder-btn').title = deep ? `Folders go ${datarpc.DEPTH} deep below /mods` : '';
}

const DRAG = 'application/x-elekloader-path';   // a row dragged within the view: its path below /mods

/** Drag-and-drop handlers for a folder (`dir`, or a function giving the open one): files from the computer upload into
 * it, and a row of the view dragged onto it moves there. */
function dropTarget(dir) {
  const where = () => (typeof dir === 'function' ? dir() : dir);
  const ours = e => [...e.dataTransfer.types].includes(DRAG);
  return {
    ondragover: e => {
      const types = [...e.dataTransfer.types];
      if (!types.includes('Files') && !ours(e)) return;
      e.preventDefault(); e.stopPropagation();
      e.dataTransfer.dropEffect = ours(e) ? 'move' : 'copy';
      e.currentTarget.classList.add('dm-over');
    },
    ondragleave: e => e.currentTarget.classList.remove('dm-over'),
    ondrop: async e => {
      e.preventDefault(); e.stopPropagation(); e.currentTarget.classList.remove('dm-over');
      const into = where();
      if (ours(e)) { moveInto(e.dataTransfer.getData(DRAG), into); return; }
      if (into !== S.cwd) open_(into);
      upload(await readFiles(e.dataTransfer.files, into));
    },
  };
}

/** A row's drag handlers: it carries its path, for a folder row or a part of the path to move it. */
const dragSource = path => ({
  draggable: 'true',
  ondragstart: e => { e.dataTransfer.setData(DRAG, path); e.dataTransfer.effectAllowed = 'move'; },
});

const isDir = p => S.folders.includes(p);
const taken = p => S.folders.includes(p) || S.files.some(f => f.name === p);

/** Why `from` can't move to `to`, or ''. The route checks it all again; this says it before a request. */
function whyNotMove(from, to) {
  const folder = isDir(from);
  if (from === to) return '';
  if (folder && inside(to, from)) return `/mods/${from} can't go into itself`;
  if (taken(to)) return `/mods/${to} is already there`;
  if (to.split('/').some(q => ulen(q) > SEG_MAX)) return `a name is at most ${SEG_MAX} bytes`;
  if (ulen(to) >= 128) return `/mods/${to} is longer than 127 bytes`;
  if (folder) {
    const deepest = Math.max(0, ...S.folders.filter(d => inside(d, from)).map(d => depth(d) - depth(from)));
    if (depth(to) + deepest > datarpc.DEPTH) return `folders go ${datarpc.DEPTH} deep below /mods`;
    if (S.files.some(f => inside(f.name, from) && ulen(to + f.name.slice(from.length)) >= 128))
      return `a path in it would be longer than 127 bytes`;
  }
  return '';
}

/** Move or rename the file or folder `from` to `to` on the unit (datarpc.Mods.move: the route keeps /mods/active's
 * lines naming it). */
async function move(from, to) {
  if (from === to) return;
  const why = whyNotMove(from, to);
  if (why) { say('dm-upload-msg', `REFUSED: ${why}`, 'bad'); return; }
  const folder = isDir(from);
  await busy(async () => {
    try { await S.mods.move(from, to, S.head, folder); } catch (e) { say('dm-upload-msg', `/mods/${from}: ${e.message}`, 'bad'); await refresh(); return; }
    if (folder && (S.cwd === from || inside(S.cwd, from))) S.cwd = to + S.cwd.slice(from.length);
    await refresh();
    say('dm-upload-msg', `Moved /mods/${from} to /mods/${to}.`, 'ok');
  });
}

/** A row dropped on a folder: it moves into it, keeping its name. */
function moveInto(from, dir) {
  if (!from || from === dir || parent(from) === dir) return;   // onto itself, or already there
  move(from, dir ? `${dir}/${base(from)}` : base(from));
}

/** Rename in place: the name cell becomes a text box; Enter renames, Escape or leaving it keeps the name. */
function startRename(path, cell) {
  const folder = isDir(path), old = base(path);
  const box = el('input', { type: 'text', class: 'dm-rename', value: old, 'aria-label': `New name for ${path}`, maxlength: SEG_MAX, spellcheck: 'false' });
  let done = false;
  const finish = ok => {
    if (done) return;
    done = true;
    const name = box.value.trim();
    if (!ok || !name || name === old) { renderFiles(); return; }
    let why = '';
    if (name.includes('/') || name.startsWith('.')) why = 'a name has no / and does not start with .';
    else if (/[\x00-\x1f\x7f]/.test(name)) why = 'a name has no control characters';
    else if (!folder && !/.\.elemod$/.test(name)) why = 'a mod file keeps a .elemod name';
    if (why) { renderFiles(); say('dm-upload-msg', `REFUSED: ${why}`, 'bad'); return; }
    const p = parent(path);
    move(path, p ? `${p}/${name}` : name);
  };
  box.addEventListener('keydown', e => {
    if (e.key === 'Enter') { e.preventDefault(); finish(true); }
    else if (e.key === 'Escape') { e.preventDefault(); finish(false); }
  });
  box.addEventListener('blur', () => finish(false));
  cell.replaceChildren(box);
  box.focus();
  if (!folder) box.setSelectionRange(0, old.length - '.elemod'.length); else box.select();
}

function renderFiles() {
  renderCrumbs();
  const cwd = S.cwd, folders = allFolders();
  const here = folders.filter(d => parent(d) === cwd);
  const files = S.files.filter(f => parent(f.name) === cwd);
  const rows = [];
  for (const d of here) {
    const below = S.files.filter(f => inside(f.name, d));
    const empty = !below.length && !folders.some(x => inside(x, d));
    const name = el('td', {}, el('code', {}, el('button', { type: 'button', class: 'dm-open', 'aria-label': `Open ${d}`, onclick: () => open_(d) }, `/${base(d)}`)));
    rows.push(el('tr', { class: 'dm-folder', 'data-dir': d, ...dropTarget(d), ...dragSource(d) },
      name,
      el('td', { class: 'muted' }, `${below.length} mod${below.length === 1 ? '' : 's'}`),
      el('td', { class: 'dm-num' }, ''),
      el('td', {}, el('button', { type: 'button', class: 'dm-mini', 'aria-label': `Rename folder ${d}`, onclick: () => startRename(d, name) }, 'Rename'), ' ',
        el('button', { type: 'button', class: 'dm-mini dm-danger', 'aria-label': `Delete folder ${d}`, disabled: !empty,
        title: empty ? null : 'Not empty: delete or move what is in it first, as on the unit', onclick: () => removeDir(d) }, 'Delete'))));
  }
  for (const f of files) {
    const name = el('td', { title: `/mods/${f.name}` }, el('code', {}, base(f.name)));
    rows.push(el('tr', { 'data-file': f.name, ...dragSource(f.name) },
      name,
      el('td', { class: f.error ? 'bad' : null }, f.error || (f.id ? `${f.id} ${f.version ?? ''}` : '')),
      el('td', { class: 'dm-num' }, kb(f.size)),
      el('td', {}, el('button', { type: 'button', class: 'dm-mini', disabled: !f.raw, onclick: () => download(base(f.name), f.raw) }, 'Download'), ' ',
        el('button', { type: 'button', class: 'dm-mini', 'aria-label': `Rename ${f.name}`, onclick: () => startRename(f.name, name) }, 'Rename'), ' ',
        el('button', { type: 'button', class: 'dm-mini dm-danger', 'aria-label': `Delete ${f.name}`, disabled: S.inUse.has(f.name),
          title: S.inUse.has(f.name) ? 'In the build: unload it on the unit first (MODS, UNLOAD)' : null, onclick: () => remove(f) }, 'Delete'))));
  }
  if (!here.length && !files.length)
    rows.push(el('tr', {}, el('td', { colspan: 4, class: 'muted' }, cwd ? 'This folder is empty.' : 'No mods in /mods yet.')));
  if (depth(cwd) >= datarpc.DEPTH)
    rows.push(el('tr', {}, el('td', { colspan: 4, class: 'muted small', id: 'dm-deep' },
      `Folders go ${datarpc.DEPTH} deep below /mods. A folder inside this one is not shown here or on the unit, and the loader can't load its mods.`)));
  $('dm-file-list').replaceChildren(
    el('tr', {}, el('th', {}, 'Name'), el('th', {}, 'Mod'), el('th', { class: 'dm-num' }, 'Size'), el('th', {}, '')),
    ...rows);
}

/** Make an empty folder on the unit, in the open one, and open it. The Data router has no command for a folder: the
 * loader's route makes one for an upload of nothing to FOLDER/.dir (datarpc.Mods.mkdir). */
async function makeFolder() {
  const name = $('dm-new-folder').value.trim();
  const path = S.cwd ? `${S.cwd}/${name}` : name;
  let why = '';
  if (!name) why = 'give it a name';
  else if (depth(S.cwd) >= datarpc.DEPTH) why = `folders go ${datarpc.DEPTH} deep below /mods`;
  else if (name.includes('/') || name.startsWith('.')) why = 'a folder name has no / and does not start with .';
  else if (/[\x00-\x1f\x7f]/.test(name)) why = 'a folder name has no control characters';
  else if (ulen(name) > SEG_MAX) why = `a folder name is at most ${SEG_MAX} bytes`;
  else if (ulen(path) + 2 >= 128) why = 'the path would leave no room for a file name (127 bytes)';
  else if (allFolders().includes(path) || S.files.some(f => f.name === path)) why = `${path} is already there`;
  if (why) { say('dm-upload-msg', `REFUSED: ${why}`, 'bad'); return; }
  await busy(async () => {
    try { await S.mods.mkdir(path, S.head); } catch (e) { say('dm-upload-msg', `/mods/${path}: ${e.message}`, 'bad'); await refresh(); return; }
    $('dm-new-folder').value = '';
    await refresh();
    open_(path);
    say('dm-upload-msg', `Made /mods/${path}.`, 'ok');
  });
}

/** Delete the empty folder d at once, with no second press (decided 2026-11-09). */
async function removeDir(d) {
  await busy(async () => {
    try { await S.mods.rm(d); } catch (e) { say('dm-upload-msg', `/mods/${d}: ${e.message}`, 'bad'); await refresh(); return; }
    say('dm-upload-msg', `Deleted /mods/${d}.`, 'ok');
    await refresh();
  });
}

function download(name, raw) {
  const a = el('a', { href: URL.createObjectURL(new Blob([raw], { type: 'application/json' })), download: name });
  document.body.append(a);
  a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
}

/** Delete the file f at once, with no second press (decided 2026-11-09). */
async function remove(f) {
  if (S.inUse.has(f.name)) return;                      // the unit's MOD IN USE: unload it there first
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

/** Check `files` ([{name, raw}], name a path below /mods) against the build, with what they require from /mods, then
 * upload them all, or none. */
async function upload(files) {
  if (!S.build) return;
  await busy(async () => {
    for (const f of files) {
      if (!/\.elemod$/.test(f.name)) { say('dm-upload-msg', `REFUSED: ${f.name} is not an .elemod file; nothing was uploaded`, 'bad'); return; }
      try { S.mods.path(f.name); } catch (e) { say('dm-upload-msg', `REFUSED: ${e.message}; nothing was uploaded`, 'bad'); return; }
      if (ulen(base(f.name)) > SEG_MAX) { say('dm-upload-msg', `REFUSED: ${base(f.name)} is longer than ${SEG_MAX} bytes; nothing was uploaded`, 'bad'); return; }
      if (ulen(f.name) >= 128) { say('dm-upload-msg', `REFUSED: /mods/${f.name} is longer than 127 bytes; nothing was uploaded`, 'bad'); return; }
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
    const over = files.filter(f => S.room && f.raw.length > S.room);   // the unit's live stage
    if (over.length) {
      say('dm-upload-msg', `REFUSED: nothing was uploaded. ${over.map(f => `${f.name} is ${kb(f.raw.length)}; the unit takes up to ${kb(S.room)} now`).join('; ')}`, 'bad');
      return;
    }
    for (const f of files) {
      try {
        await S.mods.put(f.name, f.raw, S.head, (sent, total) => say('dm-upload-msg', `Uploading ${f.name}: ${Math.round(100 * sent / total)}%`));
      } catch (e) { say('dm-upload-msg', `${f.name}: ${e.message}`, 'bad'); await refresh(); return; }
    }
    say('dm-upload-msg', `Uploaded ${files.map(f => S.mods.path(f.name)).join(', ')}. Add ${files.length === 1 ? 'it' : 'them'} to the build on the unit (MODS, LOAD TO BUILD).`, 'ok');
    await refresh();
  });
}

/** Chosen or dropped files, named into a folder (the open one unless given). */
async function readFiles(list, into = S.cwd) {
  return Promise.all([...list].map(async f => ({ name: into ? `${into}/${f.name}` : f.name, raw: new Uint8Array(await f.arrayBuffer()) })));
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
  S.shop = [];
  for (const e of items) {
    let raw = null, kind = 'flash', why = '';
    try {
      raw = new Uint8Array(await (await fetch(site(`./shop/${e.file}`))).arrayBuffer());
      const only = drive.flashOnly(JSON.parse(new TextDecoder().decode(raw)));
      if (only === null) kind = '+Drive'; else why = `uses ${only}`;
    } catch { why = 'not readable'; }
    S.shop.push({ e, raw, kind, why });
  }
  renderShop();
}

/** The shop's rows. A mod already in /mods, in any folder, is uploaded again where it is; a new one goes into the
 * open folder. */
function renderShop() {
  if (!S.shop) return;
  const rows = S.shop.map(({ e, raw, kind, why }) => {
    const c = kind === '+Drive' ? drive.check(S.build, [[e.file, raw]]).choices[0] : null;
    const copy = S.files.find(f => base(f.name) === e.file);
    const to = copy ? copy.name : S.cwd ? `${S.cwd}/${e.file}` : e.file;
    return el('tr', {},
      el('td', {}, e.title || e.id, el('div', { class: 'muted small' }, `${e.id} ${e.version}`)),
      el('td', {}, el('span', { class: `dm-tag ${kind === '+Drive' ? 'dm-drive' : ''}` }, kind), why ? el('div', { class: 'muted small' }, why) : ''),
      el('td', { class: c ? (c.state === drive.LOADED ? 'ok' : 'warn') : 'muted' }, c ? (c.state === drive.LOADED ? 'loads alone' : c.why()) : ''),
      el('td', {}, kind === '+Drive' ? [el('button', { type: 'button', class: 'dm-mini', onclick: () => upload([{ name: to, raw }]) },
        copy ? 'Upload again' : 'Upload'), el('div', { class: 'muted small' }, copy ? `replaces /mods/${to}` : `into /mods${S.cwd ? '/' + S.cwd : ''}`)] : ''));
  });
  $('dm-catalog').hidden = false;
  $('dm-catalog-list').replaceChildren(el('tr', {}, el('th', {}, 'Mod'), el('th', {}, 'Where'), el('th', {}, 'On this build'), el('th', {}, '')), ...rows);
}

// ---- wiring ----------------------------------------------------------------------------------------------------

$('dm-connect-btn').addEventListener('click', connect);
$('dm-new-folder-btn').addEventListener('click', makeFolder);
$('dm-new-folder').addEventListener('keydown', e => { if (e.key === 'Enter') { e.preventDefault(); makeFolder(); } });
$('dm-pick').addEventListener('change', async e => { const f = await readFiles(e.target.files); e.target.value = ''; upload(f); });
const drop = $('dm-drop');
drop.addEventListener('dragover', e => { e.preventDefault(); drop.classList.add('dm-over'); });
drop.addEventListener('dragleave', () => drop.classList.remove('dm-over'));
drop.addEventListener('drop', async e => { e.preventDefault(); drop.classList.remove('dm-over'); upload(await readFiles(e.dataTransfer.files)); });
{ const pane = $('dm-pane'), t = dropTarget(() => S.cwd);
  for (const [k, f] of Object.entries(t)) pane.addEventListener(k.slice(2), f); }
drop.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); $('dm-pick').click(); } });
let started = false;
/** The view is open: ask for MIDI the first time (the browser's prompt comes then, not when the page loads). */
export function open() {
  if (started) return;
  started = true;
  listPorts();
}
