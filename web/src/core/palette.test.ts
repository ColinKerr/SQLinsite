import { describe, it, expect } from "vitest";
import { PALETTE, colorForObject, colorForPage } from "./palette.ts";

describe("palette", () => {
  it("indexes the palette by object id, wrapping and handling negatives", () => {
    expect(colorForObject(0)).toBe(PALETTE[0]);
    expect(colorForObject(PALETTE.length)).toBe(PALETTE[0]);
    expect(colorForObject(-1)).toBe(PALETTE[PALETTE.length - 1]);
  });

  it("uses structural colors for null-object pages", () => {
    expect(colorForPage(null, "unallocated")).toBe("#33373f");
    expect(colorForPage(undefined, "overflow")).toBe("#7a7f8a");
    expect(colorForPage(null, "table-leaf")).toBe("#33373f"); // unknown structural → fallback
  });

  it("uses the object palette for object pages", () => {
    expect(colorForPage(2, "table-leaf")).toBe(PALETTE[2]);
  });

  it("never returns pure black", () => {
    for (let id = -20; id < 20; id++) expect(colorForObject(id)).not.toBe("#000000");
    for (const t of ["unallocated", "overflow", "freelist-leaf", "whatever"]) {
      expect(colorForPage(null, t)).not.toBe("#000000");
    }
  });
});
