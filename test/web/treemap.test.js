// SPDX-License-Identifier: GPL-3.0-or-later
// Squarified layout: every value gets a cell inside the box, areas proportional, no overlap.
const test = require('node:test');
const assert = require('node:assert');
const T = require('../../web/treemap.js');

function check(values, w, h) {
  const cells = T.squarify(values, 0, 0, w, h);
  const positive = values.filter((v) => v > 0);
  assert.strictEqual(cells.length, positive.length);
  const total = positive.reduce((a, b) => a + b, 0);
  const eps = 1e-6 * w * h;
  for (const c of cells) {
    assert.ok(c.x >= -1e-9 && c.y >= -1e-9 && c.x + c.w <= w + 1e-6 && c.y + c.h <= h + 1e-6, 'inside');
    assert.ok(Math.abs(c.w * c.h - (values[c.i] / total) * w * h) < eps + 1e-9, 'area');
  }
  for (let a = 0; a < cells.length; a++)
    for (let b = a + 1; b < cells.length; b++) {
      const p = cells[a], q = cells[b];
      const ox = Math.min(p.x + p.w, q.x + q.w) - Math.max(p.x, q.x);
      const oy = Math.min(p.y + p.h, q.y + q.h) - Math.max(p.y, q.y);
      assert.ok(ox <= 1e-6 || oy <= 1e-6, 'no overlap');
    }
  return cells;
}

test('classic example', () => { check([6, 6, 4, 3, 2, 2, 1], 6, 4); });
test('tall box', () => { check([50, 20, 20, 5, 3, 1, 1], 100, 900); });
test('one value fills the box', () => {
  const c = check([7], 10, 10)[0];
  assert.deepStrictEqual([c.x, c.y, c.w, c.h], [0, 0, 10, 10]);
});
test('zeros get no cell', () => { check([5, 0, 3, 0], 10, 10); });
test('empty and degenerate', () => {
  assert.deepStrictEqual(T.squarify([], 0, 0, 10, 10), []);
  assert.deepStrictEqual(T.squarify([1, 2], 0, 0, 0, 10), []);
});
test('many skewed values', () => {
  const v = [];
  for (let i = 0; i < 500; i++) v.push(Math.round(1e6 / (i + 1)));
  check(v, 1280, 400);
});
test('hit returns the deepest cell', () => {
  const cells = [{ node: { id: 0 }, x: 0, y: 0, w: 10, h: 10 }, { node: { id: 1 }, x: 2, y: 2, w: 3, h: 3 }];
  assert.strictEqual(T.hit(cells, 3, 3).node.id, 1);
  assert.strictEqual(T.hit(cells, 8, 8).node.id, 0);
  assert.strictEqual(T.hit(cells, 20, 20), null);
});
