import fs from "node:fs";
import path from "node:path";
import { BINARY, BUILD_DIR, REPO_ROOT, RESULTS_DIR } from "./config.ts";
import { getFreePort, httpGet, runTimed, sleep, startServe, stopProcess, waitForReady } from "./proc.ts";
import { profileFrontend } from "./browser.ts";
import { ProcSampler } from "./sampler.ts";
import type { HttpSample, RunSummary } from "./types.ts";

interface Args {
  db: string | null;
  map: string | null;
  out: string | null;
  url: string;
  query: string | null;
  repeat: number;
  port: number;
  seconds: number;
  build: boolean;
  browser: boolean;
  headed: boolean;
  zoomOut: boolean;
  baseline: boolean;
  mapOnly: boolean;
  help: boolean;
}

function parseArgs(argv: string[]): Args {
  const a: Args = { db: null, map: null, out: null, url: "?view=pages", query: null, repeat: 1, port: 0, seconds: 2, build: false, browser: true, headed: false, zoomOut: false, baseline: false, mapOnly: false, help: false };
  for (let i = 0; i < argv.length; i++) {
    const [flag, inlineVal] = argv[i].split(/=(.*)/s);
    const val = () => inlineVal ?? argv[++i];
    switch (flag) {
      case "--db": a.db = val(); break;
      case "--map": a.map = val(); break;
      case "--out": a.out = val(); break;
      case "--url": a.url = val(); break;
      case "--query": a.query = val(); break;
      case "--repeat": a.repeat = Number(val()); break;
      case "--port": a.port = Number(val()); break;
      case "--seconds": a.seconds = Number(val()); break;
      case "--build": a.build = true; break;
      case "--no-browser": a.browser = false; break;
      case "--headed": a.headed = true; break;
      case "--zoom-out": a.zoomOut = true; break;
      case "--baseline": a.baseline = true; break;
      case "--map-only": a.mapOnly = true; break;
      case "-h": case "--help": a.help = true; break;
      default: throw new Error(`unknown argument: ${flag}`);
    }
  }
  return a;
}

// The view named in --url selects the interaction (scroll / query / none).
function viewOf(urlParams: string): string {
  return new URLSearchParams(urlParams.replace(/^\?/, "")).get("view") ?? "pages";
}

const HELP = `SQLinsite perf harness — Phase 1 (backend)

Usage:
  npm run perf -- --map <map.sqlite> [--db <file.db>] [options]
  npm run perf -- --db <file.db> [options]        # generates the map first

Options:
  --map <path>       Prebuilt map file (skips the map step).
  --db <path>        The SQLite db mapped by --map (enables live views).
  --url <params>     View/node to load, e.g. '?view=tree&page=5' (default '?view=pages').
  --query <sql>      SQL for the query-view interaction (default 'SELECT 1').
  --repeat <n>       Reload n times and report retained-heap growth (leak check).
  --build            Force 'cmake --build build' before running.
  --no-browser       Backend only; skip the headless-browser frontend profiling.
  --headed           Run the browser headed (for debugging).
  --zoom-out         Zoom fully out before scrolling (pages/tables) — stresses the
                     per-viewport LOD data fetches.
  --baseline         Save this run's metrics as the regression baseline.
  --map-only         Only build + measure the map step (time, RSS, file size); skip serve/browser.
  --port <n>         Server port (default 0 = pick a free port).
  --seconds <n>      Extra idle-sample seconds after the load (default 2).
  --out <dir>        Output dir (default perf/results/<timestamp>).
  -h, --help         This help.

At least one of --map / --db is required. With only --db, the map is generated
into <out>/map.sqlite and its build time + peak RSS are recorded.`;

// Endpoints probed in isolation for baseline latency/size — including the two
// known bottlenecks (structural-groups, tree/roots) so their un-queued cost is
// measured directly. `to` is filled in from meta.pageCount once known.
function pagesEndpoints(base: string, pageCount: number | null): string[] {
  const eps = [`${base}/api/pages?from=1&to=1024`, `${base}/api/minimap`];
  if (pageCount && pageCount > 0) eps.push(`${base}/api/runs?from=1&to=${pageCount}`);
  eps.push(`${base}/api/page/1`);
  eps.push(`${base}/api/tables/structural-groups`);
  eps.push(`${base}/api/tree/roots`);
  return eps;
}

