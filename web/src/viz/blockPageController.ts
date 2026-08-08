import { fetchPage } from "../core/api.ts";
import { BG, MAX_BLOCK_PX, MIN_BLOCK_PX } from "../core/constants.ts";
import { bestFitBlockPx, cell as cellFor, colsFor } from "../core/layout.ts";
import { colorForPage, GLYPH } from "../core/palette.ts";
import { overlayFill } from "../core/overlay.ts";
import type { Profile } from "../core/profile.ts";
import type { BlockColorMode, Metric, ObjectInfo, PageRow } from "../core/types.ts";

// One block's page-canvas config, mirrored from React on every relevant change.
export interface BlockPageConfig {
  pages: PageRow[];
  startPage: number;          // block's first page (identity: a new block resets scroll/fit)
  mode: BlockColorMode;
  sharedWithParent: boolean;
  profile: Profile;
  metric: Metric;
  hasProfile: boolean;
  objById: Map<number, ObjectInfo>;
}

const FREE_TYPES = new Set(["freelist-trunk", "freelist-leaf", "unallocated"]);
const USED_COLOR = "rgb(80,200,90)";
const FREE_COLOR = "rgb(230,50,90)";
const SHARED_COLOR = "#3b7dd8";
const CHANGED_COLOR = "#e0a030";

// A compact, self-contained page canvas for a single block's page range: the same
// cell grid, minimap, zoom and hover popup as the Pages view, but bounded to the
// block's pages and colored by the active Block color mode. Deliberately simpler
// than CanvasController (no LOD/runs/settle) since a block holds ≤ pagesPerBlock
// pages, and kept separate so the detail pane never disturbs the main canvas.
export class BlockPageController {
  private ctx: CanvasRenderingContext2D;
  private mctx: CanvasRenderingContext2D;
  private cssW = 0; private cssH = 0; private dpr = 1;
  private miniW = 0; private miniH = 0;
  private scrollY = 0;
  private blockPx = 12;
  private cfg: BlockPageConfig | null = null;
  private pending = false;
  private popupTimer: number | null = null;
  private cleanups: Array<() => void> = [];

  constructor(
    private canvas: HTMLCanvasElement,
    private minimap: HTMLCanvasElement,
    private popup: HTMLElement,
    private stage: HTMLElement,
    private onZoom?: (pct: number) => void,
  ) {
    this.ctx = canvas.getContext("2d")!;
    this.mctx = minimap.getContext("2d")!;
  }

  mount() {
    const ro = new ResizeObserver(() => this.resize());
    ro.observe(this.stage);
    this.cleanups.push(() => ro.disconnect());
    this.add(this.canvas, "wheel", this.onWheel as EventListener, { passive: false });
    this.add(this.canvas, "mousemove", this.onMove as EventListener);
    this.add(this.canvas, "mouseleave", () => this.hidePopup());
    this.initMinimap();
    this.resize();
  }

  unmount() { for (const c of this.cleanups) c(); this.cleanups = []; this.clearPopupTimer(); }

  private add(el: EventTarget, t: string, fn: EventListener, opts?: AddEventListenerOptions) {
    el.addEventListener(t, fn, opts);
    this.cleanups.push(() => el.removeEventListener(t, fn, opts));
  }

  // Called from React whenever the selected block or overlay changes. A new block
  // (different startPage) resets the scroll and best-fits; otherwise just repaints.
  setData(cfg: BlockPageConfig) {
    const blockChanged = !this.cfg || this.cfg.startPage !== cfg.startPage;
    this.cfg = cfg;
    if (blockChanged) { this.scrollY = 0; this.fit(); } else this.render();
  }

  private cell() { return cellFor(this.blockPx); }
  private cols() { return colsFor(this.cssW, this.blockPx); }
  private contentHeight() {
    return Math.ceil((this.cfg?.pages.length ?? 0) / this.cols()) * this.cell();
  }

