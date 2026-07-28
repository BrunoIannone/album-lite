const { test } = require('node:test');
const assert = require('node:assert');
const { TEMPLATES, pickTemplate, autoFlow, slotAspect } = require('./layout.js');

const PAGE = { w: 12, h: 12 };
const W = PAGE.w * 2;
const H = PAGE.h;

test('every template has the right slot count and stays inside the spread', () => {
  for (const [n, layouts] of Object.entries(TEMPLATES)) {
    for (const layout of layouts) {
      assert.strictEqual(layout.length, Number(n));
      for (const s of layout) {
        assert.ok(s.x >= 0 && s.y >= 0 && s.x + s.w <= 1.0001 && s.y + s.h <= 1.0001,
          `slot out of bounds in TEMPLATES[${n}]: ${JSON.stringify(s)}`);
        assert.ok(s.w > 0 && s.h > 0);
      }
    }
  }
});

test('template slots never overlap', () => {
  const hits = (a, b) =>
    a.x < b.x + b.w - 1e-6 && b.x < a.x + a.w - 1e-6 &&
    a.y < b.y + b.h - 1e-6 && b.y < a.y + a.h - 1e-6;
  for (const [n, layouts] of Object.entries(TEMPLATES)) {
    layouts.forEach((layout, li) => {
      for (let i = 0; i < layout.length; i++) {
        for (let j = i + 1; j < layout.length; j++) {
          assert.ok(!hits(layout[i], layout[j]), `TEMPLATES[${n}][${li}] slots ${i}/${j} overlap`);
        }
      }
    });
  }
});

test('portrait photos get portrait slots, panoramas get wide slots', () => {
  const portraits = pickTemplate([0.67, 0.67], W, H);
  TEMPLATES[2][portraits].forEach((s) => assert.ok(slotAspect(s, W, H) < 1, 'expected tall slots'));

  const panos = pickTemplate([3, 3], W, H);
  TEMPLATES[2][panos].forEach((s) => assert.ok(slotAspect(s, W, H) > 2, 'expected wide slots'));
});

test('a lone panorama gets the banner slot, a lone portrait gets the tall slot', () => {
  assert.ok(slotAspect(TEMPLATES[1][pickTemplate([3.5], W, H)][0], W, H) > 3);
  assert.ok(slotAspect(TEMPLATES[1][pickTemplate([0.67], W, H)][0], W, H) < 1);
});

test('portrait sets always choose narrower slots than landscape sets', () => {
  const mean = (layout) =>
    layout.reduce((s, r) => s + slotAspect(r, W, H), 0) / layout.length;
  for (let n = 1; n <= 6; n++) {
    const tall = mean(TEMPLATES[n][pickTemplate(Array(n).fill(0.67), W, H)]);
    const wide = mean(TEMPLATES[n][pickTemplate(Array(n).fill(1.5), W, H)]);
    assert.ok(tall < wide, `n=${n}: portrait pick (${tall}) not narrower than landscape pick (${wide})`);
  }
});

test('autoFlow chunks by size and isolates panoramas', () => {
  const p = (aspect) => ({ aspect });
  const flow = autoFlow([p(1), p(1), p(3.5), p(1), p(1), p(1)], 2);
  assert.deepStrictEqual(flow.map((s) => s.length), [2, 1, 2, 1]);
  assert.strictEqual(flow[1][0].aspect, 3.5);
});
