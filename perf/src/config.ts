import path from "node:path";
import { fileURLToPath } from "node:url";

// perf/src/config.ts → repo root is two levels up from this file's dir.
const here = path.dirname(fileURLToPath(import.meta.url));
export const REPO_ROOT = path.resolve(here, "..", "..");

export const BUILD_DIR = path.join(REPO_ROOT, "build");
export const BINARY = path.join(BUILD_DIR, "sqlinsite");
export const RESULTS_DIR = path.join(REPO_ROOT, "perf", "results");

export const HOST = "127.0.0.1";