function fmtMs(ms: number): string {
  return ms >= 1000 ? `${(ms / 1000).toFixed(2)}s` : `${Math.round(ms)}ms`;
}
function fmtMB(kb: number): string {
  return `${(kb / 1024).toFixed(1)} MB`;
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  if (args.help) { console.log(HELP); return; }
  if (!args.map && !args.db) throw new Error("need --map and/or --db (see --help)");

  const outDir = args.out ?? path.join(RESULTS_DIR, new Date().toISOString().replace(/[:.]/g, "-"));
  fs.mkdirSync(outDir, { recursive: true });

  const summary: RunSummary = {
    startedAt: new Date().toISOString(),
    scenario: "backend",
    db: args.db,
    map: args.map ?? path.join(outDir, "map.sqlite"),
    pageCount: null,
    serve: { url: "", startupMs: 0, samplingMs: 0, stats: { peakRssKB: 0, avgCpu: 0, maxCpu: 0, samples: 0 }, endpoints: [] },
  };

  // 1. Ensure the binary exists (build if missing or forced).
  if (args.build || !fs.existsSync(BINARY)) {
    console.log(`▶ building sqlinsite (${BUILD_DIR})…`);
    summary.build = await runTimed("cmake", ["--build", BUILD_DIR]);
    if (summary.build.exitCode !== 0) throw new Error("build failed");
    console.log(`  build: ${fmtMs(summary.build.wallMs)}`);
  }
  if (!fs.existsSync(BINARY)) throw new Error(`binary not found: ${BINARY} (run with --build)`);

  if (args.db && fs.existsSync(args.db)) summary.dbBytes = fs.statSync(args.db).size;

  // 2. Map step (only when no prebuilt map was given).
  if (!args.map) {
    if (!args.db) throw new Error("--db is required to generate a map");
    console.log(`▶ mapping ${args.db} → ${summary.map}…`);
    summary.mapStep = await runTimed(BINARY, ["map", "--test-file", args.db, "--out-file", summary.map]);
    if (summary.mapStep.exitCode !== 0) throw new Error("map failed");
    if (fs.existsSync(summary.map)) summary.mapBytes = fs.statSync(summary.map).size;
    const ratio = summary.mapBytes && summary.dbBytes ? ` · ${(100 * summary.mapBytes / summary.dbBytes).toFixed(1)}% of db` : "";
    console.log(`  map: ${fmtMs(summary.mapStep.wallMs)} · peak RSS ${summary.mapStep.peakRssKB != null ? fmtMB(summary.mapStep.peakRssKB) : "?"} · size ${summary.mapBytes != null ? fmtMB(summary.mapBytes / 1024) : "?"}${ratio}`);
  }

  // --map-only: write the map metrics and stop (fast iteration on build perf).
  if (args.mapOnly) {
    fs.writeFileSync(path.join(outDir, "summary.json"), JSON.stringify(summary, null, 2));
    fs.writeFileSync(path.join(outDir, "report.md"), reportMarkdown(summary));
    report(summary, outDir);
    const baselinePath = path.join(RESULTS_DIR, "baseline.json");
    if (args.baseline) {
      fs.writeFileSync(baselinePath, JSON.stringify(summary, null, 2));
      console.log(`\n✓ saved baseline → ${path.relative(REPO_ROOT, baselinePath)}`);
    } else if (fs.existsSync(baselinePath)) {
      compareToBaseline(summary, JSON.parse(fs.readFileSync(baselinePath, "utf8")) as RunSummary);
    }
    return;
  }

  // 3. Start serve, wait until ready, sampling RSS/CPU throughout.
  const port = args.port || (await getFreePort());
  console.log(`▶ starting visualize serve on port ${port}…`);
  const spawnT = performance.now();
  const { child, base } = startServe(BINARY, summary.map, args.db, port);
  let sampler: ProcSampler | undefined;
  try {
    sampler = new ProcSampler(child.pid!, 250);
    sampler.start();

    summary.serve.url = base;
    summary.serve.startupMs = await waitForReady(base, spawnT);
    console.log(`  ready at ${base} in ${fmtMs(summary.serve.startupMs)}`);

    // Read meta for pageCount, then probe the Pages-view endpoints.
    const meta = await httpGet(`${base}/api/meta`);
    try { summary.pageCount = JSON.parse(meta.body)?.meta?.pageCount ?? null; } catch { /* leave null */ }

    const probes: HttpSample[] = [];
    for (const ep of [`${base}/api/meta`, ...pagesEndpoints(base, summary.pageCount)]) {
      try {
        const s = await httpGet(ep);
        probes.push({ url: ep.replace(base, ""), status: s.status, ms: Math.round(s.ms), bytes: s.bytes });
      } catch (e) {
        probes.push({ url: ep.replace(base, ""), status: -1, ms: 0, bytes: 0 });
      }
    }
    summary.serve.endpoints = probes;

    // 4. Frontend: load the target view in headless Chromium (the serve RSS
    // sampler keeps running, so it captures the spike under real browser load).
    if (args.browser) {
      const target = `${base}/${args.url.startsWith("?") ? args.url : args.url ? `?${args.url}` : ""}`;
      console.log(`▶ profiling frontend: ${target}`);
      summary.frontend = await profileFrontend(target, {
        headed: args.headed,
        view: viewOf(args.url),
        query: args.query ?? undefined,
        repeat: args.repeat,
        zoomOut: args.zoomOut,
        outDir,
      });
      const f = summary.frontend;
      console.log(`  load→ready ${fmtMs(f.navToReadyMs ?? 0)} · JS heap ${f.jsHeapUsedMB ?? "?"} MB · long tasks ${f.longTasks.count} (${f.longTasks.totalMs}ms)`);
    }

    // 5. Hold the idle server briefly so the sampler captures steady-state RSS.
    const holdMs = Math.max(0, args.seconds * 1000);
    if (holdMs) await sleep(holdMs);
    summary.serve.samplingMs = performance.now() - spawnT;
    summary.serve.stats = sampler.stop();

    fs.writeFileSync(path.join(outDir, "rss.json"), JSON.stringify(sampler.raw(), null, 2));
  } finally {
    if (sampler) sampler.stop();
    await stopProcess(child);
  }

  // 6. Write artifacts + console report + markdown report.
  fs.writeFileSync(path.join(outDir, "summary.json"), JSON.stringify(summary, null, 2));
  fs.writeFileSync(path.join(outDir, "report.md"), reportMarkdown(summary));
  report(summary, outDir);

  // 7. Baseline / regression: save or compare key metrics.
  const baselinePath = path.join(RESULTS_DIR, "baseline.json");
  if (args.baseline) {
    fs.writeFileSync(baselinePath, JSON.stringify(summary, null, 2));
    console.log(`\n✓ saved baseline → ${path.relative(REPO_ROOT, baselinePath)}`);
  } else if (fs.existsSync(baselinePath)) {
    compareToBaseline(summary, JSON.parse(fs.readFileSync(baselinePath, "utf8")) as RunSummary);
  }
}

