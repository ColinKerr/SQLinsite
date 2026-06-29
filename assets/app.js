"use strict";

// ---- constants ------------------------------------------------------------
const PALETTE = ["#4e79a7","#f28e2b","#59a14f","#e15759","#b07aa1",
                 "#76b7b2","#edc948","#ff9da7","#9c755f","#bab0ac"];
const STRUCTURAL = {
  "overflow": "#7a7f8a", "freelist-trunk": "#565b66", "freelist-leaf": "#454a54",
  "pointer-map": "#8a7fae", "lock-byte": "#9a6b6b", "unallocated": "#33373f",
};
const GLYPH = {
  "table-leaf":"T","table-interior":"T↑","index-leaf":"I","index-interior":"I↑",
  "overflow":"O","freelist-trunk":"F","freelist-leaf":"f","pointer-map":"P",
  "lock-byte":"L","unallocated":"·",
};
const LOD_THRESHOLD = 4;   // px: below this, draw runs instead of pages
const GAP = 1;
const RANGE_CAP = 2000000;

// ---- state ----------------------------------------------------------------
const S = {
  meta: null, objById: new Map(),
  view: "pages",
  blockPx: 12,
  scroll: { pages: 0, tables: 0 },
  pageCount: 0,
  metric: "none",
  hasProfile: false,
  selected: 0,
  bands: [], _tablesHeight: 0,
  // page-grid data cache (kept until replacement arrives → no flashing)
  pagesCache: { lod: null, from: 0, to: 0, pages: null, runs: null },
  pagesFetchKey: null,
  objPages: new Map(),
  // profile (loaded once; bounded by accessed pages)
  profMap: new Map(), profSorted: [], prefReads: [0], prefWrites: [0],
  profMax: { reads: 1, writes: 1, total: 1 },
  dpr: 1, cssW: 0, cssH: 0, _pending: false,
};

const canvas = document.getElementById("canvas");
const ctx = canvas.getContext("2d");
const popup = document.getElementById("popup");

// ---- helpers --------------------------------------------------------------
const cell = () => S.blockPx + GAP;
const colsFor = () => Math.max(1, Math.floor(S.cssW / cell()));
const colorForObject = (id) => PALETTE[((id % PALETTE.length) + PALETTE.length) % PALETTE.length];
const colorForPage = (objectId, pageType) =>
  (objectId === null || objectId === undefined) ? (STRUCTURAL[pageType] || "#33373f")
                                                : colorForObject(objectId);

async function getJson(url) {
  const r = await fetch(url);
  if (!r.ok) return null;
  return r.json();
}
function scheduleRender() {
  if (S._pending) return;
  S._pending = true;
  requestAnimationFrame(() => { S._pending = false; render(); });
}

// ---- profile / overlay ----------------------------------------------------
function overlayActive() { return S.metric !== "none" && S.hasProfile && S.profSorted.length > 0; }
function globalMax() {
  return S.metric === "reads" ? S.profMax.reads : S.metric === "writes" ? S.profMax.writes : S.profMax.total;
}
function metricForPage(n) {
  const a = S.profMap.get(n);
  if (!a) return 0;
  return S.metric === "reads" ? a.reads : S.metric === "writes" ? a.writes : a.reads + a.writes;
}
function lowerBound(arr, x) { let lo = 0, hi = arr.length; while (lo < hi) { const m = (lo + hi) >> 1; if (arr[m] < x) lo = m + 1; else hi = m; } return lo; }
function upperBound(arr, x) { let lo = 0, hi = arr.length; while (lo < hi) { const m = (lo + hi) >> 1; if (arr[m] <= x) lo = m + 1; else hi = m; } return lo; }
function rangeMetric(from, to) {
  const lo = lowerBound(S.profSorted, from), hi = upperBound(S.profSorted, to);
  const r = S.prefReads[hi] - S.prefReads[lo];
  const w = S.prefWrites[hi] - S.prefWrites[lo];
  return S.metric === "reads" ? r : S.metric === "writes" ? w : r + w;
}
function buildProfile(all) {
  let mr = 0, mw = 0, mt = 0;
  for (const p of (all.pages || [])) {
    S.profMap.set(p.pageNumber, p);
    S.profSorted.push(p.pageNumber);
    S.prefReads.push(S.prefReads[S.prefReads.length - 1] + p.reads);
    S.prefWrites.push(S.prefWrites[S.prefWrites.length - 1] + p.writes);
    mr = Math.max(mr, p.reads); mw = Math.max(mw, p.writes); mt = Math.max(mt, p.reads + p.writes);
  }
  S.profMax = { reads: mr || 1, writes: mw || 1, total: mt || 1 };
}

