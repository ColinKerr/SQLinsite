import { describe, expect, it } from "vitest";
import { formatBytes } from "./format.ts";

describe("formatBytes", () => {
  it("uses KB/MB/GB and trims trailing zeros", () => {
    expect(formatBytes(1024)).toBe("1 KB");
    expect(formatBytes(1536)).toBe("1.5 KB");
    expect(formatBytes(4096)).toBe("4 KB");
    expect(formatBytes(1024 * 1024)).toBe("1 MB");
    expect(formatBytes(1024 * 1024 * 1024)).toBe("1 GB");
  });

  it("keeps at most 4 total digits, choosing decimals by magnitude", () => {
    expect(formatBytes(1023 * 1024)).toBe("1023 KB"); // 4 int digits, 0 decimals
    expect(formatBytes(10.7 * 1024 * 1024)).toBe("10.7 MB"); // 2 int → 2 decimals, trimmed
    expect(formatBytes(1024 * 1024 + 512 * 1024)).toBe("1.5 MB");
  });

  it("never exceeds four significant digits", () => {
    for (const n of [1, 500, 1234, 99999, 1234567, 9_999_999_999]) {
      const digits = formatBytes(n).split(" ")[0].replace(".", "").replace("-", "");
      expect(digits.length).toBeLessThanOrEqual(4);
    }
  });
});
