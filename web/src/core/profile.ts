import { lowerBound, upperBound } from "./layout.ts";
import type { Metric, ProfilePage, ProfilePagesResponse } from "./types.ts";

// Per-page read/write lookup for the current (selection-filtered) profile, plus
// prefix sums for O(log n) range aggregates. Built once per selection change.
export class Profile {
  readonly map = new Map<number, ProfilePage>();
  readonly sorted: number[] = []; // page numbers, ascending
  readonly prefReads: number[] = [0];
  readonly prefWrites: number[] = [0];
  readonly max = { reads: 1, writes: 1, total: 1 };

  static empty(): Profile {
    return new Profile({ pages: [] });
  }

  constructor(data: ProfilePagesResponse) {
    let mr = 0, mw = 0, mt = 0;
    for (const p of data.pages || []) {
      this.map.set(p.pageNumber, p);
      this.sorted.push(p.pageNumber);
      this.prefReads.push(this.prefReads[this.prefReads.length - 1] + p.reads);
      this.prefWrites.push(this.prefWrites[this.prefWrites.length - 1] + p.writes);
      mr = Math.max(mr, p.reads);
      mw = Math.max(mw, p.writes);
      mt = Math.max(mt, p.reads + p.writes);
    }
    this.max = { reads: mr || 1, writes: mw || 1, total: mt || 1 };
  }

  globalMax(metric: Metric): number {
    return metric === "reads" ? this.max.reads
      : metric === "writes" ? this.max.writes
      : this.max.total;
  }

  metricForPage(n: number, metric: Metric): number {
    const a = this.map.get(n);
    if (!a) return 0;
    return metric === "reads" ? a.reads : metric === "writes" ? a.writes : a.reads + a.writes;
  }

  // Max single-page metric across accessed pages in [from,to] — shades a run on
  // the same scale as the per-block view.
  rangeMax(from: number, to: number, metric: Metric): number {
    const lo = lowerBound(this.sorted, from), hi = upperBound(this.sorted, to);
    let m = 0;
    for (let i = lo; i < hi; i++) m = Math.max(m, this.metricForPage(this.sorted[i], metric));
    return m;
  }

  // Sum of the metric over [from,to] (via prefix sums).
  rangeMetric(from: number, to: number, metric: Metric): number {
    const lo = lowerBound(this.sorted, from), hi = upperBound(this.sorted, to);
    const r = this.prefReads[hi] - this.prefReads[lo];
    const w = this.prefWrites[hi] - this.prefWrites[lo];
    return metric === "reads" ? r : metric === "writes" ? w : r + w;
  }

  // Number of distinct pages accessed by the current selection under the metric
  // (pages read, pages written, or pages touched at all). A unique-page count —
  // NOT a sum of accesses, so repeated/per-statement hits to one page count once.
  identifiedPages(metric: Metric): number {
    if (metric === "none") return 0;
    if (metric === "total") return this.sorted.length; // every profiled page has ≥1 access
    let n = 0;
    for (const p of this.map.values()) {
      if ((metric === "reads" ? p.reads : p.writes) > 0) n++;
    }
    return n;
  }
}
