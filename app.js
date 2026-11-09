const $ = (sel, el = document) => el.querySelector(sel);
const uid = () => Math.random().toString(36).slice(2, 10);
const STORE = 'album-lite/v1';

// The Electron shell is optional. Without it this page runs straight from file://,
// trading real file paths for blob URLs and the save dialog for the browser's print dialog.
const api = window.api ?? null;
const IS_APP = !!api;

const state = {
  size: 2,
  bleed: 0.125,
  gutter: 0.15,
  perSpread: 4,
  photos: [],   // { id, key, url, aspect } — url null means "saved album, file not re-imported yet"
  spreads: [],  // { tpl, items: [{ pid, pos: [x%, y%] }] }
};

// Id lookup index, rebuilt lazily; call reindex() whenever state.photos changes.
let photoIndex = null;
const reindex = () => { photoIndex = null; };
const photo = (pid) => (photoIndex ??= new Map(state.photos.map((p) => [p.id, p]))).get(pid);
const page = () => PAGE_SIZES[state.size];
const spreadSize = () => ({ W: page().w * 2, H: page().h });
const sheetSize = () => {
  const { W, H } = spreadSize();
  return { SW: W + state.bleed * 2, SH: H + state.bleed * 2 };
};
const label = (p) => p.key.split('|')[0].split(/[\\/]/).pop();

/* --- geometry ------------------------------------------------------------ */

// Slot rect (fractions of trim) -> inches on the sheet. Slots touching a trim edge
// are pushed out into the bleed so nothing shows white after the lab trims; the gutter
// is subtracted from the geometry rather than applied as padding, so a slot's box is
// exactly the area the photo fills.
function slotBox(rect) {
  const { W, H } = spreadSize();
  const b = state.bleed;
  const L = rect.x <= 0.001, T = rect.y <= 0.001;
  const R = rect.x + rect.w >= 0.999, B = rect.y + rect.h >= 0.999;
  const g = state.gutter / 2;
  const [pt, pr, pb, pl] = [T ? 0 : g, R ? 0 : g, B ? 0 : g, L ? 0 : g];
  return {
    left: b + rect.x * W - (L ? b : 0) + pl,
    top: b + rect.y * H - (T ? b : 0) + pt,
    width: rect.w * W + (L ? b : 0) + (R ? b : 0) - pl - pr,
    height: rect.h * H + (T ? b : 0) + (B ? b : 0) - pt - pb,
  };
}

const templateOf = (spread) => TEMPLATES[spread.items.length][spread.tpl] || TEMPLATES[spread.items.length][0];

function refit(spread) {
  const { W, H } = spreadSize();
  spread.tpl = pickTemplate(spread.items.map((it) => photo(it.pid)?.aspect ?? 1), W, H);
}

/* --- render -------------------------------------------------------------- */

// The tray only needs rebuilding when the photo list changes; layout edits skip it.
function render(tray = true) {
  const { SW, SH } = sheetSize();
  $('#pagerule').textContent = `@page { size: ${SW}in ${SH}in; margin: 0 }`;
  if (tray) renderTray();
  renderSheets();
  renderOutline();
  markUnused();
}

// Rebuild just the spreads that changed (and their outline entries).
function refreshSpreads(indices) {
  measureHeader();
  const z = previewZoom();
  for (const si of new Set(indices)) {
    const sp = state.spreads[si];
    $('#sheets').children[si].replaceWith(buildSpread(sp, si, z));
    $('#outline').children[si].replaceWith(buildOutlineItem(sp, si));
  }
  markUnused();
}

function markUnused() {
  const used = new Set(state.spreads.flatMap((s) => s.items.map((i) => i.pid)));
  for (const d of $('#thumbs').children) d.classList.toggle('unused', !used.has(d.dataset.id));
}

