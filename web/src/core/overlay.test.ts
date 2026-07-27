import { describe, it, expect } from "vitest";
import { overlayFill, UNTOUCHED_DARKEN } from "./overlay.ts";

describe("overlayFill", () => {
  it("darkens untouched cells with a capped black veil (never fully black)", () => {
    expect(overlayFill(0, 100)).toBe(`rgba(0,0,0,${UNTOUCHED_DARKEN})`);
    expect(UNTOUCHED_DARKEN).toBeLessThan(1);
  });

  it("tints touched cells white, brightening with intensity", () => {
    const low = overlayFill(1, 100);
    const high = overlayFill(100, 100);
    expect(low).toBe("rgba(255,255,255,0.126)");
    expect(high).toBe("rgba(255,255,255,0.720)");
    const alpha = (s: string) => parseFloat(s.split(",")[3]);
    expect(alpha(high)).toBeGreaterThan(alpha(low));
  });
});
