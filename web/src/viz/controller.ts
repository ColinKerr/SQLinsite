import {
  fetchObjectPageOrdinal, fetchObjectPages, fetchPage, fetchPages, fetchRuns,
  fetchStructuralPageOrdinal, fetchStructuralPages,
} from "../core/api.ts";
import { BG, GAP, HEADER_H, LOD_THRESHOLD, RANGE_CAP } from "../core/constants.ts";
import { bestFitBlockPx, cell, colsFor, pagesContentHeight, scrollForPageAtY, topLeftPage, visiblePageRange }
  from "../core/layout.ts";
import { colorForObject, colorForPage, GLYPH, STRUCTURAL } from "../core/palette.ts";
import { overlayFill } from "../core/overlay.ts";
import { formatCount } from "../core/format.ts";
import type { ObjectPagesResponse, PagesResponse, Run, RunsResponse, View }
  from "../core/types.ts";
import type { VizState } from "../state/store.ts";
import type { StoreApi } from "zustand";

// A Tables-view band: a schema object, or a structural page group (Freelist,
// Lock-Byte, All other pages). `objectId` colors object bands (and fetches their
// pages); it is null for structural groups, whose blocks are colored per page type
// and whose pages are fetched by `structuralKey`.
interface TableGroup {
  key: string;                  // band identity + page-cache prefix: `o:<id>` | `s:<key>`
  label: string;                // header text (before the page count)
  pageCount: number;
  objectId: number | null;      // schema object id, or null for a structural group
  structuralKey: string | null; // "freelist" | "lockbyte" | "other", or null
}
interface Band { grp: TableGroup; y: number; h: number; }

// The structural Tables-view band a page belongs to, derived from its page type
// (mirrors the backend's structuralGroupWhere). Used to scroll a structural page
// node — which carries no objectId — to its band.
function structuralKeyForPage(pageType: string | null): string {
  if (pageType === "freelist-trunk" || pageType === "freelist-leaf") return "freelist";
  if (pageType === "lock-byte") return "lockbyte";
  if (pageType === "pointer-map") return "pointermap";
  return "other";
}

// Owns the canvas + minimap + popup DOM and the hot render loop. Shared UI state
// (view, blockPx, metric, profile, …) comes from the Zustand store; render-loop
// internals (scroll, caches, canvas size, selection) stay here so panning/zoom
// never re-render React.
export class CanvasController {
  private ctx: CanvasRenderingContext2D;
  private mctx: CanvasRenderingContext2D;

  private cssW = 0;
  private cssH = 0;
  private dpr = 1;
  private miniW = 0;
  private miniH = 0;

  private scroll: Record<View, number> = { pages: 0, tables: 0, query: 0, tree: 0 };
  private selected = 0;
  private lastSelectedObject: number | null = null;

  private pagesCache: {
    lod: "pages" | "runs" | null; from: number; to: number;
    pages: PagesResponse | null; runs: RunsResponse | null;
  } = { lod: null, from: 0, to: 0, pages: null, runs: null };
  private pagesFetchKey: string | null = null;
  private objPages = new Map<string, ObjectPagesResponse | null>();
  private bands: Band[] = [];
  private tablesHeight = 0;

  private pending = false;
  private popupTimer: number | null = null;
  private cleanups: Array<() => void> = [];

  constructor(
    private canvas: HTMLCanvasElement,
    private minimap: HTMLCanvasElement,
    private popup: HTMLElement,
    private stage: HTMLElement,
    private store: StoreApi<VizState>,
  ) {
    this.ctx = canvas.getContext("2d")!;
    this.mctx = minimap.getContext("2d")!;
  }

  private get s() { return this.store.getState(); }
  private blockPx() { return this.s.blockPx; }
  private cell() { return cell(this.blockPx()); }
  private cols() { return colsFor(this.cssW, this.blockPx()); }
  private overlayActive() { return this.s.metric !== "none" && this.s.hasProfile; }

