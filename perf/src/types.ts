// Shared result shapes for the perf harness. Phase 1 covers the backend
// (map build + serve process + endpoint latency); the browser/frontend fields
// are added in later phases.

// One HTTP probe against the running server.
export interface HttpSample {
  url: string;
  status: number;
  ms: number;
  bytes: number;
}

// One point sampled from `ps` for a running process.
export interface RssSample {
  t: number; // ms since epoch
  rssKB: number;
  cpu: number; // percent
}

// Aggregate of a process's RSS/CPU over a sampling window.
export interface ProcStats {
  peakRssKB: number;
  avgCpu: number;
  maxCpu: number;
  samples: number;
}

// A command run to completion, timed and (via /usr/bin/time -l) RSS-measured.
export interface TimedRun {
  wallMs: number;
  peakRssKB: number | null; // null if it couldn't be parsed
  exitCode: number;
}

// One network request captured during the frontend load.
export interface NetReq {
  url: string;
  method: string;
  status: number;
  ms: number;
  bytes: number;
}

// Data-fetch stats during a scroll (LOD requests: runs / object-pages), which
// surface server-side serialization (#6) as high max/avg latency under load.
export interface ScrollFetchStats {
  count: number;
  maxMs: number;
  avgMs: number;
}

// The scripted interaction run after load (Phase 3), keyed off the view.
export type Interaction =
  | { kind: "scroll"; steps: number; durationMs: number; fps: number; zoomedOut: boolean; dataFetches: ScrollFetchStats }
  | { kind: "query"; ranMs: number; rowCount: number | null }
  | { kind: "none" };

// Repeated-navigation leak probe: retained heap (MB) after each reload+GC.
export interface LeakResult {
  samplesMB: number[];
  growthMB: number;
}

// One JS-heap timeline point (MB) sampled during the run.
export interface HeapPoint {
  t: number; // ms since sampling start
  heapMB: number;
}

// High-level + Phase-3 frontend metrics from the headless-browser run.
export interface FrontendResult {
  url: string;
  navToReadyMs: number | null; // navigation start → app-ready mark
  marks: Record<string, number>; // meta-loaded / minimap-loaded / app-ready
  jsHeapUsedMB: number | null; // retained JS heap after a forced GC at ready
  heapPeakMB: number | null; // peak over the sampled timeline
  longTasks: { count: number; totalMs: number }; // main-thread blocks >50ms
  interaction: Interaction;
  leak?: LeakResult; // only when --repeat > 1
  nav: { domContentLoadedMs: number | null; loadMs: number | null };
  network: { total: number; totalBytes: number; apiRequests: NetReq[] };
  artifacts: string[]; // files written to the run dir (trace.json, heap-timeline.json)
}

// The full record written to summary.json for one harness run.
export interface RunSummary {
  startedAt: string;
  scenario: string; // phase 1: "backend"
  db: string | null;
  map: string;
  pageCount: number | null;
  build?: TimedRun; // present only if we (re)built the binary
  mapStep?: TimedRun; // present only if we generated the map
  mapBytes?: number; // size of the generated map file (bytes); present when mapStep is
  dbBytes?: number; // size of the source db file (bytes); present when --db given
  serve: {
    url: string;
    startupMs: number; // spawn → first /api/meta 200
    samplingMs: number;
    stats: ProcStats; // serve process RSS/CPU over the window
    endpoints: HttpSample[];
  };
  frontend?: FrontendResult; // present unless --no-browser
}