// ---- canvas sizing --------------------------------------------------------
function resizeCanvas() {
  const stage = document.getElementById("stage");
  S.cssW = stage.clientWidth;
  S.cssH = stage.clientHeight;
  S.dpr = window.devicePixelRatio || 1;
  canvas.width = Math.floor(S.cssW * S.dpr);
  canvas.height = Math.floor(S.cssH * S.dpr);
  ctx.setTransform(S.dpr, 0, 0, S.dpr, 0, 0);
  if (S.view === "tables") rebuildBands();
  scheduleRender();
}

// ---- layout ---------------------------------------------------------------
function pagesContentHeight() { return Math.ceil(S.pageCount / colsFor()) * cell(); }
function clampScroll() {
  const h = S.view === "pages" ? pagesContentHeight() : tablesContentHeight();
  S.scroll[S.view] = Math.max(0, Math.min(S.scroll[S.view], Math.max(0, h - S.cssH)));
}
function visiblePageRange() {
  const c = colsFor(), top = S.scroll.pages;
  const firstRow = Math.floor(top / cell());
  const lastRow = Math.floor((top + S.cssH) / cell());
  return [Math.max(1, firstRow * c + 1), Math.max(1, Math.min(S.pageCount, (lastRow + 1) * c))];
}

// ---- page-grid data (no-flash: keep old until new arrives) -----------------
async function ensurePagesData() {
  const [vf, vt] = visiblePageRange();
  const lod = S.blockPx >= LOD_THRESHOLD ? "pages" : "runs";
  const c = S.pagesCache;
  const have = lod === "pages" ? c.pages : c.runs;
  if (have && c.lod === lod && c.from <= vf && c.to >= vt) return;  // covered

  const margin = Math.ceil(S.cssH / cell()) * colsFor();  // one screen of overscan
  const from = Math.max(1, vf - margin);
  const to = Math.min(S.pageCount, vt + margin);
  const key = `${lod}:${from}:${to}`;
  if (S.pagesFetchKey === key) return;
  S.pagesFetchKey = key;

  const url = lod === "pages" ? `/api/pages?from=${from}&to=${to}`
                              : `/api/runs?from=${from}&to=${to}`;
  const data = await getJson(url);
  if (S.pagesFetchKey !== key || !data) return;  // superseded
  c.lod = lod; c.from = from; c.to = to;
  if (lod === "pages") { c.pages = data; }
  else { c.runs = data; }
  scheduleRender();
}

// ---- drawing --------------------------------------------------------------
function clear() { ctx.fillStyle = "#1b1d23"; ctx.fillRect(0, 0, S.cssW, S.cssH); }
function drawGlyph(type, x, y) {
  if (S.blockPx < 14) return;
  ctx.fillStyle = "rgba(16,18,26,.85)";
  ctx.font = `${Math.min(11, S.blockPx - 3)}px sans-serif`;
  ctx.textAlign = "center"; ctx.textBaseline = "middle";
  ctx.fillText(GLYPH[type] || "?", x + S.blockPx / 2, y + S.blockPx / 2);
}
function drawBlock(x, y, color, pageType, pageNumber) {
  const overlay = overlayActive();
  const v = overlay ? metricForPage(pageNumber) : 0;
  ctx.globalAlpha = (overlay && v === 0) ? 0.3 : 1;
  ctx.fillStyle = color;
  ctx.fillRect(x, y, S.blockPx, S.blockPx);
  ctx.globalAlpha = 1;
  if (overlay && v > 0) {
    ctx.fillStyle = `rgba(255,255,255,${(0.12 + 0.6 * v / globalMax()).toFixed(3)})`;
    ctx.fillRect(x, y, S.blockPx, S.blockPx);
  }
  drawGlyph(pageType, x, y);
}