  // ---- lifecycle ----------------------------------------------------------
  mount() {
    const ro = new ResizeObserver(() => this.resize());
    ro.observe(this.stage);
    this.cleanups.push(() => ro.disconnect());

    // Scroll to the history-selected object when it changes (e.g. Back/Forward or
    // a Navigation-panel click), and re-render on any store change.
    this.lastSelectedObject = this.s.selectedObject;
    this.cleanups.push(this.store.subscribe(() => {
      const o = this.s.selectedObject;
      if (o != null && o !== this.lastSelectedObject) { void this.navigateToObject(o); }
      this.lastSelectedObject = o;
      this.scheduleRender();
    }));

    this.addListener(this.canvas, "wheel", this.onWheel as EventListener, { passive: false });
    this.addListener(this.canvas, "mousemove", this.onMouseMove as EventListener);
    this.addListener(this.canvas, "mouseleave", () => this.hidePopup());
    this.addListener(this.canvas, "click", this.onClick as EventListener);
    this.addListener(this.popup, "mouseenter", () => this.clearPopupTimer());
    this.addListener(this.popup, "mouseleave", () => this.hidePopup());
    this.initMinimap();

    this.resize();
    // Restore the selected object after the first layout (bands/cols are ready).
    if (this.s.selectedObject != null) {
      const id = this.s.selectedObject;
      requestAnimationFrame(() => { void this.navigateToObject(id); });
    }
  }

  unmount() {
    for (const c of this.cleanups) c();
    this.cleanups = [];
  }

  private addListener(
    el: EventTarget, type: string, fn: EventListener, opts?: AddEventListenerOptions,
  ) {
    el.addEventListener(type, fn, opts);
    this.cleanups.push(() => el.removeEventListener(type, fn, opts));
  }

  // ---- sizing -------------------------------------------------------------
  resize = () => {
    this.cssW = this.stage.clientWidth;
    this.cssH = this.stage.clientHeight;
    this.dpr = window.devicePixelRatio || 1;
    this.canvas.width = Math.floor(this.cssW * this.dpr);
    this.canvas.height = Math.floor(this.cssH * this.dpr);
    this.ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    this.miniW = this.minimap.clientWidth;
    this.miniH = this.minimap.clientHeight;
    this.minimap.width = Math.floor(this.miniW * this.dpr);
    this.minimap.height = Math.floor(this.miniH * this.dpr);
    this.mctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    this.scheduleRender();
  };

  // ---- render loop --------------------------------------------------------
  scheduleRender() {
    if (this.pending) return;
    this.pending = true;
    requestAnimationFrame(() => { this.pending = false; this.render(); });
  }

  private render() {
    if (!this.s.meta) return;
    this.clampScroll();
    if (this.s.view === "pages") { this.ensurePagesData(); this.renderPages(); }
    else { this.rebuildBands(); this.ensureTablesData(); this.renderTables(); }
    this.renderMinimap();
  }

  private clampScroll() {
    const v = this.s.view;
    const h = v === "pages" ? this.pagesContentHeight() : this.tablesHeight;
    this.scroll[v] = Math.max(0, Math.min(this.scroll[v], Math.max(0, h - this.cssH)));
  }

  private pagesContentHeight() {
    return pagesContentHeight(this.s.pageCount, this.cssW, this.blockPx());
  }

  // Total height of the Tables layout at a hypothetical zoom (mirrors
  // rebuildBands), so `fit()` can evaluate candidate zoom levels without mutating
  // state.
  private tablesHeightAt(bp: number): number {
    const cols = colsFor(this.cssW, bp);
    const c = cell(bp);
    let y = 0;
    for (const grp of this.tableGroups()) {
      const rows = Math.max(1, Math.ceil(grp.pageCount / cols));
      y += HEADER_H + rows * c + 10;
    }
    return y;
  }

