/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * PS5 DirStat panel: tree and treemap. ES5 with XMLHttpRequest, shared by / and /legacy. */
(function () {
  var gen = 0, scanning = false, selected = -1, mapRoot = 0, mapData = null, cells = [], timer = null;

  function $(id) { return document.getElementById(id); }

  function req(method, url, cb) {
    var x = new XMLHttpRequest();
    x.open(method, url, true);
    x.onreadystatechange = function () {
      if (x.readyState !== 4) return;
      var body = null;
      try { body = JSON.parse(x.responseText); } catch (e) { body = null; }
      cb(x.status, body);
    };
    x.send(null);
  }

  function fmt(n) {
    var u = ['B', 'KB', 'MB', 'GB', 'TB'], i = 0;
    while (n >= 1024 && i < u.length - 1) { n /= 1024; i++; }
    return (i ? n.toFixed(1) : String(n)) + ' ' + u[i];
  }

  function esc(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  function api(path, id, extra) { return '/api/' + path + '?id=' + id + '&gen=' + gen + extra; }

  /* --- status and scan --- */

  function poll(ms) { clearTimeout(timer); timer = setTimeout(status, ms); }

  function status() {
    req('GET', '/api/status', function (code, s) {
      if (code !== 200 || !s) { $('state').textContent = 'Panel not reachable'; poll(3000); return; }
      scanning = s.state === 'scanning';
      $('scan').textContent = scanning ? 'Cancel' : 'Scan';
      if (scanning) $('state').textContent = 'Scanning: ' + s.entries + ' entries. ' + s.path;
      else if (s.has_tree) $('state').textContent = 'Last scan ' + new Date(s.time * 1000).toLocaleString() +
        ' (' + Math.round(s.duration_ms / 1000) + ' s)' + (s.message ? '. ' + s.message : '');
      else $('state').textContent = s.message || 'No scan yet. Press Scan.';
      $('totals').textContent = s.has_tree ? fmt(s.size) + ' in ' + s.files + ' files and ' + s.dirs + ' folders' +
        (s.errors ? ', ' + s.errors + ' not readable' : '') : '';
      if (s.has_tree && s.gen !== gen) { gen = s.gen; reload(); }
      if (scanning) poll(1000);
    });
  }

  $('scan').onclick = function () {
    req('POST', scanning ? '/api/scan/cancel' : '/api/scan', function () { poll(300); });
  };

  /* --- tree --- */

  function makeRow(n, depth, parentSize) {
    var r = document.createElement('div'), dir = n.type === 'dir' && !n.err && !n.others;
    var pct = parentSize > 0 ? n.size * 100 / parentSize : 100;
    var name = n.others ? n.count + ' others' : n.name;
    if (n.type === 'skip') name += ' (not scanned)';
    if (n.err) name += ' (not readable)';
    r.className = 'row';
    r.tabIndex = 0;
    r.setAttribute('data-id', n.others ? '' : n.id);
    r.setAttribute('data-depth', depth);
    r.setAttribute('data-dir', dir ? '1' : '');
    r.innerHTML = '<span class="tw" style="padding-left:' + depth * 14 + 'px">' + (dir ? '&#9656;' : '') + '</span>' +
      '<span class="nm">' + esc(name) + '</span>' +
      '<span class="bar"><span style="width:' + pct.toFixed(1) + '%"></span></span>' +
      '<span class="sz">' + fmt(n.size) + '</span>' +
      '<span class="pc">' + pct.toFixed(1) + '%</span>' +
      '<span class="it">' + (n.type === 'dir' ? n.files + n.dirs : '') + '</span>';
    return r;
  }

  function rowOf(el) {
    while (el && el !== $('tree')) {
      if (el.className && el.className.indexOf('row') === 0) return el;
      el = el.parentNode;
    }
    return null;
  }

  function rowById(id) { return $('tree').querySelector('[data-id="' + id + '"]'); }
  function isOpen(row) { return row.getAttribute('data-open') === '1'; }

  function collapse(row) {
    var d = +row.getAttribute('data-depth'), r = row.nextSibling;
    while (r && +r.getAttribute('data-depth') > d) {
      var next = r.nextSibling;
      r.parentNode.removeChild(r);
      r = next;
    }
    row.setAttribute('data-open', '');
    row.firstChild.innerHTML = '&#9656;';
  }

  function expand(row, done) {
    if (!row.getAttribute('data-dir') || isOpen(row) || row.getAttribute('data-busy')) { if (done) done(); return; }
    row.setAttribute('data-busy', '1');
    var d = +row.getAttribute('data-depth');
    req('GET', api('node', row.getAttribute('data-id'), '&limit=200'), function (code, n) {
      row.setAttribute('data-busy', '');
      if (code === 409) { status(); return; }
      if (code !== 200 || !row.parentNode) return;
      var frag = document.createDocumentFragment(), i;
      for (i = 0; i < n.children.length; i++) frag.appendChild(makeRow(n.children[i], d + 1, n.size));
      if (n.others.count)
        frag.appendChild(makeRow({ others: 1, count: n.others.count, size: n.others.size, type: 'file' }, d + 1, n.size));
      row.parentNode.insertBefore(frag, row.nextSibling);
      row.setAttribute('data-open', '1');
      row.firstChild.innerHTML = '&#9662;';
      if (done) done();
    });
  }

  function toggle(row) { if (isOpen(row)) collapse(row); else expand(row); }

  function mark(row) {
    var prev = $('tree').querySelector('.sel');
    if (prev) prev.className = 'row';
    row.className = 'row sel';
  }

  function select(row) {
    mark(row);
    var id = row.getAttribute('data-id');
    selected = id === '' ? -1 : +id;
    info(selected);
    drawMap();
  }

  /* Click selects; a second click (X on the controller) on the selected folder opens it. */
  $('tree').onclick = function (e) {
    var row = rowOf(e.target || e.srcElement);
    if (!row) return;
    if (row.className === 'row sel' || (e.target || e.srcElement).className === 'tw') toggle(row);
    select(row);
  };

  $('tree').onkeydown = function (e) {
    var row = rowOf(e.target || e.srcElement), k = e.keyCode;
    if (!row) return;
    if (k === 13) toggle(row);
    else if (k === 39) expand(row);
    else if (k === 37) { if (isOpen(row)) collapse(row); }
    else if (k === 40 && row.nextSibling) { row.nextSibling.focus(); select(row.nextSibling); }
    else if (k === 38 && row.previousSibling) { row.previousSibling.focus(); select(row.previousSibling); }
    else return;
    if (e.preventDefault) e.preventDefault();
  };

  function reload() {
    $('tree').innerHTML = '';
    selected = -1;
    mapRoot = 0;
    info(-1);
    req('GET', api('node', 0, '&limit=0'), function (code, n) {
      if (code === 409) { status(); return; }
      if (code !== 200) return;
      var row = makeRow(n, 0, n.size);
      $('tree').appendChild(row);
      expand(row);
      enterMap(0);
    });
  }

  /* Opens the folders down to id and selects its row (when it is not under "others"). */
  function revealInTree(id) {
    req('GET', api('node', id, '&limit=0'), function (code, n) {
      if (code !== 200) return;
      var chain = n.ancestors, i = 0;
      (function next() {
        if (i < chain.length) {
          var r = rowById(chain[i++].id);
          if (r) expand(r, next);
          return;
        }
        var row = rowById(id);
        if (row) { mark(row); if (row.scrollIntoView) row.scrollIntoView(false); }
      })();
    });
  }

  function info(id) {
    if (id < 0) { $('info').innerHTML = ''; return; }
    req('GET', api('node', id, '&limit=0'), function (code, n) {
      if (code !== 200 || id !== selected) return;
      $('info').innerHTML = esc(n.path) + ' &middot; ' + fmt(n.size) +
        (n.type === 'dir' ? ' &middot; ' + n.files + ' files, ' + n.dirs + ' folders <button id="open">Show in map</button>' : '');
      var b = $('open');
      if (b) b.onclick = function () { enterMap(n.id); };
    });
  }

  /* --- treemap --- */

  function enterMap(id) {
    req('GET', api('node', id, '&limit=0'), function (code, n) {
      if (code !== 200) return;
      mapRoot = id;
      var all = n.ancestors.concat([{ id: n.id, name: n.name }]), html = '', i;
      for (i = 0; i < all.length; i++)
        html += (i ? ' / ' : '') + '<a href="#" data-id="' + all[i].id + '">' + esc(all[i].name) + '</a>';
      $('crumbs').innerHTML = html;
      req('GET', api('map', id, '&depth=3'), function (c2, m) {
        if (c2 === 409) { status(); return; }
        if (c2 !== 200) return;
        mapData = m;
        drawMap();
      });
    });
  }

  $('crumbs').onclick = function (e) {
    var a = e.target || e.srcElement, id = a.getAttribute && a.getAttribute('data-id');
    if (id !== null && id !== undefined && id !== '') enterMap(+id);
    if (e.preventDefault) e.preventDefault();
  };

  function drawMap() {
    var c = $('map');
    if (!mapData) return;
    c.width = c.clientWidth || 600;
    c.height = c.clientHeight || 320;
    cells = DSTreemap.draw(c, mapData, selected);
  }

  function cellAt(e) {
    var r = $('map').getBoundingClientRect();
    var cell = DSTreemap.hit(cells, e.clientX - r.left, e.clientY - r.top);
    return cell && !cell.node.o ? cell : null;
  }

  $('map').onclick = function (e) {
    var cell = cellAt(e);
    if (!cell) return;
    selected = cell.node.id;
    info(selected);
    revealInTree(selected);
    drawMap();
  };

  $('map').ondblclick = function (e) {
    var cell = cellAt(e);
    if (cell && cell.node.t === 'dir' && cell.node.id !== mapRoot) enterMap(cell.node.id);
  };

  $('map').onmousemove = function (e) {
    var cell = cellAt(e);
    $('map').title = cell ? cell.node.n + ' · ' + fmt(cell.node.s) : '';
  };

  window.onresize = drawMap;
  status();
})();
