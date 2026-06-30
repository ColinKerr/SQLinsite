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
  // profile (reloaded when the session/query selection changes)
  sessions: [], selLeaves: new Set(), leafCount: 0,
  profMap: new Map(), profSorted: [], prefReads: [0], prefWrites: [0],
  profMax: { reads: 1, writes: 1, total: 1 },
  dpr: 1, cssW: 0, cssH: 0, _pending: false,
};

const canvas = document.getElementById("canvas");
const ctx = canvas.getContext("2d");
const popup = document.getElementById("popup");
const minimap = document.getElementById("minimap");
const mctx = minimap.getContext("2d");

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
function overlayActive() { return S.metric !== "none" && S.hasProfile; }
// `&sel=` fragment for the current selection; empty when all leaves are on
// (the server treats "no sel" as "all").
function selParam() {
  if (!S.hasProfile) return "";
  if (S.selLeaves.size === 0) return "&sel=-1";    // explicit empty selection (no leaf)
  if (S.selLeaves.size === S.leafCount) return "";  // all selected ⇒ server default
  return "&sel=" + [...S.selLeaves].join(",");
}
function globalMax() {
  return S.metric === "reads" ? S.profMax.reads : S.metric === "writes" ? S.profMax.writes : S.profMax.total;
}
// Max single-page metric over [from,to] among accessed pages — used to shade a
// zoomed-out run on the same scale as per-block view.
function rangeMax(from, to) {
  const lo = lowerBound(S.profSorted, from), hi = upperBound(S.profSorted, to);
  let m = 0;
  for (let i = lo; i < hi; i++) m = Math.max(m, metricForPage(S.profSorted[i]));
  return m;
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
  S.profMap = new Map(); S.profSorted = []; S.prefReads = [0]; S.prefWrites = [0];
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

// Refetches the (selection-filtered) profile and rebuilds the lookup. The
// page/run grid data is selection-independent (the overlay is applied at draw
// time from profMap), so no cache invalidation is needed — just re-render.
async function reloadProfile() {
  if (!S.hasProfile) return;
  const all = await getJson(`/api/profile/pages?from=1&to=${S.pageCount}${selParam()}`);
  if (all) buildProfile(all);
  scheduleRender();
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
  S.miniW = minimap.clientWidth;
  S.miniH = minimap.clientHeight;
  minimap.width = Math.floor(S.miniW * S.dpr);
  minimap.height = Math.floor(S.miniH * S.dpr);
  mctx.setTransform(S.dpr, 0, 0, S.dpr, 0, 0);
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

  // Always fetch structural runs so the whole file is drawn with no gaps. The
  // overlay shades each run by its access intensity (drawing unaccessed runs
  // lightened, like untouched blocks) rather than omitting them — omitting would
  // leave bare-background "black" stripes wherever the selection didn't touch.
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
// Overlay fill drawn over a cell's base color when a profile overlay is active.
// Touched cells get a white tint that brightens with access intensity; cells the
// current profile never touched get a black veil that darkens them so the touched
// cells stand out. The veil is capped (<1), so an untouched cell is never drawn
// fully black — it keeps a fraction of its base color.
const UNTOUCHED_DARKEN = 0.6;  // black-veil alpha for untouched cells
function overlayFill(v) {
  return v > 0 ? `rgba(255,255,255,${(0.12 + 0.6 * v / globalMax()).toFixed(3)})`
               : `rgba(0,0,0,${UNTOUCHED_DARKEN})`;
}
function drawBlock(x, y, color, pageType, pageNumber) {
  const overlay = overlayActive();
  ctx.fillStyle = color;
  ctx.fillRect(x, y, S.blockPx, S.blockPx);
  if (overlay) {
    ctx.fillStyle = overlayFill(metricForPage(pageNumber));
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
    for (const run of c.runs.runs) drawRun(run, cols, scroll, overlay);
  }
}

function drawRun(run, cols, scroll, overlay) {
  const s = run.startPage - 1, e = run.endPage - 1;
  const rowS = Math.floor(s / cols), rowE = Math.floor(e / cols);
  const base = colorForPage(run.objectId, run.pageType);
  // Shade like blocks: a run's intensity is its brightest page on the global scale.
  const v = overlay ? rangeMax(run.startPage, run.endPage) : 0;
  for (let row = rowS; row <= rowE; row++) {
    const y = row * cell() - scroll;
    if (y > S.cssH || y + S.blockPx < 0) continue;
    const c0 = row === rowS ? s % cols : 0;
    const c1 = row === rowE ? e % cols : cols - 1;
    const x = c0 * cell(), w = (c1 - c0 + 1) * cell() - GAP;
    ctx.fillStyle = base; ctx.fillRect(x, y, w, S.blockPx);
    if (overlay) {
      ctx.fillStyle = overlayFill(v);
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
  renderMinimap();
  updateSummary();
}

// ---- minimap (scaled image of the whole view; click/drag to scroll) --------
function initMinimap() {
  const scrollTo = (clientY) => {
    const rect = minimap.getBoundingClientRect();
    const frac = Math.max(0, Math.min(1, (clientY - rect.top) / rect.height));
    const contentH = S.view === "pages" ? pagesContentHeight() : tablesContentHeight();
    S.scroll[S.view] = Math.max(0, frac * contentH - S.cssH / 2);
    scheduleRender();
  };
  let dragging = false;
  minimap.addEventListener("mousedown", (e) => { dragging = true; scrollTo(e.clientY); e.preventDefault(); });
  window.addEventListener("mousemove", (e) => { if (dragging) scrollTo(e.clientY); });
  window.addEventListener("mouseup", () => { dragging = false; });
}

function renderMinimap() {
  const w = S.miniW || 0, h = S.miniH || 0;
  if (w === 0 || h === 0) return;
  mctx.clearRect(0, 0, w, h);
  const contentH = S.view === "pages" ? pagesContentHeight() : tablesContentHeight();
  if (contentH <= 0) return;
  const scale = h / contentH;
  if (S.view === "pages") {
    const cols = colsFor();
    for (const run of (S.allRuns || [])) {
      const rowS = Math.floor((run.startPage - 1) / cols);
      const rowE = Math.floor((run.endPage - 1) / cols);
      mctx.fillStyle = colorForPage(run.objectId, run.pageType);
      mctx.fillRect(0, rowS * cell() * scale, w, Math.max(0.5, (rowE - rowS + 1) * cell() * scale));
    }
  } else {
    for (const band of S.bands) {
      mctx.fillStyle = colorForObject(band.obj.id);
      mctx.fillRect(0, band.y * scale, w, Math.max(0.5, band.h * scale));
    }
  }
  // viewport indicator
  const vy = S.scroll[S.view] * scale, vh = Math.max(2, S.cssH * scale);
  mctx.fillStyle = "rgba(110,168,254,.22)"; mctx.fillRect(0, vy, w, vh);
  mctx.strokeStyle = "rgba(255,255,255,.8)"; mctx.lineWidth = 1;
  mctx.strokeRect(0.5, vy + 0.5, w - 1, vh - 1);
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

// Like goToPage, but lands the block's row at the top-left of the view (used for
// "scroll to the first block of this object"). The position is computed from the
// page number alone, so it works whether or not the block is loaded yet.
function scrollToBlock(n) {
  S.view = "pages"; syncTabs();
  S.scroll.pages = Math.max(0, Math.floor((n - 1) / colsFor()) * cell());
  S.selected = n;
  scheduleRender();
}

// ---- zoom & view ----------------------------------------------------------
// The page currently at the grid's top-left corner (pages view).
function topLeftPage() { return Math.floor(S.scroll.pages / cell()) * colsFor() + 1; }
// Re-zooms, keeping `anchorPage` pinned at viewport y `anchorY`. With no anchor,
// the top-left page stays in the top-left corner across the zoom change.
function setZoom(px, anchorPage, anchorY) {
  px = Math.max(1, Math.min(40, px));
  if (anchorPage == null && S.view === "pages") { anchorPage = topLeftPage(); anchorY = 0; }
  S.blockPx = px;
  if (S.view === "tables") rebuildBands();
  if (anchorPage != null && S.view === "pages") {
    S.scroll.pages = Math.max(0, Math.floor((anchorPage - 1) / colsFor()) * cell() - (anchorY || 0));
  }
  scheduleRender();  // page/object data stays valid (keyed by page/ordinal, not zoom)
}
function fitWidth() { setZoom(S.blockPx); }
function syncTabs() {
  document.getElementById("tab-pages").classList.toggle("active", S.view === "pages");
  document.getElementById("tab-tables").classList.toggle("active", S.view === "tables");
}
function setView(v) { S.view = v; syncTabs(); if (v === "tables") rebuildBands(); scheduleRender(); }

// ---- legend & summary -----------------------------------------------------
function escapeHtml(s) {
  return String(s).replace(/[&<>"]/g, (c) =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
}
function objectRowHtml(o, child) {
  return `<div class="legend-row clickable${child ? " child" : ""}" data-object-id="${o.id}" title="Go to ${escapeHtml(o.name)}">` +
    `<span class="swatch" style="background:${colorForObject(o.id)}"></span>` +
    `<span class="name">${escapeHtml(o.name)}</span><span class="count">${o.pageCount.toLocaleString()}</span></div>`;
}
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

  // Tables are roots; their indexes (matched by tableName) are nested children.
  legend.insertAdjacentHTML("beforeend", "<h3>Tables &amp; indexes</h3>");
  const indexesByTable = new Map();
  for (const o of S.meta.objects) {
    if (o.type === "table") continue;
    const key = o.tableName || "";
    (indexesByTable.get(key) || indexesByTable.set(key, []).get(key)).push(o);
  }
  const claimed = new Set();
  for (const o of S.meta.objects) {
    if (o.type !== "table") continue;
    legend.insertAdjacentHTML("beforeend", objectRowHtml(o, false));
    for (const idx of (indexesByTable.get(o.name) || [])) {
      legend.insertAdjacentHTML("beforeend", objectRowHtml(idx, true));
      claimed.add(idx.id);
    }
  }
  // Orphan indexes (no matching table object) render as roots.
  for (const o of S.meta.objects) {
    if (o.type !== "table" && !claimed.has(o.id)) {
      legend.insertAdjacentHTML("beforeend", objectRowHtml(o, false));
    }
  }

  renderSessions(legend);

  legend.onclick = (e) => {
    if (e.target.closest("#sessions")) return;  // checkbox handlers own this region
    const row = e.target.closest("[data-object-id]");
    if (row) navigateToObject(parseInt(row.dataset.objectId, 10));
  };
}

// Profile session/statement checkbox tree (each query = one leaf id).
function renderSessions(legend) {
  if (!S.hasProfile || !S.sessions.length) return;
  let html = '<h3>Profile sessions</h3><div id="sessions">';
  for (const s of S.sessions) {
    const ids = s.leaves.map((l) => l.leafId).join(",");
    html += `<div class="session"><label><input type="checkbox" class="ses" data-leaves="${ids}">` +
      `<span class="name">${escapeHtml(s.session)}</span></label><div class="stmts">`;
    for (const l of s.leaves) {
      html += `<label><input type="checkbox" class="leaf" data-leaf="${l.leafId}">stmt ${l.statementIndex}</label>`;
    }
    html += "</div></div>";
  }
  legend.insertAdjacentHTML("beforeend", html + "</div>");

  legend.querySelectorAll("#sessions input.leaf").forEach((cb) =>
    cb.addEventListener("change", () => {
      const id = +cb.dataset.leaf;
      if (cb.checked) S.selLeaves.add(id); else S.selLeaves.delete(id);
      syncSessionChecks(); reloadProfile();
    }));
  legend.querySelectorAll("#sessions input.ses").forEach((cb) =>
    cb.addEventListener("change", () => {
      for (const id of cb.dataset.leaves.split(",").map(Number)) {
        if (cb.checked) S.selLeaves.add(id); else S.selLeaves.delete(id);
      }
      syncSessionChecks(); reloadProfile();
    }));
  syncSessionChecks();
}

// Reflects S.selLeaves in the checkbox tree (parent shows indeterminate when its
// statements are partially selected).
function syncSessionChecks() {
  document.querySelectorAll("#sessions input.leaf").forEach((cb) => {
    cb.checked = S.selLeaves.has(+cb.dataset.leaf);
  });
  document.querySelectorAll("#sessions input.ses").forEach((cb) => {
    const ids = cb.dataset.leaves.split(",").map(Number);
    const on = ids.filter((id) => S.selLeaves.has(id)).length;
    cb.checked = on === ids.length;
    cb.indeterminate = on > 0 && on < ids.length;
  });
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
  // Pages view: bring the object's first block to the top-left corner. Prefer
  // meta's startPage; fall back to querying the object's first page so this works
  // regardless of what is currently loaded on the front-end.
  let first = obj.startPage;
  if (!first) {
    const d = await getJson(`/api/object/pages?objectId=${id}&from=0&to=0`);
    first = (d && d.pages && d.pages.length) ? d.pages[0].pageNumber : null;
  }
  if (first) scrollToBlock(first);
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
  document.getElementById("metric").onchange = (e) => {
    S.metric = e.target.value;  // overlay is applied at draw time; just re-render
    scheduleRender();
  };
  initMinimap();

  canvas.addEventListener("wheel", (e) => {
    e.preventDefault();
    if (e.ctrlKey || e.metaKey) {
      const rect = canvas.getBoundingClientRect();
      const my = e.clientY - rect.top;
      const anchor = Math.floor((my + S.scroll.pages) / cell()) * colsFor() + 1;
      setZoom(S.blockPx + (e.deltaY < 0 ? 2 : -2), anchor, my);
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
  S.sessions = S.meta.sessions || [];
  for (const o of S.meta.objects) S.objById.set(o.id, o);

  // Whole-file run map for the minimap (structural; independent of selection).
  const allRuns = await getJson(`/api/runs?from=1&to=${S.pageCount}`);
  S.allRuns = allRuns ? allRuns.runs : [];

  if (S.hasProfile) {
    // Default: every session/statement selected.
    for (const s of S.sessions) for (const l of s.leaves) S.selLeaves.add(l.leafId);
    S.leafCount = S.selLeaves.size;
    const all = await getJson(`/api/profile/pages?from=1&to=${S.pageCount}${selParam()}`);
    if (all) buildProfile(all);
    S.metric = "total";
    document.getElementById("metric").value = "total";
  }
  renderLegend();
  initEvents();
  resizeCanvas();
}
main();
