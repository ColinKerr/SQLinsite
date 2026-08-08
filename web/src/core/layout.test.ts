import { describe, it, expect } from "vitest";
import {
  cell, colsFor, pagesContentHeight, visiblePageRange, topLeftPage, scrollForPageAtY,
  lowerBound, upperBound, bestFitBlockPx, tablesBandBoxes, mapBandY,
} from "./layout.ts";
import { HEADER_H, TABLE_BAND_GAP } from "./constants.ts";

describe("layout math", () => {
  it("cell = blockPx + gap", () => {
    expect(cell(12)).toBe(13);
  });

  it("colsFor floors width/cell, min 1", () => {
    expect(colsFor(130, 12)).toBe(10);
    expect(colsFor(5, 12)).toBe(1);
  });

  it("content height rounds up rows", () => {
    // 25 pages, 10 cols → 3 rows × 13 = 39
    expect(pagesContentHeight(25, 130, 12)).toBe(39);
  });

  it("visiblePageRange is 1-based inclusive and clamped", () => {
    // scroll 0, one row visible fully (+partial); 10 cols
    const [from, to] = visiblePageRange(0, 130, 13, 12, 1000);
    expect(from).toBe(1);
    expect(to).toBe(20); // rows 0..1 → up to page 20
  });

  it("topLeftPage and scrollForPageAtY round-trip", () => {
    const cssW = 130, blockPx = 12;
    const scroll = scrollForPageAtY(101, 0, cssW, blockPx); // put page 101 at top
    expect(topLeftPage(scroll, cssW, blockPx)).toBe(101);
  });

  it("scrollForPageAtY never negative", () => {
    expect(scrollForPageAtY(1, 500, 130, 12)).toBe(0);
  });

  it("binary search bounds", () => {
    const a = [1, 3, 5, 7, 9];
    expect(lowerBound(a, 5)).toBe(2);
    expect(upperBound(a, 5)).toBe(3);
    expect(lowerBound(a, 4)).toBe(2);
    expect(upperBound(a, 9)).toBe(5);
  });

  it("bestFitBlockPx picks the largest fitting zoom, else min", () => {
    const h = (bp: number) => bp; // monotonic in blockPx
    expect(bestFitBlockPx(10, h, 1, 40)).toBe(10);  // largest bp with bp ≤ 10
    expect(bestFitBlockPx(100, h, 1, 40)).toBe(40); // everything fits → zoom in to max
    expect(bestFitBlockPx(0.5, h, 1, 40)).toBe(1);  // nothing fits → min (zoomed out)
  });

  it("tablesBandBoxes lays bands out with header + gap and a min of one row", () => {
    // 2 bands, 10 & 5 pages, 10 cols, cell 13 → 1 row each; h = HEADER_H + 13.
    const { boxes, height } = tablesBandBoxes([10, 5], 10, 13);
    expect(boxes[0]).toEqual({ y: 0, h: HEADER_H + 13 });
    expect(boxes[1]).toEqual({ y: HEADER_H + 13 + TABLE_BAND_GAP, h: HEADER_H + 13 });
    expect(height).toBe(2 * (HEADER_H + 13 + TABLE_BAND_GAP));
    // A zero-page band still gets one row (never collapses to just a header).
    expect(tablesBandBoxes([0], 10, 13).boxes[0].h).toBe(HEADER_H + 13);
  });

  it("tablesBandBoxes proportions change with zoom (why the minimap needs a fixed layout)", () => {
    // One big band + one tiny band; the tiny band's share remains constant because headers are ignored for minimap
    const frac = (cellPx: number) => {
      const { boxes, height } = tablesBandBoxes([100, 1], 10, cellPx, true);
      return boxes[0].h / height;
    };
    expect(frac(2)).toBeCloseTo(frac(40), 2); // consistent with zoom
    expect(frac(7)).toBe(frac(7));            // deterministic at a fixed cell
  });

  it("mapBandY is piecewise-linear between two same-length layouts", () => {
    const from = [{ y: 0, h: 10 }, { y: 20, h: 10 }];
    const to = [{ y: 0, h: 100 }, { y: 110, h: 100 }];
    expect(mapBandY(0, from, to)).toBe(0);      // band 0 start
    expect(mapBandY(5, from, to)).toBe(50);     // band 0 midpoint
    expect(mapBandY(25, from, to)).toBe(160);   // band 1 midpoint
    expect(mapBandY(1000, from, to)).toBe(210); // past the end → clamped to last box end
    expect(mapBandY(5, [], to)).toBe(0);        // empty source → 0
  });

  it("bestFitBlockPx result reveals whether it actually fits", () => {
    const viewH = 200, cssW = 400;
    const smallFits = bestFitBlockPx(viewH, (b) => pagesContentHeight(50, cssW, b));
    expect(pagesContentHeight(50, cssW, smallFits) <= viewH).toBe(true); // fits → scroll to top
    const hugeFit = bestFitBlockPx(viewH, (b) => pagesContentHeight(1_000_000, cssW, b));
    expect(pagesContentHeight(1_000_000, cssW, hugeFit) > viewH).toBe(true); // can't fit → keep scroll
  });
});