function renderPages() {
  clear();
  const cols = colsFor(), scroll = S.scroll.pages, c = S.pagesCache;
  if (c.lod === "pages" && c.pages) {
    for (const p of c.pages.pages) {
      const idx = p.pageNumber - 1;
      const x = (idx % cols) * cell(), y = Math.floor(idx / cols) * cell() - scroll;
      if (y > S.cssH || y + S.blockPx < 0) continue;
      drawBlock(x, y, colorForPage(p.objectId, p.pageType), p.pageType, p.pageNumber);
      if (p.pageNumber === S.selected) {
        ctx.strokeStyle = "#6ea8fe"; ctx.lineWidth = 2;
        ctx.strokeRect(x + 1, y + 1, S.blockPx - 2, S.blockPx - 2);
      }
    }
  } else if (c.lod === "runs" && c.runs) {
    const overlay = overlayActive();
    let maxRun = 1;
    if (overlay) for (const r of c.runs.runs) maxRun = Math.max(maxRun, rangeMetric(r.startPage, r.endPage));
    for (const run of c.runs.runs) drawRun(run, cols, scroll, overlay, maxRun);
  }
}

function drawRun(run, cols, scroll, overlay, maxRun) {
  const s = run.startPage - 1, e = run.endPage - 1;
  const rowS = Math.floor(s / cols), rowE = Math.floor(e / cols);
  const base = colorForPage(run.objectId, run.pageType);
  const val = overlay ? rangeMetric(run.startPage, run.endPage) : 0;
  for (let row = rowS; row <= rowE; row++) {
    const y = row * cell() - scroll;
    if (y > S.cssH || y + S.blockPx < 0) continue;
    const c0 = row === rowS ? s % cols : 0;
    const c1 = row === rowE ? e % cols : cols - 1;
    const x = c0 * cell(), w = (c1 - c0 + 1) * cell() - GAP;
    ctx.globalAlpha = (overlay && val === 0) ? 0.3 : 1;
    ctx.fillStyle = base; ctx.fillRect(x, y, w, S.blockPx);
    ctx.globalAlpha = 1;
    if (overlay && val > 0) {
      ctx.fillStyle = `rgba(255,255,255,${(0.1 + 0.6 * val / maxRun).toFixed(3)})`;
      ctx.fillRect(x, y, w, S.blockPx);
    }
  }
}

// ---- tables view ----------------------------------------------------------
const HEADER_H = 22;
function rebuildBands() {
  const cols = colsFor();
  S.bands = []; let y = 0;
  for (const o of S.meta.objects) {
    const rows = Math.max(1, Math.ceil(o.pageCount / cols));
    const h = HEADER_H + rows * cell();
    S.bands.push({ obj: o, y, h }); y += h + 10;
  }
  S._tablesHeight = y;
}
function tablesContentHeight() { return S._tablesHeight; }

function ensureTablesData() {
  const cols = colsFor(), scroll = S.scroll.tables;
  for (const band of S.bands) {
    const top = band.y - scroll;
    if (top + band.h < 0 || top > S.cssH) continue;
    const firstRow = Math.max(0, Math.floor((scroll - band.y - HEADER_H) / cell()));
    const lastRow = Math.floor((scroll + S.cssH - band.y - HEADER_H) / cell());
    const from = Math.max(0, firstRow * cols);
    const to = Math.min(band.obj.pageCount - 1, (lastRow + 1) * cols);
    if (to < from || to - from + 1 > RANGE_CAP) continue;
    const key = `${band.obj.id}:${from}:${to}`;
    if (S.objPages.has(key)) continue;
    S.objPages.set(key, null);
    getJson(`/api/object/pages?objectId=${band.obj.id}&from=${from}&to=${to}`)
      .then((d) => { S.objPages.set(key, d); scheduleRender(); });
  }
}

