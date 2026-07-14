import { describe, expect, it } from "vitest";
import { formatVersionError } from "./version.ts";

describe("formatVersionError", () => {
  it("returns null when the map matches the expected version", () => {
    expect(formatVersionError(2, 2)).toBeNull();
  });

  it("reports an older map and asks to regenerate", () => {
    const msg = formatVersionError(1, 2);
    expect(msg).toMatch(/older/);
    expect(msg).toMatch(/v1/);
    expect(msg).toMatch(/v2/);
    expect(msg).toMatch(/sqlinsite map/);
  });

  it("reports a newer map", () => {
    const msg = formatVersionError(3, 2);
    expect(msg).toMatch(/newer/);
    expect(msg).toMatch(/v3/);
  });

  it("treats a missing version as older (incompatible)", () => {
    expect(formatVersionError(undefined, 2)).toMatch(/older/);
  });
});