function renderTray() {
  const missing = state.photos.filter((p) => !p.url).length;
  $('#trayEmpty').hidden = state.photos.length > 0;
  $('#thumbs').replaceChildren(...state.photos.map((p) => {
    const d = document.createElement('div');
    d.className = 'thumb' + (p.url ? '' : ' missing');
    d.dataset.id = p.id;
    d.draggable = !!p.url;
    d.title = p.url ? p.key : `${label(p)} — re-import this file to restore it`;
    d.ondragstart = (e) => e.dataTransfer.setData('text/plain', `p:${p.id}`);
    if (p.url) {
      // Thumbnails are small cached copies; the full-size file is never decoded for the tray.
      const img = new Image();
      img.dataset.pid = p.id;
      img.loading = 'lazy';
      img.decoding = 'async';
      img.alt = '';
      paintThumb(img, p);
      d.append(img);
    } else {
      d.append(Object.assign(document.createElement('span'), { textContent: label(p) }));
    }
    const remove = btn('✕', `Remove ${label(p)} from album`, (e) => {
      e.stopPropagation();
      removePhoto(p);
    });
    remove.className = 'thumb-remove';
    d.append(remove);
    return d;
  }));
  if (missing) status(`${missing} photo${missing > 1 ? 's' : ''} missing — re-import to restore this album.`);
}

const previewZoom = () => Math.min(1, ($('#sheets').clientWidth - 44) / (sheetSize().SW * 96));

// content-visibility skips layout/paint of off-screen spreads; this reserves their scroll height.
// Rebuilt spreads forget the size the browser remembered for them, so the estimate has to be
// exact or the scroll position drifts. The header's real height (plus its margin) is measured
// from any spread that is currently laid out.
let headerHeight = 31;
function measureHeader() {
  const h = [...document.querySelectorAll('#sheets header')].find((el) => el.offsetHeight > 0);
  if (h) headerHeight = h.offsetHeight + parseFloat(getComputedStyle(h).marginBottom);
}

function reserveSize(wrap, zoom) {
  const { SW, SH } = sheetSize();
  wrap.style.containIntrinsicSize = `auto ${SW * 96 * zoom}px auto ${SH * 96 * zoom + headerHeight}px`;
}

// Replacing every spread must not move the viewport (e.g. when inserting a spread mid-album).
function renderSheets() {
  const box = $('#sheets');
  const top = box.scrollTop;
  measureHeader();
  const z = previewZoom();
  box.replaceChildren(...state.spreads.map((sp, si) => buildSpread(sp, si, z)));
  box.scrollTop = top;
}

function buildSpread(sp, si, zoom) {
  const { SW, SH } = sheetSize();
  const wrap = document.createElement('div');
  wrap.className = 'sheet-wrap';
  reserveSize(wrap, zoom);

  const head = document.createElement('header');
  head.className = 'no-print';
  const n = TEMPLATES[sp.items.length].length;
  head.innerHTML = `<span>Spread ${si + 1}</span>`;
  head.append(
    btn('◀', 'Previous layout', () => { sp.tpl = (sp.tpl + n - 1) % n; commit(si); }),
    Object.assign(document.createElement('span'), { textContent: `layout ${sp.tpl + 1}/${n}` }),
    btn('▶', 'Next layout', () => { sp.tpl = (sp.tpl + 1) % n; commit(si); }),
    btn('Best fit', 'Re-pick layout for these photos', () => { refit(sp); commit(si); }),
    btn('+ Photo', 'Add a photo slot', () => {
      if (sp.items.length >= MAX_PER_SPREAD) return;
      sp.items.push({ pid: null, pos: [50, 50], zoom: 1 });
      refit(sp);
      commit(si);
    }),
    btn('+ Spread after', 'Insert a new empty spread after this one', () => {
      state.spreads.splice(si + 1, 0, {
        tpl: 0,
        items: [{ pid: null, pos: [50, 50], zoom: 1 }],
      });
      commit();
    }),
    btn('Delete spread', 'Delete spread', () => { state.spreads.splice(si, 1); commit(); }),
  );

  const sheet = document.createElement('section');
  sheet.className = 'sheet';
  sheet.style.cssText = `width:${SW}in;height:${SH}in;zoom:${zoom}`;
  sheet.style.setProperty('--z', zoom);
  templateOf(sp).forEach((rect, ii) => sheet.append(slotEl(sp, si, ii, rect)));

  wrap.append(head, sheet);
  return wrap;
}