// Key comparable metrics for regression tracking.
function keyMetrics(s: RunSummary): Record<string, number> {
  const sg = s.serve.endpoints.find((e) => e.url.includes("structural-groups"));
  const m: Record<string, number> = {};
  if (s.mapStep) {
    m["map build ms"] = Math.round(s.mapStep.wallMs);
    m["map peak RSS MB"] = s.mapStep.peakRssKB != null ? Math.round(s.mapStep.peakRssKB / 1024) : 0;
    m["map size MB"] = s.mapBytes != null ? Math.round(s.mapBytes / (1024 * 1024)) : 0;
  }
  m["load→ready ms"] = s.frontend?.navToReadyMs ?? 0;
  m["structural-groups ms"] = sg?.ms ?? 0;
  m["serve peak RSS MB"] = Math.round(s.serve.stats.peakRssKB / 1024);
  m["JS heap MB"] = s.frontend?.jsHeapUsedMB ?? 0;
  m["long-task ms"] = s.frontend?.longTasks.totalMs ?? 0;
  return m;
}

// Print deltas vs. the saved baseline; flag metrics that regress > 20%.
function compareToBaseline(cur: RunSummary, base: RunSummary): void {
  const c = keyMetrics(cur), b = keyMetrics(base);
  console.log("\n── vs baseline ──────────────────────────────────────────");
  for (const k of Object.keys(c)) {
    const pct = b[k] ? Math.round(((c[k] - b[k]) / b[k]) * 100) : 0;
    const flag = pct > 20 ? "  ⚠ REGRESSION" : pct < -20 ? "  ✓ improved" : "";
    console.log(`  ${k.padEnd(22)} ${String(b[k]).padStart(8)} → ${String(c[k]).padStart(8)}  ${pct >= 0 ? "+" : ""}${pct}%${flag}`);
  }
  console.log("─────────────────────────────────────────────────────────");
}

