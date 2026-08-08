# Performance Profiling Plan & Reusable Harness

Plan for profiling load performance of **very large** SQLite files and building a
reusable harness to profile any file in the future. Covers **what** to profile
(backend + frontend, speed + memory) and **the harness** to build.

## Goals

1. Find where time and memory go when loading very large SQLite files, across the
   whole pipeline (map → serve → browser render).
2. Build a reusable, scriptable harness that starts the app, loads to a page
   given by a URL (default: Pages view), and captures backend + frontend
   performance and memory — usable against any file, not just today's.

## Pipeline being measured

```
sqlinsite map big.db → map.sqlite      (C++, one-time build cost)
   → (optional) sqlinsite profile
   → visualize serve --map-file … --db-file big.db --port 0   (C++ server)
   → headless Chromium → <serverURL>?view=…                   (browser)
```

Three cost centers: the **C++ map build**, the **C++ serve process** (per-endpoint
work + RSS), and the **browser** (JS / render / heap).


## Part A — What to profile

### Backend (C++)

- **`sqlinsite map`**: wall time, peak RSS, output map size, pages/sec. Large fixed
  cost for huge files; profile explicitly, but allow skipping via a prebuilt
  `--map`.
- **`visualize serve`**: startup-to-first-response time; process **RSS sampled over
  the run** and CPU%.
- **Per-endpoint latency + payload size** for whatever the target view calls:
  `/api/meta` (objects list can be large), `/api/pages`, `/api/runs` (LOD + the
  413→runs fallback), `/api/object/pages`, `/api/page/:n`, `/api/schema`,
  `/api/query/*`.
- *Optional server enhancement:* `SQLINSITE_ACCESS_LOG=1` env → log
  `method path status bytes durationMs` to stderr for authoritative server-side
  timing (harness parses it). Optional so v1 doesn't depend on it.

### Frontend (Playwright + CDP trace + high-level metrics)

- **Load timing**: time to `/api/meta` resolved → first canvas paint → app-ready
  mark (see hook below).
- **Full CDP performance trace** per run (`trace.json`, openable in Perfetto):
  long tasks, scripting/layout/paint/GPU, Monaco load cost.
- **JS heap**: `Performance.getMetrics` / `Memory` timeseries for peak
  `JSHeapUsedSize`; `HeapProfiler` sampling for allocation hotspots.
- **Network**: per-request start/end/size/status via CDP (payload sizes for the
  huge grid; confirms runs-LOD actually engages).
- **Interaction responsiveness**: scripted scroll of the Pages grid — frame stats
  + long tasks during scroll; DOM node count for virtualized tables (leak check
  on repeated nav).

### Memory (both)

- Backend RSS peak (map + serve).
- Frontend JS-heap peak **and growth across a repeated-navigation loop** (leak
  detection).

### Scenarios (each run = one URL, existing param scheme)

- **Pages** (`?view=pages`, default) — billion-page grid: cold load + scripted scroll.
- **Tables** (`?view=tables&obj=<largest>`).
- **Tree** (`?view=tree&page=N`) — deep drilldown, single `/api/page/:n`.
- **Query** (`?view=query`) — run a heavy query: run + row windowing + profile overlay.

### Workloads

- **Provided files**: `--db <path>` (+ optional `--map <path>`). See reference
  workload above.
- **Synthetic generator** (deterministic, seeded; built by piping generated SQL —
  `WITH RECURSIVE`, `randomblob`, `zeroblob` — to the system `sqlite3`, no new DB
  dep): scale tiers (10 MB / 100 MB / 1 GB / 10 GB opt-in) × shape variants
  (many-small-rows, wide/blob overflow-heavy, many-tables, index/deep-btree-heavy).

---

## Part B — The harness

**Stack & location:** new top-level `perf/` with its own `package.json` (Playwright
as a devDep there, so the shipped web bundle stays clean). TypeScript.

**Invocation:**

```
npm --prefix perf run perf -- \
  --db <path.db> [--map <path.map>] \
  --url '?view=tree&page=5'          # default: ?view=pages
  [--scale large] [--scenario pages,tables,query] [--trace] [--out perf/results/…]
```

**Per-run orchestration:**

1. Ensure the C++ binary is built (`cmake --build`).
2. Raw db + no map → run `sqlinsite map`, capturing wall time + peak RSS
   (`/usr/bin/time -l` on macOS / `getrusage`).
3. Start `visualize serve --port 0`; parse the printed URL/port; wait until it
   answers `/api/meta`. Sample serve RSS/CPU on an interval throughout.
4. Launch Playwright Chromium; attach CDP collectors (Tracing, Network,
   Performance/Memory, HeapProfiler).
5. Navigate to `<serverURL>?<urlParams>`; wait on the app-ready hook.
6. Run the scenario's interaction script (scroll / query / drill).
7. Stop tracing; snapshot metrics + heap.
8. Tear down browser and kill serve (robust cleanup on failure).
9. Write artifacts.

**App-side ready hook (small, prod-safe):** when the active view finishes its first
meaningful render (meta loaded + first canvas frame / results rendered), call
`performance.mark('sqlinsite:view-ready')` and set
`window.__sqlinsitePerf = { ready:true, marks }`. Cheap enough to leave always-on
(or gate behind `?perf=1`). Gives the harness a deterministic stop signal instead
of guessing on network-idle.

**Output** (`perf/results/<timestamp>/<scenario>/`):

- `summary.json` — all scalar metrics (map time/RSS, serve startup, endpoint
  latencies/sizes, load-to-ready, JS-heap peak, scroll frame stats).
- `trace.json`, `network.json`, `heap-timeline.json`, optional heap snapshot.
- Top-level `report.md` + console table comparing scenarios/scales;
  `baseline.json` for regression diffing (flag runs that regress > X%).

## Deliverables

- `perf/` harness (generic, path-driven) + synthetic generator.
- Small app-side ready-mark hook in the web app.
- *(optional)* `SQLINSITE_ACCESS_LOG` in the C++ server.
- A baseline results set + short findings write-up on the reference file above.

## Likely hotspots to watch (hypotheses to confirm)

- `sqlinsite map` on large files — dominant one-time cost.
- `/api/meta` objects-list size and initial Pages range/runs LOD.
- Canvas render + JS heap for the 8.6M-page grid; virtualization retention on
  repeated nav.
- Query row-window loading and profile-overlay recompute.
- Monaco editor load cost on first Query-view entry.