// Window resizes only change the preview scale, so re-scale in place instead of rebuilding.
function applyZoom() {
  measureHeader();
  const z = previewZoom();
  for (const wrap of $('#sheets').children) {
    const sheet = wrap.lastElementChild;
    sheet.style.zoom = z;
    sheet.style.setProperty('--z', z);
    reserveSize(wrap, z);
  }
}

function btn(label, title, onclick) {
  const b = document.createElement('button');
  b.textContent = label;
  b.title = title;
  b.setAttribute('aria-label', title);
  b.onclick = onclick;
  return b;
}

function slotEl(sp, si, ii, rect) {
  const item = sp.items[ii];
  const p = photo(item.pid);
  const box = slotBox(rect);
  const el = document.createElement('div');
  el.className = 'slot';
  el.style.cssText =
    `left:${box.left}in;top:${box.top}in;width:${box.width}in;height:${box.height}in`;

  const ui = document.createElement('div');
  ui.className = 'slot-ui no-print';
  const grab = btn('⠿', 'Drag to swap with another slot', null);
  grab.className = 'grab';
  grab.draggable = true;
  grab.ondragstart = (e) => e.dataTransfer.setData('text/plain', `s:${si}:${ii}`);

  if (p?.url) {
    const img = new Image();
    img.src = p.url;
    img.loading = 'lazy';
    img.decoding = 'async';
    img.alt = '';
    img.draggable = false;

    const warn = document.createElement('div');
    warn.className = 'dpi no-print';
    const readout = document.createElement('span');
    readout.className = 'zoom-read';

    // Size the photo to its cover box, then slide it by the share of the overflow the
    // user has panned to. pos 0..100 keeps the meaning object-position had, and at
    // zoom 1 an axis with no overflow simply can't move — no gaps, no clamping needed.
    const paint = () => {
      const { fw, fh } = coverFit(p.aspect, box.width / box.height);
      const [zw, zh] = [fw * item.zoom, fh * item.zoom];
      img.style.width = `${zw * 100}%`;
      img.style.height = `${zh * 100}%`;
      img.style.left = `${-(zw - 1) * item.pos[0]}%`;
      img.style.top = `${-(zh - 1) * item.pos[1]}%`;

      const dpi = p.w ? fitDPI(p.w, box.width, fw, item.zoom) : null;
      warn.hidden = !(dpi && dpi < MIN_DPI);
      warn.textContent = dpi ? `${Math.round(dpi)} DPI` : '';
      warn.title = `Prints at about ${Math.round(dpi)} DPI — below ${MIN_DPI} looks soft. ` +
        `Zoom out, or use this photo in a smaller slot.`;
      readout.textContent = `${Math.round(item.zoom * 100)}%`;
      readout.title = 'Zoom. Double-click the photo to reset it.';
    };

    const zoomBy = (factor) => {
      item.zoom = Math.min(MAX_ZOOM, Math.max(1, item.zoom * factor));
      paint();
      saveSoon();
    };

    img.onpointerdown = (e) => startPan(e, img, item, paint);
    img.ondblclick = () => { item.pos = [50, 50]; item.zoom = 1; paint(); save(); };
    // Ctrl+wheel is also what a trackpad pinch reports, so pinch-to-zoom works for free.
    el.onwheel = (e) => {
      if (!e.ctrlKey) return;
      e.preventDefault();
      zoomBy(Math.exp(-e.deltaY / 300));
    };

    ui.append(
      btn('−', 'Zoom out', () => zoomBy(1 / 1.25)),
      readout,
      btn('+', 'Zoom in (or hold Ctrl and scroll)', () => zoomBy(1.25)),
      grab,
    );
    el.append(img, warn);
    paint();
  } else if (p) {
    el.classList.add('empty');
    el.append(Object.assign(document.createElement('span'), { textContent: label(p) }));
    ui.append(grab);
    } else {
    el.classList.add('empty');
    el.append(Object.assign(document.createElement('span'), { textContent: 'Drop a photo here' }));
    ui.append(grab);
  }

  ui.append(btn('✕', 'Remove photo from this slot', () => {
    sp.items.splice(ii, 1);
    if (!sp.items.length) {
      state.spreads.splice(si, 1);
      commit();
    } else {
      refit(sp);
      commit(si);
    }
  }));
  el.append(ui);

  el.ondragover = (e) => { e.preventDefault(); el.classList.add('over'); };
  el.ondragleave = () => el.classList.remove('over');
  el.ondrop = (e) => {
    e.preventDefault();
    el.classList.remove('over');
    const [kind, a, b] = e.dataTransfer.getData('text/plain').split(':');
    if (kind === 'o') return;
    const changed = [si];
    if (kind === 'p') {
      Object.assign(item, { pid: a, pos: [50, 50], zoom: 1 });
    } else {
      // Framing belongs to the photo, so it travels with it.
      const other = state.spreads[+a].items[+b];
      [item.pid, other.pid] = [other.pid, item.pid];
      [item.pos, other.pos] = [other.pos, item.pos];
      [item.zoom, other.zoom] = [other.zoom, item.zoom];
      changed.push(+a);
    }
    commit(...changed);
  };
  return el;
}