  resize = () => {
    this.cssW = this.stage.clientWidth; this.cssH = this.stage.clientHeight;
    this.dpr = window.devicePixelRatio || 1;
    this.canvas.width = Math.floor(this.cssW * this.dpr);
    this.canvas.height = Math.floor(this.cssH * this.dpr);
    this.ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    this.miniW = this.minimap.clientWidth; this.miniH = this.minimap.clientHeight;
    this.minimap.width = Math.floor(this.miniW * this.dpr);
    this.minimap.height = Math.floor(this.miniH * this.dpr);
    this.mctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    this.render();
  };

  private schedule() {
    if (this.pending) return;
    this.pending = true;
    requestAnimationFrame(() => { this.pending = false; this.render(); });
  }

  private render() {
    this.scrollY = Math.max(0, Math.min(this.scrollY, Math.max(0, this.contentHeight() - this.cssH)));
    this.ctx.fillStyle = BG; this.ctx.fillRect(0, 0, this.cssW, this.cssH);
    const cfg = this.cfg; if (!cfg) return;
    const cols = this.cols(), c = this.cell(), bp = this.blockPx, scroll = this.scrollY;
    const overlay = cfg.mode === "profile" && cfg.hasProfile && cfg.metric !== "none";
    const gmax = overlay ? cfg.profile.globalMax(cfg.metric) : 1;
    cfg.pages.forEach((p, i) => {
      const x = (i % cols) * c, y = Math.floor(i / cols) * c - scroll;
      if (y > this.cssH || y + bp < 0) return;
      this.ctx.fillStyle = this.pageColor(cfg, p);
      this.ctx.fillRect(x, y, bp, bp);
      if (overlay) {
        this.ctx.fillStyle = overlayFill(cfg.profile.metricForPage(p.pageNumber, cfg.metric), gmax);
        this.ctx.fillRect(x, y, bp, bp);
      }
      this.drawGlyph(p.pageType, x, y);
    });
    this.renderMinimap();
  }

  // Page fill for the active Block color mode — the page-level analog of the block
  // grid's colors: object/type (also the base under the profile overlay), a binary
  // used/free tint, or the block's uniform shared-vs-changed color.
  private pageColor(cfg: BlockPageConfig, p: PageRow): string {
    switch (cfg.mode) {
      case "free": return FREE_TYPES.has(p.pageType) ? FREE_COLOR : USED_COLOR;
      case "shared": return cfg.sharedWithParent ? SHARED_COLOR : CHANGED_COLOR;
      default: return colorForPage(p.objectId, p.pageType); // object + profile base
    }
  }

  private drawGlyph(type: string, x: number, y: number) {
    if (this.blockPx < 14) return;
    this.ctx.fillStyle = "rgba(16,18,26,.85)";
    this.ctx.font = `${Math.min(11, this.blockPx - 3)}px sans-serif`;
    this.ctx.textAlign = "center"; this.ctx.textBaseline = "middle";
    this.ctx.fillText(GLYPH[type] || "?", x + this.blockPx / 2, y + this.blockPx / 2);
  }

  private renderMinimap() {
    const w = this.miniW, h = this.miniH; if (w === 0 || h === 0) return;
    this.mctx.clearRect(0, 0, w, h);
    const cfg = this.cfg; if (!cfg) return;
    const contentH = this.contentHeight(); if (contentH <= 0) return;
    const scale = h / contentH, cols = this.cols(), c = this.cell();
    cfg.pages.forEach((p, i) => {
      this.mctx.fillStyle = this.pageColor(cfg, p);
      this.mctx.fillRect(0, Math.floor(i / cols) * c * scale, w, Math.max(0.5, c * scale));
    });
    const vy = this.scrollY * scale, vh = Math.max(2, this.cssH * scale);
    this.mctx.fillStyle = "rgba(110,168,254,.22)"; this.mctx.fillRect(0, vy, w, vh);
    this.mctx.strokeStyle = "rgba(255,255,255,.8)"; this.mctx.lineWidth = 1;
    this.mctx.strokeRect(0.5, vy + 0.5, w - 1, vh - 1);
  }