  // ---- pages-view data (no-flash: keep old until new arrives) -------------
  private async ensurePagesData() {
    const [vf, vt] = visiblePageRange(
      this.scroll.pages, this.cssW, this.cssH, this.blockPx(), this.s.pageCount,
    );
    const lod = this.blockPx() >= LOD_THRESHOLD ? "pages" : "runs";
    const c = this.pagesCache;
    const have = lod === "pages" ? c.pages : c.runs;
    if (have && c.lod === lod && c.from <= vf && c.to >= vt) return;

    const margin = Math.ceil(this.cssH / this.cell()) * this.cols();
    const from = Math.max(1, vf - margin);
    const to = Math.min(this.s.pageCount, vt + margin);
    const key = `${lod}:${from}:${to}`;
    if (this.pagesFetchKey === key) return;
    this.pagesFetchKey = key;

    const data = lod === "pages" ? await fetchPages(from, to) : await fetchRuns(from, to);
    if (this.pagesFetchKey !== key || !data) return; // superseded
    c.lod = lod; c.from = from; c.to = to;
    if (lod === "pages") c.pages = data as PagesResponse;
    else c.runs = data as RunsResponse;
    this.scheduleRender();
  }

  // ---- drawing ------------------------------------------------------------
  private clear() { this.ctx.fillStyle = BG; this.ctx.fillRect(0, 0, this.cssW, this.cssH); }

  private drawGlyph(type: string, x: number, y: number) {
    const bp = this.blockPx();
    if (bp < 14) return;
    this.ctx.fillStyle = "rgba(16,18,26,.85)";
    this.ctx.font = `${Math.min(11, bp - 3)}px sans-serif`;
    this.ctx.textAlign = "center"; this.ctx.textBaseline = "middle";
    this.ctx.fillText(GLYPH[type] || "?", x + bp / 2, y + bp / 2);
  }

  private drawBlock(x: number, y: number, color: string, pageType: string, pageNumber: number) {
    const bp = this.blockPx();
    this.ctx.fillStyle = color;
    this.ctx.fillRect(x, y, bp, bp);
    if (this.overlayActive()) {
      const { profile, metric } = this.s;
      this.ctx.fillStyle = overlayFill(profile.metricForPage(pageNumber, metric), profile.globalMax(metric));
      this.ctx.fillRect(x, y, bp, bp);
    }
    this.drawGlyph(pageType, x, y);
  }

  private renderPages() {
    this.clear();
    const bp = this.blockPx(), cols = this.cols(), scroll = this.scroll.pages, c = this.pagesCache;
    if (c.lod === "pages" && c.pages) {
      for (const p of c.pages.pages) {
        const idx = p.pageNumber - 1;
        const x = (idx % cols) * this.cell(), y = Math.floor(idx / cols) * this.cell() - scroll;
        if (y > this.cssH || y + bp < 0) continue;
        this.drawBlock(x, y, colorForPage(p.objectId, p.pageType), p.pageType, p.pageNumber);
        if (p.pageNumber === this.selected) {
          this.ctx.strokeStyle = "#6ea8fe"; this.ctx.lineWidth = 2;
          this.ctx.strokeRect(x + 1, y + 1, bp - 2, bp - 2);
        }
      }
    } else if (c.lod === "runs" && c.runs) {
      for (const run of c.runs.runs) this.drawRun(run, cols, scroll);
    }
  }

  private drawRun(run: Run, cols: number, scroll: number) {
    const bp = this.blockPx();
    const s = run.startPage - 1, e = run.endPage - 1;
    const rowS = Math.floor(s / cols), rowE = Math.floor(e / cols);
    const base = colorForPage(run.objectId, run.pageType);
    const { profile, metric } = this.s;
    const v = this.overlayActive() ? profile.rangeMax(run.startPage, run.endPage, metric) : 0;
    const fill = this.overlayActive() ? overlayFill(v, profile.globalMax(metric)) : null;
    for (let row = rowS; row <= rowE; row++) {
      const y = row * this.cell() - scroll;
      if (y > this.cssH || y + bp < 0) continue;
      const c0 = row === rowS ? s % cols : 0;
      const c1 = row === rowE ? e % cols : cols - 1;
      const x = c0 * this.cell(), w = (c1 - c0 + 1) * this.cell() - GAP;
      this.ctx.fillStyle = base; this.ctx.fillRect(x, y, w, bp);
      if (fill) { this.ctx.fillStyle = fill; this.ctx.fillRect(x, y, w, bp); }
    }
  }