// Drag inside a slot to re-frame the crop. Overflow is read off the rendered geometry
// rather than recomputed, so panning stays correct at any zoom or preview scale.
function startPan(e, img, item, paint) {
  e.preventDefault();
  img.setPointerCapture(e.pointerId);
  const over = img.getBoundingClientRect();
  const slot = img.parentElement.getBoundingClientRect();
  const [ovx, ovy] = [over.width - slot.width, over.height - slot.height];
  const start = { x: e.clientX, y: e.clientY, pos: [...item.pos] };
  const move = (ev) => {
    item.pos = [
      ovx > 0.5 ? clamp(start.pos[0] - ((ev.clientX - start.x) / ovx) * 100) : item.pos[0],
      ovy > 0.5 ? clamp(start.pos[1] - ((ev.clientY - start.y) / ovy) * 100) : item.pos[1],
    ];
    paint();
  };
  const up = () => {
    img.removeEventListener('pointermove', move);
    img.removeEventListener('pointerup', up);
    save();
  };
  img.addEventListener('pointermove', move);
  img.addEventListener('pointerup', up);
}

const clamp = (v) => Math.max(0, Math.min(100, v));
// commit(i, j…) re-renders only those spreads; commit() rebuilds every spread (use it
// when spreads are added, removed or reordered, since handlers capture their index).
const commit = (...changed) => {
  if (changed.length) refreshSpreads(changed);
  else render(false);
  saveSoon();
};
const status = (msg) => { $('#status').textContent = msg; };

function release(p) {
  for (const u of [p.url, p.thumb]) if (u?.startsWith('blob:')) URL.revokeObjectURL(u);
}

function removePhoto(p) {
  state.photos = state.photos.filter((item) => item.id !== p.id);
  reindex();
  state.spreads = state.spreads.filter((spread) => {
    const before = spread.items.length;
    spread.items = spread.items.filter((item) => item.pid !== p.id);
    if (!spread.items.length) return false;
    if (spread.items.length !== before) refit(spread);
    return true;
  });
  release(p);
  thumbDB.del(p.key);
  $('#thumbs').querySelector(`[data-id="${p.id}"]`)?.remove();
  $('#trayEmpty').hidden = state.photos.length > 0;
  commit();
  status(`${label(p)} removed from album.`);
}

// Zooming fires far faster than it's worth serialising the album for.
let saveTimer;
const saveSoon = () => { clearTimeout(saveTimer); saveTimer = setTimeout(save, 300); };

/* --- thumbnails ---------------------------------------------------------- */

// Trays and the outline show ~320px copies instead of decoding full-size originals.
const THUMB_PX = 320;
const IMPORT_CONCURRENCY = 4;

async function mapLimit(items, limit, fn) {
  const out = new Array(items.length);
  let next = 0;
  await Promise.all(Array.from({ length: Math.min(limit, items.length) }, async () => {
    while (next < items.length) {
      const i = next++;
      out[i] = await fn(items[i], i);
    }
  }));
  return out;
}