function renderTables() {
  clear();
  const cols = colsFor(), scroll = S.scroll.tables;
  for (const band of S.bands) {
    const top = band.y - scroll;
    if (top > S.cssH || top + band.h < 0) continue;
    ctx.fillStyle = "#e6e8ec"; ctx.font = "12px sans-serif";
    ctx.textAlign = "left"; ctx.textBaseline = "alphabetic";
    ctx.fillText(`${band.obj.name}  ·  ${band.obj.type}  ·  ${band.obj.pageCount.toLocaleString()} pages`, 2, top + 14);
    for (const [key, data] of S.objPages) {
      if (!data || !key.startsWith(band.obj.id + ":")) continue;
      for (const p of data.pages) {
        const x = (p.ordinal % cols) * cell();
        const y = band.y + HEADER_H + Math.floor(p.ordinal / cols) * cell() - scroll;
        if (y > S.cssH || y + S.blockPx < 0) continue;
        drawBlock(x, y, colorForObject(band.obj.id), p.pageType, p.pageNumber);
      }
    }
  }
}

// ---- render ---------------------------------------------------------------
function render() {
  clampScroll();
  if (S.view === "pages") { ensurePagesData(); renderPages(); }
  else { ensureTablesData(); renderTables(); }
  updateSummary();
}

// ---- hit testing & popup --------------------------------------------------
function pageAt(mx, my) {
  const cols = colsFor();
  if (S.view === "pages") {
    const col = Math.floor(mx / cell());
    const idx = Math.floor((my + S.scroll.pages) / cell()) * cols + col;
    if (col < 0 || col >= cols || idx < 0 || idx >= S.pageCount) return null;
    return { pageNumber: idx + 1 };
  }
  for (const band of S.bands) {
    const top = band.y - S.scroll.tables;
    if (my < top || my > top + band.h) continue;
    const innerY = my - top - HEADER_H;
    if (innerY < 0) return null;
    const col = Math.floor(mx / cell());
    const ordinal = Math.floor(innerY / cell()) * cols + col;
    if (col < 0 || col >= cols || ordinal >= band.obj.pageCount) return null;
    for (const [key, data] of S.objPages) {
      if (!data || !key.startsWith(band.obj.id + ":")) continue;
      const hit = data.pages.find((p) => p.ordinal === ordinal);
      if (hit) return { pageNumber: hit.pageNumber };
    }
    return null;
  }
  return null;
}
function runAt(pageNumber) {
  const c = S.pagesCache;
  if (c.lod !== "runs" || !c.runs) return null;
  return c.runs.runs.find((r) => pageNumber >= r.startPage && pageNumber <= r.endPage);
}

let popupTimer = null;
function showPopup(evt) {
  const rect = canvas.getBoundingClientRect();
  const hit = pageAt(evt.clientX - rect.left, evt.clientY - rect.top);
  if (!hit) { hidePopup(); return; }

  if (S.view === "pages" && S.blockPx < LOD_THRESHOLD) {
    const run = runAt(hit.pageNumber);
    if (!run) { hidePopup(); return; }
    const obj = S.objById.get(run.objectId);
    let extra = "";
    if (overlayActive()) extra = `<tr><td>${S.metric}</td><td class="v">${rangeMetric(run.startPage, run.endPage)}</td></tr>`;
    placePopup(evt,
      `<h4>${run.pageType} run</h4><table>` +
      `<tr><td>object</td><td class="v">${obj ? obj.name : "—"}</td></tr>` +
      `<tr><td>pages</td><td class="v">${run.startPage}–${run.endPage} (${run.endPage - run.startPage + 1})</td></tr>` +
      extra + `</table><div class="ptrs">click to zoom in</div>`);
    return;
  }

  clearTimeout(popupTimer);
  popupTimer = setTimeout(async () => {
    const d = await getJson(`/api/page/${hit.pageNumber}`);
    if (!d) return;
    const obj = S.objById.get(d.objectId);
    const rows = [
      ["type", d.pageType],
      ["object", obj ? `${obj.name} (${obj.type})` : "—"],
      ["free bytes", d.freeBytes],
      ["cells", d.cellCount],
      ["rowid", (d.rowidMin != null) ? `${d.rowidMin}–${d.rowidMax}` : null],
    ];
    if (d.profile) rows.push(["reads / writes", `${d.profile.reads} / ${d.profile.writes}`]);
    let html = `<h4>Page ${d.pageNumber}</h4><table>`;
    for (const [k, v] of rows) if (v != null) html += `<tr><td>${k}</td><td class="v">${v}</td></tr>`;
    html += "</table>";
    if (d.pointers && d.pointers.length) {
      html += '<div class="ptrs">';
      for (const ptr of d.pointers.slice(0, 80)) html += `<a data-goto="${ptr.toPage}">${ptr.kind}→${ptr.toPage}</a>`;
      html += "</div>";
    }
    popup.innerHTML = html;
    popup.querySelectorAll("a[data-goto]").forEach((a) => a.addEventListener("click", () => goToPage(+a.dataset.goto)));
    place(evt);
  }, 60);
}
function placePopup(evt, html) { popup.innerHTML = html; place(evt); }
function place(evt) {
  const stage = document.getElementById("stage").getBoundingClientRect();
  popup.hidden = false;
  popup.style.left = (evt.clientX - stage.left + 14) + "px";
  popup.style.top = (evt.clientY - stage.top + 14) + "px";
}
function hidePopup() { clearTimeout(popupTimer); popup.hidden = true; }

