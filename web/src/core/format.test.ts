import { describe, expect, it } from "vitest";
import { formatBytes, formatCount, formatSectorBytes } from "./format.ts";

describe("formatBytes", () => {
  it("uses the largest KB/MB/GB unit that keeps a whole-number digit", () => {
    expect(formatBytes(1024)).toBe("1 KB");
    expect(formatBytes(1536)).toBe("1.5 KB");
    expect(formatBytes(1023 * 1024)).toBe("1023 KB");
    expect(formatBytes(1024 * 1024 + 512 * 1024)).toBe("1.5 MB");
    expect(formatBytes(1024 * 1024)).toBe("1 MB");
    expect(formatBytes(1024 * 1024 * 1024)).toBe("1 GB");
  });

  it("never shows more than two decimal places", () => {
    expect(formatBytes(1.234 * 1024 * 1024)).toBe("1.23 MB");
    expect(formatBytes(10.7 * 1024 * 1024)).toBe("10.7 MB");
    for (const n of [1, 500, 1234, 99999, 1234567, 9_999_999_999]) {
      const frac = formatBytes(n).split(" ")[0].split(".")[1] ?? "";
      expect(frac.length).toBeLessThanOrEqual(2);
    }
  });
});

describe("formatSectorBytes", () => {
  it("shows unconverted bytes with a bare B suffix", () => {
    expect(formatSectorBytes(0)).toBe("0B");
    expect(formatSectorBytes(42)).toBe("42B");
    expect(formatSectorBytes(65536)).toBe("65536B");
  });
});

describe("formatCount", () => {
  it("groups thousands and shows no decimal point", () => {
    expect(formatCount(0)).toBe("0");
    expect(formatCount(999)).toBe("999");
    expect(formatCount(4599739)).toBe("4,599,739");
    expect(formatCount(1234.6)).toBe("1,235");
  });
});