  // ---- tables view --------------------------------------------------------
  // The ordered Tables-view groups: one band per schema object, then a band per
  // present structural page group (Freelist, Lock-Byte, All other pages) so every
  // page in the file appears in some band.
  private tableGroups(): TableGroup[] {
    const groups: TableGroup[] = this.s.objects.map((o) => ({
      key: `o:${o.id}`, label: `${o.name}  ·  ${o.type}`, pageCount: o.pageCount,
      objectId: o.id, structuralKey: null,
    }));
    for (const g of this.s.structuralGroups) {
      groups.push({ key: `s:${g.key}`, label: g.label, pageCount: g.pageCount,
                    objectId: null, structuralKey: g.key });
    }
    return groups;
  }

  private fetchGroupPages(grp: TableGroup, from: number, to: number): Promise<ObjectPagesResponse | null> {
    return grp.objectId != null
      ? fetchObjectPages(grp.objectId, from, to)
      : fetchStructuralPages(grp.structuralKey as string, from, to);
  }

  // A representative band color for the minimap: the object palette color, or a
  // fixed structural tint (the "All other pages" group is mixed, so use gray).
  private bandColor(grp: TableGroup): string {
    if (grp.objectId != null) return colorForObject(grp.objectId);
    if (grp.structuralKey === "freelist") return STRUCTURAL["freelist-trunk"];
    if (grp.structuralKey === "lockbyte") return STRUCTURAL["lock-byte"];
    return STRUCTURAL["unallocated"];
  }

  private rebuildBands() {
    const cols = this.cols();
    this.bands = []; let y = 0;
    for (const grp of this.tableGroups()) {
      const rows = Math.max(1, Math.ceil(grp.pageCount / cols));
      const h = HEADER_H + rows * this.cell();
      this.bands.push({ grp, y, h }); y += h + 10;
    }
    this.tablesHeight = y;
  }

  private ensureTablesData() {
    const cols = this.cols(), scroll = this.scroll.tables;
    for (const band of this.bands) {
      const top = band.y - scroll;
      if (top + band.h < 0 || top > this.cssH) continue;
      const firstRow = Math.max(0, Math.floor((scroll - band.y - HEADER_H) / this.cell()));
      const lastRow = Math.floor((scroll + this.cssH - band.y - HEADER_H) / this.cell());
      const from = Math.max(0, firstRow * cols);
      const to = Math.min(band.grp.pageCount - 1, (lastRow + 1) * cols);
      if (to < from || to - from + 1 > RANGE_CAP) continue;
      const key = `${band.grp.key}:${from}:${to}`;
      if (this.objPages.has(key)) continue;
      this.objPages.set(key, null);
      this.fetchGroupPages(band.grp, from, to).then((d) => {
        this.objPages.set(key, d); this.scheduleRender();
      });
    }
  }

  private renderTables() {
    this.clear();
    const bp = this.blockPx(), cols = this.cols(), scroll = this.scroll.tables;
    for (const band of this.bands) {
      const top = band.y - scroll;
      if (top > this.cssH || top + band.h < 0) continue;
      this.ctx.fillStyle = "#e6e8ec"; this.ctx.font = "12px sans-serif";
      this.ctx.textAlign = "left"; this.ctx.textBaseline = "alphabetic";
      this.ctx.fillText(
        `${band.grp.label}  ·  ${formatCount(band.grp.pageCount)} pages`,
        2, top + 14,
      );
      for (const [key, data] of this.objPages) {
        if (!data || !key.startsWith(band.grp.key + ":")) continue;
        for (const p of data.pages) {
          const x = (p.ordinal % cols) * this.cell();
          const y = band.y + HEADER_H + Math.floor(p.ordinal / cols) * this.cell() - scroll;
          if (y > this.cssH || y + bp < 0) continue;
          this.drawBlock(x, y, colorForPage(band.grp.objectId, p.pageType), p.pageType, p.pageNumber);
          if (p.pageNumber === this.selected) {
            this.ctx.strokeStyle = "#6ea8fe"; this.ctx.lineWidth = 2;
            this.ctx.strokeRect(x + 1, y + 1, bp - 2, bp - 2);
          }
        }
      }
    }
  }