function goToPage(n) {
  S.view = "pages"; syncTabs();
  S.scroll.pages = Math.max(0, Math.floor((n - 1) / colsFor()) * cell() - S.cssH / 2);
  S.selected = n;
  scheduleRender();
}

// ---- zoom & view ----------------------------------------------------------
function setZoom(px, anchorPage) {
  S.blockPx = Math.max(1, Math.min(40, px));
  if (S.view === "tables") rebuildBands();
  if (anchorPage && S.view === "pages") {
    S.scroll.pages = Math.max(0, Math.floor((anchorPage - 1) / colsFor()) * cell() - S.cssH / 2);
  }
  scheduleRender();  // page/object data stays valid (keyed by page/ordinal, not zoom)
}
function fitWidth() { setZoom(S.blockPx, null); }
function syncTabs() {
  document.getElementById("tab-pages").classList.toggle("active", S.view === "pages");
  document.getElementById("tab-tables").classList.toggle("active", S.view === "tables");
}
function setView(v) { S.view = v; syncTabs(); if (v === "tables") rebuildBands(); scheduleRender(); }

// ---- legend & summary -----------------------------------------------------
function renderLegend() {
  const legend = document.getElementById("legend");
  legend.innerHTML = "<h3>Page types</h3>";
  const counts = new Map((S.meta.typeCounts || []).map((t) => [t.pageType, t.count]));
  for (const type of Object.keys(GLYPH)) {
    const n = counts.get(type) || 0;
    if (n === 0) continue;
    const sw = STRUCTURAL[type] || "#30343d";
    legend.insertAdjacentHTML("beforeend",
      `<div class="legend-row"><span class="swatch" style="background:${sw}">${GLYPH[type]}</span>` +
      `<span class="name">${type}</span><span class="count">${n.toLocaleString()}</span></div>`);
  }
  legend.insertAdjacentHTML("beforeend", "<h3>Tables &amp; indexes</h3>");
  for (const o of S.meta.objects) {
    legend.insertAdjacentHTML("beforeend",
      `<div class="legend-row clickable" data-object-id="${o.id}" title="Go to ${o.name}">` +
      `<span class="swatch" style="background:${colorForObject(o.id)}"></span>` +
      `<span class="name">${o.name}</span><span class="count">${o.pageCount.toLocaleString()}</span></div>`);
  }
  legend.onclick = (e) => {
    const row = e.target.closest("[data-object-id]");
    if (row) navigateToObject(parseInt(row.dataset.objectId, 10));
  };
}

