# SQLinsite perf harness

Reusable performance harness for profiling how SQLinsite loads large SQLite
files. See [`plan/PERF_HARNESS.md`](../plan/PERF_HARNESS.md) for the full plan.

**Status: Phase 4 (complete).** Orchestrates the C++ pipeline; via headless
Chromium (Playwright) loads a view, runs a scripted interaction, and captures a
Chromium trace, heap timeline, long tasks, and a leak check; ships a **synthetic
generator** that reproduces the real-file bottlenecks, plus **baseline/regression**
tracking and per-run `report.md`.

## Setup

```
cd perf && npm install
npx playwright install chromium    # one-time browser download
```

## Usage

```
# Prebuilt map (skips the expensive map step); loads the Pages view by default:
npm run perf -- --map <map.sqlite> --db <file.db>

# Load a specific view/node (the view selects the interaction):
npm run perf -- --map <map.sqlite> --db <file.db> --url '?view=tree&page=5'

# Query view: type + run SQL and measure the round-trip:
npm run perf -- --map <map.sqlite> --db <file.db> --url '?view=query' --query 'SELECT ...'

# Leak check: reload N times, report retained-heap growth:
npm run perf -- --map <map.sqlite> --db <file.db> --repeat 5

# Raw db only (generates the map into the output dir first):
npm run perf -- --db <file.db>

npm run perf -- --baseline     # save this run as the regression baseline
npm run perf -- --no-browser   # backend only
npm run perf -- --help         # all options
```

Later runs auto-compare against `results/baseline.json` and flag metrics that
regress > 20%.

### Synthetic data (reproduce the bottlenecks without a real large file)

```
npm run generate -- --scale large           # ~0.7M pages, ~350 MB
npm run generate -- --pages 11000000 --out synth/huge.db   # ~8M pages, matches the real file
npm run perf -- --db <generated.db>          # maps + profiles it
```

The generator uses a small `page_size` + overflow blobs to hit a high **page
count** cheaply — the dimension the bottlenecks scale on. 

What the harness records (written to `perf/results/<timestamp>/`):

- **Backend** — map build wall time + peak RSS (when mapping), serve startup
  time, serve peak RSS / CPU, and latency + payload size for the Pages-view
  endpoints (`/api/meta`, `/api/pages`, `/api/runs`, `/api/page/1`).
- **Frontend** — navigation→ready time (with `meta-loaded` / `runs-loaded`
  marks), retained + peak JS heap, long tasks (main-thread blocking), a scripted
  interaction (canvas **scroll** with an FPS, or **query** run), an optional
  **leak** check, and per-request timing/size for `/api/*`.
- `summary.json` (all metrics) + `rss.json` (serve RSS/CPU time series) +
  **`trace.json`** (Chromium trace — open in [Perfetto](https://ui.perfetto.dev)
  or `chrome://tracing`) + `heap-timeline.json`.

The frontend timing relies on always-on marks in the app
(`web/src/core/perf.ts` → `window.__sqlinsitePerf`), so **after changing the web
app rebuild the binary** (`cmake --build build`, or pass `--build`) to re-embed
the assets. The harness uses the binary at `build/sqlinsite`.