function reportMarkdown(s: RunSummary): string {
  const M: string[] = [];
  M.push(`# Perf run — ${s.startedAt}`, "");
  M.push(`- **db:** \`${s.db ?? "(none)"}\``);
  M.push(`- **map:** \`${s.map}\``);
  M.push(`- **pages:** ${s.pageCount != null ? s.pageCount.toLocaleString() : "?"}`);
  if (s.dbBytes != null) M.push(`- **db size:** ${fmtMB(s.dbBytes / 1024)}`);
  if (s.mapStep) M.push(`- **map build:** ${fmtMs(s.mapStep.wallMs)} · peak RSS ${s.mapStep.peakRssKB != null ? fmtMB(s.mapStep.peakRssKB) : "?"} · size ${s.mapBytes != null ? fmtMB(s.mapBytes / 1024) : "?"}${s.mapBytes && s.dbBytes ? ` (${(100 * s.mapBytes / s.dbBytes).toFixed(1)}% of db)` : ""}`);
  M.push(`- **serve startup:** ${fmtMs(s.serve.startupMs)} · peak RSS ${fmtMB(s.serve.stats.peakRssKB)}`, "");
  M.push("## Endpoint latency (isolated)", "", "| status | time | size | endpoint |", "|---|---|---|---|");
  for (const e of s.serve.endpoints) M.push(`| ${e.status} | ${fmtMs(e.ms)} | ${fmtMB(e.bytes / 1024)} | \`${e.url}\` |`);
  if (s.frontend) {
    const f = s.frontend;
    M.push("", "## Frontend", "");
    M.push(`- **load→ready:** ${fmtMs(f.navToReadyMs ?? 0)} (meta ${fmtMs(f.marks["meta-loaded"] ?? 0)} · minimap ${fmtMs(f.marks["minimap-loaded"] ?? 0)})`);
    M.push(`- **JS heap:** ${f.jsHeapUsedMB ?? "?"} MB retained · ${f.heapPeakMB ?? "?"} MB peak`);
    M.push(`- **long tasks:** ${f.longTasks.count} · ${f.longTasks.totalMs}ms main-thread block`);
    if (f.interaction.kind === "scroll") M.push(`- **scroll${f.interaction.zoomedOut ? " (zoomed out)" : ""}:** ${f.interaction.fps} fps over ${f.interaction.steps} steps · ${f.interaction.dataFetches.count} LOD fetches (max ${f.interaction.dataFetches.maxMs}ms · avg ${f.interaction.dataFetches.avgMs}ms)`);
    if (f.interaction.kind === "query") M.push(`- **query:** ${fmtMs(f.interaction.ranMs)} · ${f.interaction.rowCount ?? "?"} rows`);
    if (f.leak) M.push(`- **leak (${f.leak.samplesMB.length}×):** ${f.leak.samplesMB.join(" → ")} MB · growth ${f.leak.growthMB} MB`);
    M.push(`- **artifacts:** ${f.artifacts.map((a) => `\`${a}\``).join(", ")}`);
  }
  return M.join("\n") + "\n";
}

function report(s: RunSummary, outDir: string): void {
  const L: string[] = [];
  L.push("");
  L.push("── SQLinsite perf (backend) ─────────────────────────────");
  L.push(`db          ${s.db ?? "(none)"}`);
  L.push(`map         ${s.map}`);
  L.push(`pages       ${s.pageCount != null ? s.pageCount.toLocaleString() : "?"}`);
  if (s.build) L.push(`build       ${fmtMs(s.build.wallMs)}`);
  if (s.dbBytes != null) L.push(`db size     ${fmtMB(s.dbBytes / 1024)}`);
  if (s.mapStep) L.push(`map build   ${fmtMs(s.mapStep.wallMs)} · peak RSS ${s.mapStep.peakRssKB != null ? fmtMB(s.mapStep.peakRssKB) : "?"} · size ${s.mapBytes != null ? fmtMB(s.mapBytes / 1024) : "?"}${s.mapBytes && s.dbBytes ? ` (${(100 * s.mapBytes / s.dbBytes).toFixed(1)}% of db)` : ""}`);
  L.push(`serve start ${fmtMs(s.serve.startupMs)}`);
  L.push(`serve RSS   peak ${fmtMB(s.serve.stats.peakRssKB)} · avg CPU ${s.serve.stats.avgCpu}% · max ${s.serve.stats.maxCpu}% · ${s.serve.stats.samples} samples`);
  L.push("endpoints:");
  for (const e of s.serve.endpoints) {
    L.push(`  ${String(e.status).padEnd(4)} ${fmtMs(e.ms).padStart(7)}  ${fmtMB(e.bytes / 1024).padStart(9)}  ${e.url}`);
  }
  if (s.frontend) {
    const f = s.frontend;
    L.push("");
    L.push(`frontend    ${f.url.replace(/^https?:\/\/[^/]+/, "")}`);
    L.push(`  load→ready  ${fmtMs(f.navToReadyMs ?? 0)}  (meta ${fmtMs(f.marks["meta-loaded"] ?? 0)} · minimap ${fmtMs(f.marks["minimap-loaded"] ?? 0)})`);
    L.push(`  JS heap     ${f.jsHeapUsedMB ?? "?"} MB retained · ${f.heapPeakMB ?? "?"} MB peak`);
    L.push(`  long tasks  ${f.longTasks.count} · ${f.longTasks.totalMs}ms total main-thread block`);
    if (f.interaction.kind === "scroll") {
      L.push(`  scroll${f.interaction.zoomedOut ? " (out)" : "     "} ${f.interaction.steps} steps in ${fmtMs(f.interaction.durationMs)} · ${f.interaction.fps} fps`);
      L.push(`  LOD fetches ${f.interaction.dataFetches.count} · max ${f.interaction.dataFetches.maxMs}ms · avg ${f.interaction.dataFetches.avgMs}ms`);
    }
    if (f.interaction.kind === "query") L.push(`  query       ran in ${fmtMs(f.interaction.ranMs)} · ${f.interaction.rowCount != null ? f.interaction.rowCount.toLocaleString() + " rows" : "no results"}`);
    if (f.leak) L.push(`  leak (${f.leak.samplesMB.length}×)  ${f.leak.samplesMB.join(" → ")} MB · growth ${f.leak.growthMB} MB`);
    L.push(`  network     ${f.network.total} reqs · ${fmtMB(f.network.totalBytes / 1024)} transferred`);
    for (const e of f.network.apiRequests) {
      L.push(`    ${String(e.status).padEnd(4)} ${fmtMs(e.ms).padStart(7)}  ${fmtMB(e.bytes / 1024).padStart(9)}  ${e.url}`);
    }
    L.push(`  fe artifacts ${f.artifacts.join(", ")}`);
  }
  L.push(`artifacts   ${path.relative(REPO_ROOT, outDir)}/ (summary.json, rss.json)`);
  L.push("─────────────────────────────────────────────────────────");
  console.log(L.join("\n"));
}

main().catch((e) => {
  console.error(`\n✗ ${e.message}`);
  process.exit(1);
});