// Navigates the current view to the start of an object's blocks.
async function navigateToObject(id) {
  const obj = S.objById.get(id);
  if (!obj) return;
  if (S.view === "tables") {
    if (!S.bands.length) rebuildBands();
    const band = S.bands.find((b) => b.obj.id === id);
    if (band) { S.scroll.tables = Math.max(0, band.y); scheduleRender(); }
    return;
  }
  // Pages view: jump to the object's first page (lowest page number it owns).
  const d = await getJson(`/api/object/pages?objectId=${id}&from=0&to=0`);
  if (d && d.pages && d.pages.length) goToPage(d.pages[0].pageNumber);
}
function updateSummary() {
  const m = S.meta.meta;
  let text = `${m.pageCount.toLocaleString()} pages · ${m.pageSize}B · ${S.meta.objects.length} objects · ${S.blockPx}px`;
  if (S.hasProfile) text += " · profile loaded";
  document.getElementById("summary").textContent = text;
}

// ---- events ---------------------------------------------------------------
function initEvents() {
  document.getElementById("tab-pages").onclick = () => setView("pages");
  document.getElementById("tab-tables").onclick = () => setView("tables");
  document.getElementById("zoom-in").onclick = () => setZoom(S.blockPx + 2, null);
  document.getElementById("zoom-out").onclick = () => setZoom(S.blockPx - 2, null);
  document.getElementById("zoom-fit").onclick = fitWidth;
  document.getElementById("metric").onchange = (e) => { S.metric = e.target.value; scheduleRender(); };

  canvas.addEventListener("wheel", (e) => {
    e.preventDefault();
    if (e.ctrlKey || e.metaKey) {
      const rect = canvas.getBoundingClientRect();
      const anchor = Math.floor((e.clientY - rect.top + S.scroll.pages) / cell()) * colsFor() + 1;
      setZoom(S.blockPx + (e.deltaY < 0 ? 2 : -2), anchor);
    } else {
      S.scroll[S.view] += e.deltaY;
      scheduleRender();
    }
  }, { passive: false });

  canvas.addEventListener("mousemove", showPopup);
  canvas.addEventListener("mouseleave", hidePopup);
  popup.addEventListener("mouseenter", () => clearTimeout(popupTimer));
  popup.addEventListener("mouseleave", hidePopup);
  canvas.addEventListener("click", (e) => {
    const rect = canvas.getBoundingClientRect();
    const hit = pageAt(e.clientX - rect.left, e.clientY - rect.top);
    if (!hit) return;
    if (S.view === "pages" && S.blockPx < LOD_THRESHOLD) {
      const run = runAt(hit.pageNumber);
      if (run) setZoom(12, run.startPage);
    } else {
      S.selected = hit.pageNumber; scheduleRender();
    }
  });

  window.addEventListener("resize", resizeCanvas);
  initResizer();
}

function initResizer() {
  const resizer = document.getElementById("resizer");
  const legend = document.getElementById("legend");
  const saved = localStorage.getItem("legendWidth");
  if (saved) legend.style.width = saved + "px";
  let dragging = false;
  resizer.addEventListener("mousedown", (e) => { dragging = true; e.preventDefault(); });
  window.addEventListener("mousemove", (e) => {
    if (!dragging) return;
    const w = Math.max(140, Math.min(window.innerWidth * 0.6, window.innerWidth - e.clientX - 3));
    legend.style.width = w + "px";
    resizeCanvas();
  });
  window.addEventListener("mouseup", () => {
    if (!dragging) return;
    dragging = false;
    localStorage.setItem("legendWidth", parseInt(legend.style.width, 10));
  });
}

// ---- boot -----------------------------------------------------------------
async function main() {
  S.meta = await getJson("/api/meta");
  S.pageCount = S.meta.meta.pageCount;
  S.hasProfile = !!S.meta.hasProfile;
  for (const o of S.meta.objects) S.objById.set(o.id, o);
  if (S.hasProfile) {
    const all = await getJson(`/api/profile/pages?from=1&to=${S.pageCount}`);
    if (all) buildProfile(all);
    S.metric = "total";
    document.getElementById("metric").value = "total";
  }
  renderLegend();
  initEvents();
  resizeCanvas();
}
main();
