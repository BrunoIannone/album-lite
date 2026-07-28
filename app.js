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

const photo = (pid) => state.photos.find((p) => p.id === pid);
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

function render() {
  const { SW, SH } = sheetSize();
  $('#pagerule').textContent = `@page { size: ${SW}in ${SH}in; margin: 0 }`;

  const used = new Set(state.spreads.flatMap((s) => s.items.map((i) => i.pid)));
  const missing = state.photos.filter((p) => !p.url).length;
  $('#trayEmpty').hidden = state.photos.length > 0;
  $('#thumbs').replaceChildren(...state.photos.map((p) => {
    const d = document.createElement('div');
    d.className = 'thumb' + (used.has(p.id) ? '' : ' unused') + (p.url ? '' : ' missing');
    d.draggable = !!p.url;
    d.title = p.url ? p.key : `${label(p)} — re-import this file to restore it`;
    d.ondragstart = (e) => e.dataTransfer.setData('text/plain', `p:${p.id}`);
    if (p.url) {
      const img = new Image();
      img.src = p.url;
      img.loading = 'lazy';
      img.alt = '';
      d.append(img);
    } else {
      d.textContent = label(p);
    }
    return d;
  }));
  if (missing) status(`${missing} photo${missing > 1 ? 's' : ''} missing — re-import to restore this album.`);

  // ponytail: full re-render on every change. Virtualize if albums pass ~300 photos.
  const zoom = Math.min(1, ($('#sheets').clientWidth - 44) / (SW * 96));
  $('#sheets').replaceChildren(...state.spreads.map((sp, si) => {
    const wrap = document.createElement('div');
    wrap.className = 'sheet-wrap';

    const head = document.createElement('header');
    head.className = 'no-print';
    const n = TEMPLATES[sp.items.length].length;
    head.innerHTML = `<span>Spread ${si + 1}</span>`;
    head.append(
      btn('◀', 'Previous layout', () => { sp.tpl = (sp.tpl + n - 1) % n; commit(); }),
      Object.assign(document.createElement('span'), { textContent: `layout ${sp.tpl + 1}/${n}` }),
      btn('▶', 'Next layout', () => { sp.tpl = (sp.tpl + 1) % n; commit(); }),
      btn('Best fit', 'Re-pick layout for these photos', () => { refit(sp); commit(); }),
      btn('Delete spread', 'Delete spread', () => { state.spreads.splice(si, 1); commit(); }),
    );

    const sheet = document.createElement('section');
    sheet.className = 'sheet';
    sheet.style.cssText = `width:${SW}in;height:${SH}in;zoom:${zoom}`;
    sheet.style.setProperty('--z', zoom);
    templateOf(sp).forEach((rect, ii) => sheet.append(slotEl(sp, si, ii, rect)));

    wrap.append(head, sheet);
    return wrap;
  }));
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
    ui.append(grab);
  }

  ui.append(btn('✕', 'Remove photo from this slot', () => {
    sp.items.splice(ii, 1);
    if (!sp.items.length) state.spreads.splice(si, 1);
    else refit(sp);
    commit();
  }));
  el.append(ui);

  el.ondragover = (e) => { e.preventDefault(); el.classList.add('over'); };
  el.ondragleave = () => el.classList.remove('over');
  el.ondrop = (e) => {
    e.preventDefault();
    el.classList.remove('over');
    const [kind, a, b] = e.dataTransfer.getData('text/plain').split(':');
    if (kind === 'p') {
      Object.assign(item, { pid: a, pos: [50, 50], zoom: 1 });
    } else {
      // Framing belongs to the photo, so it travels with it.
      const other = state.spreads[+a].items[+b];
      [item.pid, other.pid] = [other.pid, item.pid];
      [item.pos, other.pos] = [other.pos, item.pos];
      [item.zoom, other.zoom] = [other.zoom, item.zoom];
    }
    commit();
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
const commit = () => { render(); save(); };
const status = (msg) => { $('#status').textContent = msg; };

// Zooming fires far faster than it's worth serialising the album for.
let saveTimer;
const saveSoon = () => { clearTimeout(saveTimer); saveTimer = setTimeout(save, 300); };

/* --- import & persistence ------------------------------------------------ */

// Natural pixel size decides print resolution once a photo is zoomed, so keep it.
const measure = (url) => new Promise((res) => {
  const im = new Image();
  im.onload = () => res({ aspect: im.naturalWidth / im.naturalHeight || 1, w: im.naturalWidth, h: im.naturalHeight });
  im.onerror = () => res({ aspect: 1, w: 0, h: 0 });
  im.src = url;
});

// A file already referenced by the restored album fills in its blank; anything else is new.
// Same key twice is never two photos — drag a thumbnail onto a second slot to reuse one.
async function addFiles(files) {
  const { W, H } = spreadSize();
  const fresh = [];
  for (const f of files) {
    if (!f.type.startsWith('image/')) continue;
    const key = IS_APP ? api.pathForFile(f) : `${f.name}|${f.size}`;
    const held = state.photos.find((p) => p.key === key);
    if (held?.url) continue;
    const url = IS_APP ? api.urlForPath(key) : URL.createObjectURL(f);
    const dims = await measure(url);
    if (held) {
      Object.assign(held, { url, ...dims });
    } else {
      const p = { id: uid(), key, url, ...dims };
      state.photos.push(p);
      fresh.push(p);
    }
  }
  // Only new photos get new spreads; existing spreads keep whatever you arranged.
  state.spreads.push(...flowSpreads(fresh, state.perSpread, W, H));
  commit();
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
    state.photos.forEach((p) => p.url?.startsWith('blob:') && URL.revokeObjectURL(p.url));
    state.photos = [];
    state.spreads = [];
    commit();
    status('');
  };
  $('#export').onclick = exportPDF;

  addEventListener('resize', render);
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

  status('Decoding images at full resolution…');
  await Promise.all([...document.querySelectorAll('.sheet img')].map((i) => i.decode().catch(() => {})));
  const { SW, SH } = sheetSize();
  const size = `${SW} x ${SH} in (incl. ${state.bleed} in bleed)`;

  if (!IS_APP) {
    status(`Choose "Save as PDF" — the preview should read ${size}.`);
    return print();
  }
  const saved = await api.exportPDF({ suggestedName: `album-${page().w}x${page().h}.pdf` });
  status(saved ? `Saved ${saved} · ${state.spreads.length} spreads at ${size}` : 'Export cancelled.');
}

restore();
initControls();
render();
