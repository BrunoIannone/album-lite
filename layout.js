/* Pure layout data + heuristics. Loaded by the renderer via <script> and by `node --test`. */

const PAGE_SIZES = [
  { name: '8 x 8 in', w: 8, h: 8 },
  { name: '10 x 10 in', w: 10, h: 10 },
  { name: '12 x 12 in', w: 12, h: 12 },
  { name: '8.5 x 11 in', w: 8.5, h: 11 },
  { name: '11 x 8.5 in', w: 11, h: 8.5 },
  { name: '12 x 9 in', w: 12, h: 9 },
  { name: '10 x 7 in', w: 10, h: 7 },
  { name: 'A4 portrait', w: 8.27, h: 11.69 },
];

const r = (x, y, w, h) => ({ x, y, w, h });
const cols = (n) => Array.from({ length: n }, (_, i) => r(i / n, 0, 1 / n, 1));
const grid = (c, rows) =>
  Array.from({ length: c * rows }, (_, i) =>
    r((i % c) / c, Math.floor(i / c) / rows, 1 / c, 1 / rows));

/* Slot rects are fractions of the SPREAD (two facing pages), listed in reading order.
   Photos fill slots in order, so chronology survives auto-layout. */
const TEMPLATES = {
  1: [
    [r(0, 0, 1, 1)],
    [r(0.1, 0.1, 0.8, 0.8)],
    [r(0.34, 0.05, 0.32, 0.9)],
    [r(0.05, 0.26, 0.9, 0.48)],
  ],
  2: [
    [r(0, 0, 0.5, 1), r(0.5, 0, 0.5, 1)],
    [r(0.05, 0.12, 0.43, 0.76), r(0.52, 0.12, 0.43, 0.76)],
    [r(0.16, 0.05, 0.28, 0.9), r(0.56, 0.05, 0.28, 0.9)],
    [r(0.05, 0.06, 0.9, 0.41), r(0.05, 0.53, 0.9, 0.41)],
    [r(0, 0, 0.62, 1), r(0.68, 0.22, 0.26, 0.56)],
    [r(0.06, 0.22, 0.26, 0.56), r(0.38, 0, 0.62, 1)],
  ],
  3: [
    [r(0, 0, 0.5, 1), r(0.5, 0, 0.5, 0.5), r(0.5, 0.5, 0.5, 0.5)],
    [r(0, 0, 0.5, 0.5), r(0, 0.5, 0.5, 0.5), r(0.5, 0, 0.5, 1)],
    cols(3),
    [r(0.06, 0.08, 0.26, 0.84), r(0.37, 0.08, 0.26, 0.84), r(0.68, 0.08, 0.26, 0.84)],
    [r(0.04, 0.05, 0.92, 0.46), r(0.04, 0.55, 0.45, 0.4), r(0.51, 0.55, 0.45, 0.4)],
  ],
  4: [
    grid(2, 2),
    [r(0.05, 0.07, 0.43, 0.41), r(0.52, 0.07, 0.43, 0.41), r(0.05, 0.52, 0.43, 0.41), r(0.52, 0.52, 0.43, 0.41)],
    cols(4),
    [r(0, 0, 0.55, 1), r(0.55, 0, 0.45, 1 / 3), r(0.55, 1 / 3, 0.45, 1 / 3), r(0.55, 2 / 3, 0.45, 1 / 3)],
    [r(0, 0, 0.45, 1 / 3), r(0, 1 / 3, 0.45, 1 / 3), r(0, 2 / 3, 0.45, 1 / 3), r(0.45, 0, 0.55, 1)],
  ],
  5: [
    [r(0, 0, 0.5, 1), r(0.5, 0, 0.25, 0.5), r(0.75, 0, 0.25, 0.5), r(0.5, 0.5, 0.25, 0.5), r(0.75, 0.5, 0.25, 0.5)],
    cols(5),
    [r(0.03, 0.05, 0.94, 0.45), r(0.03, 0.53, 0.225, 0.42), r(0.2675, 0.53, 0.225, 0.42), r(0.505, 0.53, 0.225, 0.42), r(0.7425, 0.53, 0.225, 0.42)],
    [r(0.03, 0.05, 0.46, 0.44), r(0.51, 0.05, 0.46, 0.44), r(0.03, 0.53, 0.3, 0.42), r(0.35, 0.53, 0.3, 0.42), r(0.67, 0.53, 0.3, 0.42)],
  ],
  6: [
    grid(3, 2),
    cols(6),
    grid(2, 3),
    [r(0.09, 0.05, 0.2, 0.44), r(0.4, 0.05, 0.2, 0.44), r(0.71, 0.05, 0.2, 0.44),
     r(0.09, 0.53, 0.2, 0.42), r(0.4, 0.53, 0.2, 0.42), r(0.71, 0.53, 0.2, 0.42)],
  ],
};

const MAX_PER_SPREAD = 6;

const slotAspect = (rect, W, H) => (rect.w * W) / (rect.h * H);

/* Cost = how badly slot shapes disagree with the photos that will land in them,
   in reading order. Log-ratio so 2:1 too wide costs the same as 2:1 too tall. */
function layoutCost(layout, aspects, W, H) {
  let c = 0;
  for (let i = 0; i < layout.length; i++) {
    c += Math.abs(Math.log(slotAspect(layout[i], W, H)) - Math.log(aspects[i]));
  }
  return c;
}

/** Index into TEMPLATES[n] of the best-fitting layout for these photo aspect ratios. */
function pickTemplate(aspects, W, H) {
  const opts = TEMPLATES[aspects.length];
  if (!opts) return 0;
  let best = 0;
  let bestCost = Infinity;
  opts.forEach((layout, i) => {
    const c = layoutCost(layout, aspects, W, H);
    if (c < bestCost) {
      bestCost = c;
      best = i;
    }
  });
  return best;
}

/** Chunk photos into spreads. Panoramas get a spread to themselves. */
function autoFlow(photos, perSpread) {
  const max = Math.min(Math.max(perSpread | 0, 1), MAX_PER_SPREAD);
  const spreads = [];
  let cur = [];
  const flush = () => {
    if (cur.length) spreads.push(cur);
    cur = [];
  };
  for (const p of photos) {
    if (p.aspect >= 2.2 || p.aspect <= 0.45) {
      flush();
      spreads.push([p]);
      continue;
    }
    cur.push(p);
    if (cur.length >= max) flush();
  }
  flush();
  return spreads;
}

if (typeof module !== 'undefined') {
  module.exports = { PAGE_SIZES, TEMPLATES, MAX_PER_SPREAD, slotAspect, layoutCost, pickTemplate, autoFlow };
}