// Thumbnail cache, keyed by photo key, so restored albums don't re-decode every original.
// Any failure (no IndexedDB, quota) just means no cache.
const thumbDB = (() => {
  let opened;
  const open = () => (opened ??= new Promise((res, rej) => {
    const rq = indexedDB.open('album-lite', 1);
    rq.onupgradeneeded = () => rq.result.createObjectStore('thumbs');
    rq.onsuccess = () => res(rq.result);
    rq.onerror = () => rej(rq.error);
  }));
  const run = async (mode, op) => {
    try {
      const store = (await open()).transaction('thumbs', mode).objectStore('thumbs');
      return await new Promise((res, rej) => {
        const rq = op(store);
        rq.onsuccess = () => res(rq.result);
        rq.onerror = () => rej(rq.error);
      });
    } catch { return undefined; }
  };
  return {
    get: (k) => run('readonly', (s) => s.get(k)),
    put: (k, blob) => blob && run('readwrite', (s) => s.put(blob, k)),
    del: (k) => run('readwrite', (s) => s.delete(k)),
    clear: () => run('readwrite', (s) => s.clear()),
  };
})();

async function thumbBlob(bitmap) {
  const s = Math.min(1, THUMB_PX / Math.max(bitmap.width, bitmap.height));
  const w = Math.max(1, Math.round(bitmap.width * s));
  const h = Math.max(1, Math.round(bitmap.height * s));
  const canvas = new OffscreenCanvas(w, h);
  const ctx = canvas.getContext('2d');
  ctx.imageSmoothingQuality = 'high';
  ctx.drawImage(bitmap, 0, 0, w, h);
  return canvas.convertToBlob({ type: 'image/jpeg', quality: 0.8 });
}

// Thumb consumers carry data-pid so they can be filled in when the thumbnail arrives.
function paintThumb(el, p) {
  if (!p.thumb) return ensureThumb(p);
  if (el instanceof HTMLImageElement) el.src = p.thumb;
  else el.style.backgroundImage = `url("${p.thumb}")`;
}

function setThumb(p, blob) {
  if (p.thumb?.startsWith('blob:')) URL.revokeObjectURL(p.thumb);
  p.thumb = blob ? URL.createObjectURL(blob) : p.url;
  document.querySelectorAll(`[data-pid="${p.id}"]`).forEach((el) => paintThumb(el, p));
}

const jobs = [];
let running = 0;
function pump() {
  while (running < 3 && jobs.length) {
    running++;
    jobs.shift()().finally(() => { running--; pump(); });
  }
}
const enqueue = (job) => { jobs.push(job); pump(); };

// Photos restored from a saved album have no thumbnail yet: use the cache, else build one.
function ensureThumb(p) {
  if (p.thumb || p.thumbBusy || !p.url) return;
  p.thumbBusy = true;
  enqueue(async () => {
    let blob = await thumbDB.get(p.key);
    if (!blob && IS_APP) {
      try {
        const bitmap = await createImageBitmap(new Blob([await api.readFile(p.key)]));
        blob = await thumbBlob(bitmap);
        bitmap.close();
        thumbDB.put(p.key, blob);
      } catch { /* fall back to the full-size file below */ }
    }
    p.thumbBusy = false;
    if (photo(p.id) === p) setThumb(p, blob);
  });
}

/* --- import & persistence ------------------------------------------------ */

// Decode each file once: its natural pixel size decides print resolution once a photo
// is zoomed, and the same decode yields the thumbnail.
async function decodeFile(f) {
  try {
    const bitmap = await createImageBitmap(f);
    const dims = { aspect: bitmap.width / bitmap.height || 1, w: bitmap.width, h: bitmap.height };
    const thumb = await thumbBlob(bitmap).catch(() => null);
    bitmap.close();
    return { dims, thumb };
  } catch {
    return { dims: { aspect: 1, w: 0, h: 0 }, thumb: null };
  }
}

