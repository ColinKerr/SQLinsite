import { GAP, HEADER_H, MAX_BLOCK_PX, MIN_BLOCK_PX, TABLE_BAND_GAP } from "./constants.ts";

// Grid geometry, all pure functions of (blockPx, cssW/H, scroll, pageCount).

export const cell = (blockPx: number) => blockPx + GAP;

// Best-fit zoom: the largest blockPx in [min, max] at which the content fits in
// viewH (`contentHeightAt(bp) <= viewH`), or `min` when nothing fits. Content
// height is monotonic in blockPx, so a downward scan finds the largest fit.
export function bestFitBlockPx(
  viewH: number,
  contentHeightAt: (bp: number) => number,
  min: number = MIN_BLOCK_PX,
  max: number = MAX_BLOCK_PX,
): number {
  for (let bp = max; bp >= min; bp--) {
    if (contentHeightAt(bp) <= viewH) return bp;
  }
  return min;
}

export const colsFor = (cssW: number, blockPx: number) =>
  Math.max(1, Math.floor(cssW / cell(blockPx)));

export function pagesContentHeight(pageCount: number, cssW: number, blockPx: number): number {
  return Math.ceil(pageCount / colsFor(cssW, blockPx)) * cell(blockPx);
}

// ---- tables-view band layout ------------------------------------------------

export interface BandBox { y: number; h: number }

// Vertical [y, h] box per tables-view band (one per group's pageCount) at a given
// column count and cell size, plus the total content height. Pure — used both for
// the live layout (rebuildBands) and for the FIXED, zoom-independent reference
// layout the minimap renders from, so the minimap doesn't reflow as you zoom.
export function tablesBandBoxes(pageCounts: number[], cols: number, cellPx: number, onlyCells: boolean = false): { boxes: BandBox[]; height: number } {
  const boxes: BandBox[] = [];
  let y = 0;
  for (const pc of pageCounts) {
    const rows = Math.max(1, Math.ceil(pc / cols));
    const h = (onlyCells ? 0 : HEADER_H) + rows * cellPx;
    boxes.push({ y, h });
    y += h + (onlyCells ? 0 : TABLE_BAND_GAP);
  }
  return { boxes, height: y };
}

// Maps a y from one band layout to the corresponding y in another layout with the
// same bands in the same order (piecewise-linear within each band). Lets the minimap
// place the current viewport in the fixed reference layout, and translate a minimap
// click back to a scroll offset. `from` and `to` must be the same length.
export function mapBandY(y: number, from: BandBox[], to: BandBox[]): number {
  if (from.length === 0 || to.length === 0) return 0;
  for (let i = 0; i < from.length; i++) {
    const box = from[i];
    if (y < box.y + box.h || i === from.length - 1) {
      const t = box.h > 0 ? clamp((y - box.y) / box.h, 0, 1) : 0;
      return to[i].y + t * to[i].h;
    }
  }
  const last = to[to.length - 1];
  return last.y + last.h;
}

// Inclusive 1-based [from, to] page range visible for the given scroll offset.
export function visiblePageRange(
  scroll: number, cssW: number, cssH: number, blockPx: number, pageCount: number,
): [number, number] {
  const c = colsFor(cssW, blockPx);
  const firstRow = Math.floor(scroll / cell(blockPx));
  const lastRow = Math.floor((scroll + cssH) / cell(blockPx));
  return [
    Math.max(1, firstRow * c + 1),
    Math.max(1, Math.min(pageCount, (lastRow + 1) * c)),
  ];
}

// The 1-based page currently at the grid's top-left corner.
export function topLeftPage(scroll: number, cssW: number, blockPx: number): number {
  return Math.floor(scroll / cell(blockPx)) * colsFor(cssW, blockPx) + 1;
}

// Scroll offset that places `page`'s row at viewport y `anchorY`.
export function scrollForPageAtY(
  page: number, anchorY: number, cssW: number, blockPx: number,
): number {
  return Math.max(0, Math.floor((page - 1) / colsFor(cssW, blockPx)) * cell(blockPx) - anchorY);
}

export function clamp(v: number, lo: number, hi: number): number {
  return Math.max(lo, Math.min(hi, v));
}

export function lowerBound(arr: number[], x: number): number {
  let lo = 0, hi = arr.length;
  while (lo < hi) { const m = (lo + hi) >> 1; if (arr[m] < x) lo = m + 1; else hi = m; }
  return lo;
}
export function upperBound(arr: number[], x: number): number {
  let lo = 0, hi = arr.length;
  while (lo < hi) { const m = (lo + hi) >> 1; if (arr[m] <= x) lo = m + 1; else hi = m; }
  return lo;
}
