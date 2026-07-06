import { describe, it, expect } from "vitest";
import {
  cell, colsFor, pagesContentHeight, visiblePageRange, topLeftPage, scrollForPageAtY,
  lowerBound, upperBound,
} from "./layout.ts";

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
});