  // ---- minimap ------------------------------------------------------------
  private initMinimap() {
    let dragging = false;
    const scrollTo = (clientY: number) => {
      const rect = this.minimap.getBoundingClientRect();
      const frac = Math.max(0, Math.min(1, (clientY - rect.top) / rect.height));
      const contentH = this.s.view === "pages" ? this.pagesContentHeight() : this.tablesHeight;
      this.scroll[this.s.view] = Math.max(0, frac * contentH - this.cssH / 2);
      this.scheduleRender();
    };
    this.addListener(this.minimap, "mousedown", ((e: MouseEvent) => {
      dragging = true; scrollTo(e.clientY); e.preventDefault();
    }) as EventListener);
    this.addListener(window, "mousemove", ((e: MouseEvent) => { if (dragging) scrollTo(e.clientY); }) as EventListener);
    this.addListener(window, "mouseup", () => { dragging = false; });
  }

  private renderMinimap() {
    const w = this.miniW, h = this.miniH;
    if (w === 0 || h === 0) return;
    this.mctx.clearRect(0, 0, w, h);
    const contentH = this.s.view === "pages" ? this.pagesContentHeight() : this.tablesHeight;
    if (contentH <= 0) return;
    const scale = h / contentH;
    if (this.s.view === "pages") {
      const cols = this.cols();
      for (const run of this.s.allRuns) {
        const rowS = Math.floor((run.startPage - 1) / cols);
        const rowE = Math.floor((run.endPage - 1) / cols);
        this.mctx.fillStyle = colorForPage(run.objectId, run.pageType);
        this.mctx.fillRect(0, rowS * this.cell() * scale, w, Math.max(0.5, (rowE - rowS + 1) * this.cell() * scale));
      }
    } else {
      for (const band of this.bands) {
        this.mctx.fillStyle = this.bandColor(band.grp);
        this.mctx.fillRect(0, band.y * scale, w, Math.max(0.5, band.h * scale));
      }
    }
    const vy = this.scroll[this.s.view] * scale, vh = Math.max(2, this.cssH * scale);
    this.mctx.fillStyle = "rgba(110,168,254,.22)"; this.mctx.fillRect(0, vy, w, vh);
    this.mctx.strokeStyle = "rgba(255,255,255,.8)"; this.mctx.lineWidth = 1;
    this.mctx.strokeRect(0.5, vy + 0.5, w - 1, vh - 1);
  }

  // ---- hit testing & popup ------------------------------------------------
  private pageAt(mx: number, my: number): { pageNumber: number } | null {
    const cols = this.cols();
    if (this.s.view === "pages") {
      const col = Math.floor(mx / this.cell());
      const idx = Math.floor((my + this.scroll.pages) / this.cell()) * cols + col;
      if (col < 0 || col >= cols || idx < 0 || idx >= this.s.pageCount) return null;
      return { pageNumber: idx + 1 };
    }
    for (const band of this.bands) {
      const top = band.y - this.scroll.tables;
      if (my < top || my > top + band.h) continue;
      const innerY = my - top - HEADER_H;
      if (innerY < 0) return null;
      const col = Math.floor(mx / this.cell());
      const ordinal = Math.floor(innerY / this.cell()) * cols + col;
      if (col < 0 || col >= cols || ordinal >= band.grp.pageCount) return null;
      for (const [key, data] of this.objPages) {
        if (!data || !key.startsWith(band.grp.key + ":")) continue;
        const hit = data.pages.find((p) => p.ordinal === ordinal);
        if (hit) return { pageNumber: hit.pageNumber };
      }
      return null;
    }
    return null;
  }

  private runAt(pageNumber: number): Run | null {
    const c = this.pagesCache;
    if (c.lod !== "runs" || !c.runs) return null;
    return c.runs.runs.find((r) => pageNumber >= r.startPage && pageNumber <= r.endPage) || null;
  }

