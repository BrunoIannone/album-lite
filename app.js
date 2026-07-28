const $ = (sel, el = document) => el.querySelector(sel);
const uid = () => Math.random().toString(36).slice(2, 10);
const STORE = 'album-lite/v1';

const state = {
  size: 2,
  bleed: 0.125,
  gutter: 0.15,
  perSpread: 4,
  photos: [],   // { id, path, url, aspect }
  spreads: [],  // { tpl, items: [{ pid, pos: [x%, y%] }] }
};

const photo = (pid) => state.photos.find((p) => p.id === pid);
const page = () => PAGE_SIZES[state.size];
const spreadSize = () => ({ W: page().w * 2, H: page().h });
const sheetSize = () => {
  const { W, H } = spreadSize();
  return { SW: W + state.bleed * 2, SH: H + state.bleed * 2 };
};

/* --- geometry ------------------------------------------------------------ */

// Slot rect (fractions of trim) -> inches on the sheet. Slots touching a trim edge
// are pushed out into the bleed so nothing shows white after the lab trims.
function slotBox(rect) {
  const { W, H } = spreadSize();
  const b = state.bleed;
  const L = rect.x <= 0.001, T = rect.y <= 0.001;
  const R = rect.x + rect.w >= 0.999, B = rect.y + rect.h >= 0.999;
  const g = state.gutter / 2;
  return {
    left: b + rect.x * W - (L ? b : 0),
    top: b + rect.y * H - (T ? b : 0),
    width: rect.w * W + (L ? b : 0) + (R ? b : 0),
    height: rect.h * H + (T ? b : 0) + (B ? b : 0),
    pad: [T ? 0 : g, R ? 0 : g, B ? 0 : g, L ? 0 : g],
  };
}

const templateOf = (spread) => TEMPLATES[spread.items.length][spread.tpl] || TEMPLATES[spread.items.length][0];

function refit(spread) {
  const { W, H } = spreadSize();
  spread.tpl = pickTemplate(spread.items.map((it) => photo(it.pid)?.aspect ?? 1), W, H);
}

function rebuild() {
  const { W, H } = spreadSize();
  state.spreads = autoFlow(state.photos, state.perSpread).map((group) => {
    const items = group.map((p) => ({ pid: p.id, pos: [50, 50] }));
    return { tpl: pickTemplate(group.map((p) => p.aspect), W, H), items };
  });
}

/* --- render -------------------------------------------------------------- */

function render() {
  const { SW, SH } = sheetSize();
  $('#pagerule').textContent = `@page { size: ${SW}in ${SH}in; margin: 0 }`;

  const used = new Set(state.spreads.flatMap((s) => s.items.map((i) => i.pid)));
  $('#trayEmpty').hidden = state.photos.length > 0;
  $('#thumbs').replaceChildren(...state.photos.map((p) => {
    const d = document.createElement('div');
    d.className = 'thumb' + (used.has(p.id) ? '' : ' unused');
    d.draggable = true;
    d.title = p.path;
    d.ondragstart = (e) => e.dataTransfer.setData('text/plain', `p:${p.id}`);
    const img = new Image();
    img.src = p.url;
    img.loading = 'lazy';
    img.alt = '';
    d.append(img);
    return d;
  }));

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
  el.style.cssText = `left:${box.left}in;top:${box.top}in;width:${box.width}in;height:${box.height}in;` +
    `padding:${box.pad.map((v) => v + 'in').join(' ')}`;

  if (p) {
    const img = new Image();
    img.src = p.url;
    img.alt = '';
    img.draggable = false;
    img.style.objectPosition = `${item.pos[0]}% ${item.pos[1]}%`;
    img.onpointerdown = (e) => startPan(e, img, item);
    img.ondblclick = () => { item.pos = [50, 50]; commit(); };
    el.append(img);
  }

  const ui = document.createElement('div');
  ui.className = 'slot-ui no-print';
  const grab = btn('⠿', 'Drag to swap with another slot', null);
  grab.className = 'grab';
  grab.draggable = true;
  grab.ondragstart = (e) => e.dataTransfer.setData('text/plain', `s:${si}:${ii}`);
  ui.append(grab, btn('✕', 'Remove photo from this slot', () => {
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
      item.pid = a;
      item.pos = [50, 50];
    } else {
      const other = state.spreads[+a].items[+b];
      [item.pid, other.pid] = [other.pid, item.pid];
      [item.pos, other.pos] = [other.pos, item.pos];
    }
    commit();
  };
  return el;
}

// Drag inside a slot to re-frame the crop.
function startPan(e, img, item) {
  e.preventDefault();
  img.setPointerCapture(e.pointerId);
  const rect = img.getBoundingClientRect();
  const start = { x: e.clientX, y: e.clientY, pos: [...item.pos] };
  const move = (ev) => {
    const dx = ((ev.clientX - start.x) / rect.width) * 100;
    const dy = ((ev.clientY - start.y) / rect.height) * 100;
    item.pos = [clamp(start.pos[0] - dx), clamp(start.pos[1] - dy)];
    img.style.objectPosition = `${item.pos[0]}% ${item.pos[1]}%`;
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

/* --- import & persistence ------------------------------------------------ */

const aspectOf = (url) => new Promise((res) => {
  const im = new Image();
  im.onload = () => res(im.naturalWidth / im.naturalHeight || 1);
  im.onerror = () => res(1);
  im.src = url;
});

async function addFiles(files) {
  for (const f of files) {
    const path = api.pathForFile(f);
    const url = api.urlForPath(path);
    state.photos.push({ id: uid(), path, url, aspect: await aspectOf(url) });
  }
  rebuild();
  commit();
  status(`${state.photos.length} photos, ${state.spreads.length} spreads`);
}

function save() {
  const { size, bleed, gutter, perSpread, spreads } = state;
  try {
    localStorage.setItem(STORE, JSON.stringify({
      size, bleed, gutter, perSpread, spreads,
      photos: state.photos.map(({ id, path, aspect }) => ({ id, path, aspect })),
    }));
  } catch { /* ponytail: no storage (or quota hit) just means no autosave. */ }
}

function restore() {
  try {
    const saved = JSON.parse(localStorage.getItem(STORE) || 'null');
    if (!saved) return;
    Object.assign(state, saved);
    state.photos = saved.photos.map((p) => ({ ...p, url: api.urlForPath(p.path) }));
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
    state.photos = [];
    state.spreads = [];
    commit();
  };
  $('#export').onclick = exportPDF;

  addEventListener('resize', render);
  addEventListener('dragover', (e) => e.preventDefault());
  addEventListener('drop', (e) => {
    if (e.dataTransfer.files.length) { e.preventDefault(); addFiles([...e.dataTransfer.files]); }
  });
}

async function exportPDF() {
  if (!state.spreads.length) return status('Nothing to export.');
  status('Decoding images at full resolution…');
  await Promise.all([...document.querySelectorAll('.sheet img')].map((i) => i.decode().catch(() => {})));
  const { SW, SH } = sheetSize();
  const saved = await api.exportPDF({
    suggestedName: `album-${page().w}x${page().h}.pdf`,
  });
  status(saved
    ? `Saved ${saved}  ·  ${state.spreads.length} spreads at ${SW}x${SH} in (incl. ${state.bleed} in bleed)`
    : 'Export cancelled.');
}

restore();
initControls();
render();
