import { GAP, MAX_BLOCK_PX, MIN_BLOCK_PX } from "./constants.ts";

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
