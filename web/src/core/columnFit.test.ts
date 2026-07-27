import { describe, it, expect } from "vitest";
import { fitColumnWidths, formatCellText, MIN_COL, MAX_COL } from "./columnFit.ts";

// Deterministic measurer: width proportional to text length.
const measure = (t: string) => t.length * 7;

describe("fitColumnWidths", () => {
  it("sizes each column to the widest of header and cell values", () => {
    const cols = [{ name: "id" }, { name: "name" }];
    const rows = [
      [1, "short"],
      [22, "a much longer value"],
    ];
    const [w0, w1] = fitColumnWidths(cols, rows, measure, { pad: 0, min: 0 });
    expect(w0).toBe("22".length * 7); // header "id"(14) < "22"(14) tie → 14
    expect(w1).toBe("a much longer value".length * 7); // widest cell wins over header
  });

  it("uses the header when it is wider than every value", () => {
    const cols = [{ name: "a_very_wide_header" }];
    const rows = [["x"], ["y"]];
    const [w] = fitColumnWidths(cols, rows, measure, { pad: 0, min: 0 });
    expect(w).toBe("a_very_wide_header".length * 7);
  });

  it("renders NULL for null/undefined values", () => {
    expect(formatCellText(null)).toBe("NULL");
    expect(formatCellText(undefined)).toBe("NULL");
    const cols = [{ name: "c" }];
    const [w] = fitColumnWidths(cols, [[null]], measure, { pad: 0, min: 0 });
    expect(w).toBe("NULL".length * 7);
  });

  it("clamps to [min, max] and adds padding", () => {
    const cols = [{ name: "x" }];
    const wide = "x".repeat(1000);
    const [wMax] = fitColumnWidths(cols, [[wide]], measure);
    expect(wMax).toBe(MAX_COL);
    const [wMin] = fitColumnWidths([{ name: "" }], [[""]], measure, { pad: 0 });
    expect(wMin).toBe(MIN_COL);
  });
});