  private initMinimap() {
    let dragging = false;
    const scrollTo = (clientY: number) => {
      const rect = this.minimap.getBoundingClientRect();
      const frac = Math.max(0, Math.min(1, (clientY - rect.top) / rect.height));
      this.scrollY = Math.max(0, frac * this.contentHeight() - this.cssH / 2);
      this.schedule();
    };
    this.add(this.minimap, "mousedown", ((e: MouseEvent) => { dragging = true; scrollTo(e.clientY); e.preventDefault(); }) as EventListener);
    this.add(window, "mousemove", ((e: MouseEvent) => { if (dragging) scrollTo(e.clientY); }) as EventListener);
    this.add(window, "mouseup", () => { dragging = false; });
  }

  private pageAt(mx: number, my: number): { p: PageRow; idx: number } | null {
    const cols = this.cols(), c = this.cell();
    const col = Math.floor(mx / c);
    const idx = Math.floor((my + this.scrollY) / c) * cols + col;
    const pages = this.cfg?.pages;
    if (!pages || col < 0 || col >= cols || idx < 0 || idx >= pages.length) return null;
    return { p: pages[idx], idx };
  }

  private onMove = (evt: MouseEvent) => {
    const rect = this.canvas.getBoundingClientRect();
    const hit = this.pageAt(evt.clientX - rect.left, evt.clientY - rect.top);
    if (!hit) { this.hidePopup(); return; }
    this.clearPopupTimer();
    this.popupTimer = window.setTimeout(async () => {
      const d = await fetchPage(hit.p.pageNumber);
      if (!d) return;
      const obj = this.cfg?.objById.get(d.objectId ?? -1);
      const rows: Array<[string, string | number | null]> = [
        ["type", d.pageType],
        ["object", obj ? `${obj.name} (${obj.type})` : "—"],
        ["free bytes", d.freeBytes],
        ["cells", d.cellCount],
      ];
      if (d.profile) rows.push(["reads / writes", `${d.profile.reads} / ${d.profile.writes}`]);
      let html = `<h4>Page ${d.pageNumber}</h4><table>`;
      for (const [k, v] of rows) if (v != null) html += `<tr><td>${k}</td><td class="v">${v}</td></tr>`;
      this.popup.innerHTML = html + "</table>";
      const stage = this.stage.getBoundingClientRect();
      this.popup.hidden = false;
      this.popup.style.left = (evt.clientX - stage.left + 14) + "px";
      this.popup.style.top = (evt.clientY - stage.top + 14) + "px";
    }, 60);
  };

  private clearPopupTimer() { if (this.popupTimer != null) { clearTimeout(this.popupTimer); this.popupTimer = null; } }
  private hidePopup() { this.clearPopupTimer(); this.popup.hidden = true; }

  private onWheel = (e: WheelEvent) => {
    e.preventDefault();
    if (e.ctrlKey || e.metaKey) {
      // Zoom, keeping the page under the cursor roughly fixed on screen.
      const rect = this.canvas.getBoundingClientRect();
      const my = e.clientY - rect.top;
      const hit = this.pageAt(e.clientX - rect.left, my);
      this.setZoom(this.blockPx + (e.deltaY < 0 ? 2 : -2));
      if (hit) this.scrollY = Math.max(0, Math.floor(hit.idx / this.cols()) * this.cell() - my);
      this.schedule();
    } else {
      this.scrollY += e.deltaY; this.schedule();
    }
  };

  setZoom(px: number) {
    this.blockPx = Math.max(MIN_BLOCK_PX, Math.min(MAX_BLOCK_PX, px));
    this.onZoom?.(Math.round((100 * this.blockPx) / MAX_BLOCK_PX));
    this.schedule();
  }
  zoomBy(d: number) { this.setZoom(this.blockPx + d); }

  fit() {
    if (this.cssW <= 0 || this.cssH <= 0 || !this.cfg || this.cfg.pages.length <= 0) return;
    const n = this.cfg.pages.length;
    this.scrollY = 0;
    this.setZoom(bestFitBlockPx(this.cssH, (bp) => Math.ceil(n / colsFor(this.cssW, bp)) * cellFor(bp)));
  }
}
