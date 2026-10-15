/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Squarified treemap (Bruls, Huizing, van Wijk) and its drawing on a canvas.
 * ES5: it also runs in the browser of older firmwares. */
var DSTreemap = (function () {
  /* Worst aspect ratio of a row of areas laid along a side. */
  function worst(row, sum, side) {
    var mx = 0, mn = Infinity, i;
    for (i = 0; i < row.length; i++) {
      if (row[i] > mx) mx = row[i];
      if (row[i] < mn) mn = row[i];
    }
    var s2 = sum * sum, w2 = side * side;
    return Math.max(w2 * mx / s2, s2 / (w2 * mn));
  }

  /* Cells for values inside x, y, w, h: [{i, x, y, w, h}], i the index in values.
   * Largest first gives the squarest cells. Values <= 0 get no cell. */
  function squarify(values, x, y, w, h) {
    var out = [], items = [], total = 0, i;
    for (i = 0; i < values.length; i++) {
      if (values[i] > 0) { items.push(i); total += values[i]; }
    }
    if (!items.length || w <= 0 || h <= 0) return out;
    var scale = w * h / total, k = 0;
    while (k < items.length) {
      var side = Math.min(w, h), row = [values[items[k]] * scale], sum = row[0], end = k + 1;
      while (end < items.length) {
        var a = values[items[end]] * scale, next = row.concat([a]);
        if (worst(next, sum + a, side) > worst(row, sum, side)) break;
        row = next;
        sum += a;
        end++;
      }
      var thick = sum / side, pos = 0, j;
      for (j = 0; j < row.length; j++) {
        var len = row[j] / thick;
        if (w >= h) out.push({ i: items[k + j], x: x, y: y + pos, w: thick, h: len });
        else out.push({ i: items[k + j], x: x + pos, y: y, w: len, h: thick });
        pos += len;
      }
      if (w >= h) { x += thick; w -= thick; } else { y += thick; h -= thick; }
      k = end;
    }
    return out;
  }

  var HUES = [210, 30, 120, 280, 0, 180, 60, 330, 150, 250, 90, 15];

  function shade(hue, level) {
    return 'hsl(' + hue + ',55%,' + Math.min(28 + level * 9, 64) + '%)';
  }

  function fit(ctx, text, width) {
    if (ctx.measureText(text).width <= width) return text;
    while (text.length > 1 && ctx.measureText(text + '…').width > width) text = text.slice(0, -1);
    return text + '…';
  }

  /* Draws map (the JSON of /api/map) on canvas, with the cell of selectedId outlined.
   * Returns the cells for hit(), parents before their children. */
  function draw(canvas, map, selectedId) {
    var ctx = canvas.getContext('2d'), cells = [], sel = null;
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.font = '11px sans-serif';
    ctx.textBaseline = 'top';
    function walk(node, x, y, w, h, level, hue) {
      var cell = { node: node, x: x, y: y, w: w, h: h }, kids = node.c || [], i;
      cells.push(cell);
      if (node.id === selectedId) sel = cell;
      ctx.fillStyle = node.o ? '#4a4f57' : shade(hue, level);
      ctx.fillRect(x, y, w, h);
      ctx.strokeStyle = '#0e1116';
      ctx.lineWidth = 1;
      ctx.strokeRect(x + 0.5, y + 0.5, Math.max(w - 1, 0), Math.max(h - 1, 0));
      var head = kids.length && w > 30 && h > 30 ? 14 : 0;
      if (w > 40 && h > 14) {
        ctx.fillStyle = '#ffffff';
        ctx.fillText(fit(ctx, node.o ? node.k + ' others' : node.n, w - 6), x + 3, y + 2);
      }
      if (!kids.length || w < 6 || h < 6 + head) return;
      var vals = [];
      for (i = 0; i < kids.length; i++) vals.push(kids[i].s);
      var r = squarify(vals, x + 2, y + head + 1, w - 4, h - head - 3);
      for (i = 0; i < r.length; i++)
        walk(kids[r[i].i], r[i].x, r[i].y, r[i].w, r[i].h, level + 1, level === 0 ? HUES[r[i].i % HUES.length] : hue);
    }
    walk(map, 0, 0, canvas.width, canvas.height, 0, 210);
    if (sel) {
      ctx.strokeStyle = '#ffd400';
      ctx.lineWidth = 2;
      ctx.strokeRect(sel.x + 1, sel.y + 1, Math.max(sel.w - 2, 0), Math.max(sel.h - 2, 0));
    }
    return cells;
  }

  /* The deepest cell at x, y, or null. */
  function hit(cells, x, y) {
    for (var i = cells.length - 1; i >= 0; i--) {
      var c = cells[i];
      if (x >= c.x && y >= c.y && x < c.x + c.w && y < c.y + c.h) return c;
    }
    return null;
  }

  return { squarify: squarify, draw: draw, hit: hit };
})();

if (typeof module !== 'undefined') module.exports = DSTreemap;