// A file already referenced by the restored album fills in its blank; anything else is new.
// Same key twice is never two photos — drag a thumbnail onto a second slot to reuse one.
async function addFiles(files) {
  const { W, H } = spreadSize();
  const byKey = new Map(state.photos.map((p) => [p.key, p]));
  const seen = new Set();
  const todo = [];
  for (const f of files) {
    if (!f.type.startsWith('image/')) continue;
    const key = IS_APP ? api.pathForFile(f) : `${f.name}|${f.size}`;
    if (seen.has(key) || byKey.get(key)?.url) continue;
    seen.add(key);
    todo.push({ f, key, held: byKey.get(key) });
  }

  let done = 0;
  const decoded = await mapLimit(todo, IMPORT_CONCURRENCY, async ({ f }) => {
    const r = await decodeFile(f);
    status(`Importing ${++done}/${todo.length}…`);
    return r;
  });

  // Results are applied in selection order so chronology survives parallel decoding.
  const fresh = [];
  todo.forEach(({ f, key, held }, i) => {
    const { dims, thumb } = decoded[i];
    const url = IS_APP ? api.urlForPath(key) : URL.createObjectURL(f);
    const p = held ?? { id: uid(), key };
    Object.assign(p, { url, ...dims });
    setThumb(p, thumb);
    thumbDB.put(key, thumb);
    if (!held) {
      state.photos.push(p);
      fresh.push(p);
    }
  });
  reindex();
  // Only new photos get new spreads; existing spreads keep whatever you arranged.
  state.spreads.push(...flowSpreads(fresh, state.perSpread, W, H));
  render();
  saveSoon();
  status(`${state.photos.length} photos, ${state.spreads.length} spreads`);
}

function rebuild() {
  const { W, H } = spreadSize();
  state.spreads = flowSpreads(state.photos, state.perSpread, W, H);
}

function save() {
  const { size, bleed, gutter, perSpread, spreads } = state;
  try {
    localStorage.setItem(STORE, JSON.stringify({
      size, bleed, gutter, perSpread, spreads,
      photos: state.photos.map(({ id, key, aspect, w, h }) => ({ id, key, aspect, w, h })),
    }));
  } catch { /* ponytail: no storage (or quota hit) just means no autosave. */ }
}

function restore() {
  try {
    const saved = JSON.parse(localStorage.getItem(STORE) || 'null');
    if (!saved) return;
    Object.assign(state, saved);
    // Electron knows the real paths, so it reloads photos outright; a browser page
    // only holds blob URLs that died with the last session, so it waits for a re-import.
    state.photos = saved.photos.map((p) => ({ ...p, url: IS_APP ? api.urlForPath(p.key) : null }));
    reindex();
    state.spreads.forEach((s) => s.items.forEach((i) => { i.zoom ??= 1; }));
  } catch {
    try { localStorage.removeItem(STORE); } catch { /* ignore */ }
  }
}

/* --- wiring -------------------------------------------------------------- */

function initControls() {
  $('#size').replaceChildren(...PAGE_SIZES.map((s, i) =>
    new Option(`${s.name} page  ·  ${s.w * 2} x ${s.h} spread`, i)));
  $('#perSpread').replaceChildren(...Array.from({ length: MAX_PER_SPREAD }, (_, i) =>
    new Option(String(i + 1), i + 1)));

  $('#size').value = state.size;
  $('#perSpread').value = state.perSpread;
  $('#bleed').value = state.bleed;
  $('#gutter').value = state.gutter;

  $('#size').onchange = (e) => { state.size = +e.target.value; state.spreads.forEach(refit); commit(); };
  $('#perSpread').onchange = (e) => { state.perSpread = +e.target.value; rebuild(); commit(); };
  $('#bleed').onchange = (e) => { state.bleed = Math.max(0, +e.target.value || 0); commit(); };
  $('#gutter').onchange = (e) => { state.gutter = Math.max(0, +e.target.value || 0); commit(); };

  $('#import').onclick = () => $('#file').click();
  $('#file').onchange = (e) => { addFiles([...e.target.files]); e.target.value = ''; };
  $('#auto').onclick = () => { rebuild(); commit(); };
  $('#clear').onclick = () => {
    if (!confirm('Remove all photos and spreads?')) return;
    state.photos.forEach(release);
    state.photos = [];
    state.spreads = [];
    reindex();
    thumbDB.clear();
    render();
    saveSoon();
    status('');
  };
  $('#export').onclick = exportPDF;

  let resizeFrame;
  addEventListener('resize', () => {
    cancelAnimationFrame(resizeFrame);
    resizeFrame = requestAnimationFrame(applyZoom);
  });
  addEventListener('beforeunload', save);
  addEventListener('dragover', (e) => e.preventDefault());
  addEventListener('drop', (e) => {
    if (e.dataTransfer.files.length) { e.preventDefault(); addFiles([...e.dataTransfer.files]); }
  });
}

