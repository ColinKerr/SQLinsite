import { describe, it, expect } from "vitest";
import { Profile } from "./profile.ts";

const sample = new Profile({
  pages: [
    { pageNumber: 2, reads: 3, writes: 0 },
    { pageNumber: 5, reads: 1, writes: 4 },
    { pageNumber: 9, reads: 0, writes: 2 },
  ],
});

describe("Profile", () => {
  it("builds per-page lookup and maxima", () => {
    expect(sample.metricForPage(2, "reads")).toBe(3);
    expect(sample.metricForPage(5, "total")).toBe(5);
    expect(sample.metricForPage(99, "total")).toBe(0);
    expect(sample.max).toEqual({ reads: 3, writes: 4, total: 5 });
  });

  it("globalMax follows the metric", () => {
    expect(sample.globalMax("reads")).toBe(3);
    expect(sample.globalMax("writes")).toBe(4);
    expect(sample.globalMax("total")).toBe(5);
  });

  it("rangeMetric sums via prefix sums", () => {
    expect(sample.rangeMetric(1, 9, "reads")).toBe(4);
    expect(sample.rangeMetric(1, 9, "writes")).toBe(6);
    expect(sample.rangeMetric(1, 9, "total")).toBe(10);
    expect(sample.rangeMetric(3, 6, "total")).toBe(5); // only page 5
  });

  it("rangeMax is the brightest single page in range", () => {
    expect(sample.rangeMax(1, 9, "total")).toBe(5); // page 5
    expect(sample.rangeMax(1, 3, "total")).toBe(3); // page 2
  });

  it("identifiedPages counts distinct pages, not accesses", () => {
    // pages: 2(r3), 5(r1,w4), 9(w2)
    expect(sample.identifiedPages("total")).toBe(3);  // all touched pages
    expect(sample.identifiedPages("reads")).toBe(2);  // pages 2 and 5 read
    expect(sample.identifiedPages("writes")).toBe(2); // pages 5 and 9 written
    expect(sample.identifiedPages("none")).toBe(0);
  });

  it("empty profile is safe (max clamped to 1)", () => {
    const e = Profile.empty();
    expect(e.max).toEqual({ reads: 1, writes: 1, total: 1 });
    expect(e.rangeMax(1, 100, "total")).toBe(0);
  });
});
