// Lightweight, always-on performance marks the perf harness reads to know when
// the app finished its initial data load and first paint. Marks are cheap and
// harmless in production; the harness (perf/, see plan/PERF_HARNESS.md) waits on
// `window.__sqlinsitePerf.ready` and reads `marks` for load timing.

interface PerfState {
  marks: Record<string, number>; // name → ms since navigation start
  ready: boolean;
}

declare global {
  interface Window {
    __sqlinsitePerf?: PerfState;
  }
}

function state(): PerfState {
  if (typeof window === "undefined") return { marks: {}, ready: false };
  if (!window.__sqlinsitePerf) window.__sqlinsitePerf = { marks: {}, ready: false };
  return window.__sqlinsitePerf;
}

// Record a named mark at the current time (ms since navigation start).
export function perfMark(name: string): void {
  state().marks[name] = performance.now();
  try {
    performance.mark(`sqlinsite:${name}`);
  } catch {
    // performance.mark may be unavailable in some environments; marks map is enough.
  }
}

// Signal that the app has finished its initial load and painted the first view.
export function perfReady(): void {
  perfMark("app-ready");
  state().ready = true;
}