/** Photos that would print soft at their current crop, as [label, dpi] pairs. */
function lowRes() {
  const out = [];
  for (const sp of state.spreads) {
    templateOf(sp).forEach((rect, ii) => {
      const p = photo(sp.items[ii]?.pid);
      if (!p?.w) return;
      const box = slotBox(rect);
      const { fw } = coverFit(p.aspect, box.width / box.height);
      const dpi = fitDPI(p.w, box.width, fw, sp.items[ii].zoom);
      if (dpi < MIN_DPI) out.push([label(p), Math.round(dpi)]);
    });
  }
  return out;
}

async function exportPDF() {
  if (!state.spreads.length) return status('Nothing to export.');

  const soft = lowRes();
  if (soft.length && !confirm(
    `${soft.length} photo${soft.length > 1 ? 's' : ''} will print below ${MIN_DPI} DPI and may look soft:\n\n` +
    soft.slice(0, 8).map(([n, d]) => `  ${n} — ${d} DPI`).join('\n') +
    (soft.length > 8 ? `\n  …and ${soft.length - 8} more` : '') +
    '\n\nZoom those out, or export anyway?')) {
    return status(`Export cancelled — ${soft.length} photo(s) below ${MIN_DPI} DPI.`);
  }

  // Off-screen spreads are lazy and skipped by layout; force them all in before printing,
  // and decode in batches so hundreds of full-size photos aren't held at once.
  document.body.classList.add('exporting');
  try {
    status('Decoding images at full resolution…');
    const imgs = [...document.querySelectorAll('.sheet img')];
    imgs.forEach((i) => { i.loading = 'eager'; });
    for (let i = 0; i < imgs.length; i += 8) {
      await Promise.all(imgs.slice(i, i + 8).map((im) => im.decode().catch(() => { })));
      status(`Decoding images… ${Math.min(i + 8, imgs.length)}/${imgs.length}`);
    }
    const { SW, SH } = sheetSize();
    const size = `${SW} x ${SH} in (incl. ${state.bleed} in bleed)`;

    if (!IS_APP) {
      status(`Choose "Save as PDF" — the preview should read ${size}.`);
      return print();
    }
    const saved = await api.exportPDF({ suggestedName: `album-${page().w}x${page().h}.pdf` });
    status(saved ? `Saved ${saved} · ${state.spreads.length} spreads at ${size}` : 'Export cancelled.');
  } finally {
    document.body.classList.remove('exporting');
  }
}

restore();
initControls();
render();
function renderOutline() {
  $('#outline').replaceChildren(...state.spreads.map(buildOutlineItem));
}

function buildOutlineItem(sp, si) {
  const { W, H } = spreadSize();
  const item = document.createElement('div');
  item.className = 'o-item';
  item.draggable = true;
  item.title = 'Click to jump to this spread, drag to reorder';

  const mini = document.createElement('div');
  mini.className = 'o-mini';
  mini.style.aspectRatio = `${W} / ${H}`;
  templateOf(sp).forEach((rect, ii) => {
    const p = photo(sp.items[ii]?.pid);
    const s = document.createElement('div');
    s.style.cssText = `left:${rect.x * 100}%;top:${rect.y * 100}%;width:${rect.w * 100}%;height:${rect.h * 100}%`;
    if (p?.url) {
      s.dataset.pid = p.id;
      paintThumb(s, p);
    }
    mini.append(s);
  });

  const num = document.createElement('span');
  num.textContent = si + 1;
  item.append(mini, num);

  item.onclick = () =>
    $('#sheets').children[si]?.scrollIntoView({ behavior: 'smooth', block: 'start' });
  item.ondragstart = (e) => e.dataTransfer.setData('text/plain', `o:${si}`);
  item.ondragover = (e) => { e.preventDefault(); item.classList.add('over'); };
  item.ondragleave = () => item.classList.remove('over');
  item.ondrop = (e) => {
    e.preventDefault();
    item.classList.remove('over');
    const [kind, from] = e.dataTransfer.getData('text/plain').split(':');
    if (kind !== 'o') return;
    const [moved] = state.spreads.splice(+from, 1);
    state.spreads.splice(si, 0, moved);
    commit();
  };
  return item;
}