  private onMouseMove = (evt: MouseEvent) => {
    const rect = this.canvas.getBoundingClientRect();
    const hit = this.pageAt(evt.clientX - rect.left, evt.clientY - rect.top);
    if (!hit) { this.hidePopup(); return; }

    if (this.s.view === "pages" && this.blockPx() < LOD_THRESHOLD) {
      const run = this.runAt(hit.pageNumber);
      if (!run) { this.hidePopup(); return; }
      const obj = this.s.objById.get(run.objectId ?? -1);
      const { profile, metric } = this.s;
      let extra = "";
      if (this.overlayActive()) {
        extra = `<tr><td>${metric}</td><td class="v">${profile.rangeMetric(run.startPage, run.endPage, metric)}</td></tr>`;
      }
      this.placePopup(evt,
        `<h4>${run.pageType} run</h4><table>` +
        `<tr><td>object</td><td class="v">${obj ? obj.name : "—"}</td></tr>` +
        `<tr><td>pages</td><td class="v">${run.startPage}–${run.endPage} (${run.endPage - run.startPage + 1})</td></tr>` +
        extra + `</table><div class="ptrs">click to zoom in</div>`);
      return;
    }

    this.clearPopupTimer();
    this.popupTimer = window.setTimeout(async () => {
      const d = await fetchPage(hit.pageNumber);
      if (!d) return;
      const obj = this.s.objById.get(d.objectId ?? -1);
      const rows: Array<[string, string | number | null]> = [
        ["type", d.pageType],
        ["object", obj ? `${obj.name} (${obj.type})` : "—"],
        ["free bytes", d.freeBytes],
        ["cells", d.cellCount],
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
      this.popup.innerHTML = html;
      this.popup.querySelectorAll<HTMLElement>("a[data-goto]").forEach((a) =>
        a.addEventListener("click", () => this.goToPage(Number(a.dataset.goto))));
      this.place(evt);
    }, 60);
  };

  private placePopup(evt: MouseEvent, html: string) { this.popup.innerHTML = html; this.place(evt); }
  private place(evt: MouseEvent) {
    const stage = this.stage.getBoundingClientRect();
    this.popup.hidden = false;
    this.popup.style.left = (evt.clientX - stage.left + 14) + "px";
    this.popup.style.top = (evt.clientY - stage.top + 14) + "px";
  }
  private clearPopupTimer() { if (this.popupTimer != null) { clearTimeout(this.popupTimer); this.popupTimer = null; } }
  private hidePopup() { this.clearPopupTimer(); this.popup.hidden = true; }

  // ---- navigation & zoom --------------------------------------------------
  // Scroll the Pages view to page `n`, centered, and select it. Public so the
  // shared b-tree tree can drive Pages-view navigation from a node click.
  goToPage(n: number) {
    this.store.getState().setView("pages");
    this.scroll.pages = Math.max(0, scrollForPageAtY(n, this.cssH / 2, this.cssW, this.blockPx()));
    this.selected = n;
    this.scheduleRender();
  }
  private scrollToBlock(n: number) {
    this.store.getState().setView("pages");
    this.scroll.pages = scrollForPageAtY(n, 0, this.cssW, this.blockPx());
    this.selected = n;
    this.scheduleRender();
  }

  // Scroll to and select page `n`'s block within the CURRENT view — never switching
  // views. Public so the shared b-tree tree can navigate from a node click: in the
  // Pages view the block is centered in the page grid; in the Tables view it is
  // centered in its band (an object band via objectId, else the structural band for
  // its page type).
  selectPageInView(n: number, objectId: number | null, pageType: string | null) {
    if (this.s.view === "tables") { void this.scrollToPageInBand(n, objectId, pageType); return; }
    this.scroll.pages = Math.max(0, scrollForPageAtY(n, this.cssH / 2, this.cssW, this.blockPx()));
    this.selected = n;
    this.scheduleRender();
  }

  // Tables view: center page `n`'s block in its band. Bands lay pages out by their
  // 0-based ordinal within the group (pages aren't contiguous), so we resolve that
  // ordinal from the server first. Object pages use objectId; structural pages (no
  // objectId) resolve to their group band via page type.
  private async scrollToPageInBand(n: number, objectId: number | null, pageType: string | null) {
    let band: Band | undefined;
    let ordinal = -1;
    if (objectId != null) {
      band = this.bands.find((b) => b.grp.objectId === objectId);
      if (!band) return;
      ordinal = (await fetchObjectPageOrdinal(objectId, n))?.ordinal ?? -1;
    } else {
      const key = structuralKeyForPage(pageType);
      band = this.bands.find((b) => b.grp.structuralKey === key);
      if (!band) return;
      ordinal = (await fetchStructuralPageOrdinal(key, n))?.ordinal ?? -1;
    }
    if (ordinal < 0) return;
    const rowTop = band.y + HEADER_H + Math.floor(ordinal / this.cols()) * this.cell();
    this.scroll.tables = Math.max(0, rowTop - this.cssH / 2);
    this.selected = n;
    this.scheduleRender();
  }

  // Public: called by the Zoom controls.
  setZoom(px: number, anchorPage?: number, anchorY?: number) {
    if (anchorPage == null && this.s.view === "pages") {
      anchorPage = topLeftPage(this.scroll.pages, this.cssW, this.blockPx());
      anchorY = 0;
    }
    this.store.getState().setBlockPx(px); // clamps to [MIN,MAX]
    if (anchorPage != null && this.s.view === "pages") {
      this.scroll.pages = scrollForPageAtY(anchorPage, anchorY || 0, this.cssW, this.blockPx());
    }
    this.scheduleRender();
  }
  zoomBy(delta: number) { this.setZoom(this.blockPx() + delta); }

  // Best-fit: pick the largest zoom at which the whole view fits vertically — the
  // full file (Pages) or all object bands (Tables) — zooming in or out as needed,
  // or the maximum zoom-out when it cannot fit. If everything fits, scroll to the
  // top; if it cannot fit, leave the scroll position unchanged.
  fit() {
    if (this.cssW <= 0 || this.cssH <= 0) return;
    const pages = this.s.view === "pages";
    if (pages ? this.s.pageCount <= 0 : this.tableGroups().length === 0) return;
    const heightAt = pages
      ? (bp: number) => pagesContentHeight(this.s.pageCount, this.cssW, bp)
      : (bp: number) => this.tablesHeightAt(bp);
    const best = bestFitBlockPx(this.cssH, heightAt);
    const fits = heightAt(best) <= this.cssH;
    if (fits && pages) {
      this.setZoom(best, 1, 0);      // fits: page 1 at top-left
    } else if (fits) {
      this.scroll.tables = 0;        // fits: show tables from the top
      this.setZoom(best);
    } else {
      this.setZoom(best);            // cannot fit: keep the current scroll position
    }
  }

  // Public: called from the Navigation panel legend.
  async navigateToObject(id: number) {
    const obj = this.s.objById.get(id);
    if (!obj) return;
    if (this.s.view === "tables") {
      const band = this.bands.find((b) => b.grp.objectId === id);
      if (band) { this.scroll.tables = Math.max(0, band.y); this.scheduleRender(); }
      return;
    }
    let first: number | null = obj.startLeafPage || obj.startPage;
    if (!first) {
      const d = await fetchObjectPages(id, 0, 1);
      const pages = d?.pages || [];
      first = pages.length > 1 ? pages[1].pageNumber : pages.length ? pages[0].pageNumber : null;
    }
    if (first) this.scrollToBlock(first);
  }

  private onWheel = (e: WheelEvent) => {
    e.preventDefault();
    if (e.ctrlKey || e.metaKey) {
      const rect = this.canvas.getBoundingClientRect();
      const my = e.clientY - rect.top;
      const anchor = Math.floor((my + this.scroll.pages) / this.cell()) * this.cols() + 1;
      this.setZoom(this.blockPx() + (e.deltaY < 0 ? 2 : -2), anchor, my);
    } else {
      this.scroll[this.s.view] += e.deltaY;
      this.scheduleRender();
    }
  };

  private onClick = (e: MouseEvent) => {
    const rect = this.canvas.getBoundingClientRect();
    const hit = this.pageAt(e.clientX - rect.left, e.clientY - rect.top);
    if (!hit) return;
    if (this.s.view === "pages" && this.blockPx() < LOD_THRESHOLD) {
      const run = this.runAt(hit.pageNumber);
      if (run) this.setZoom(12, run.startPage);
    } else {
      this.selected = hit.pageNumber; this.scheduleRender();
    }
  };
}
