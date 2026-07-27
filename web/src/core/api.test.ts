import { describe, it, expect } from "vitest";
import { selParam } from "./api.ts";

describe("selParam", () => {
  it("empty when no profile", () => {
    expect(selParam(new Set([1]), 3, false)).toBe("");
  });
  it("empty when all leaves selected", () => {
    expect(selParam(new Set([0, 1, 2]), 3, true)).toBe("");
  });
  it("-1 when none selected", () => {
    expect(selParam(new Set(), 3, true)).toBe("&sel=-1");
  });
  it("lists ids for a partial selection", () => {
    expect(selParam(new Set([0, 2]), 3, true)).toBe("&sel=0,2");
  });
});
