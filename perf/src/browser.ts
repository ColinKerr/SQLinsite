import fs from "node:fs";
import path from "node:path";
import { chromium, type CDPSession, type Page, type Request } from "playwright";
import type { FrontendResult, HeapPoint, Interaction, LeakResult, NetReq } from "./types.ts";

// The perf marks / helpers the app or our init script expose in-page, typed here
// for the page.evaluate callbacks below.
declare global {
  interface Window {
    __sqlinsitePerf?: { marks: Record<string, number>; ready: boolean };
    __perfLongTasks?: number[];
    __perfFrames?: number;
    __perfRaf?: number;
  }
}

export interface BrowserOpts {
  headed?: boolean;
  timeoutMs?: number;
  view?: string; // parsed from --url; selects the interaction
  query?: string; // SQL for the query interaction
  repeat?: number; // >1 runs the leak loop
  zoomOut?: boolean; // zoom fully out before the scroll interaction
  outDir: string; // where trace.json / heap-timeline.json are written
}

// Runs in the page before any of our evaluate() callbacks. Also polyfills the
// `__name` helper esbuild/tsx injects into named functions (our transpiled
// evaluate callbacks reference it, but it doesn't exist in the browser).
const LONGTASK_INIT = `
  globalThis.__name = globalThis.__name || function (f) { return f; };
  window.__perfLongTasks = [];
  try {
    new PerformanceObserver((l) => {
      for (const e of l.getEntries()) window.__perfLongTasks.push(e.duration);
    }).observe({ type: "longtask", buffered: true });
  } catch {}
`;

const readHeapMB = async (cdp: CDPSession): Promise<number | null> => {
  const m = await cdp.send("Performance.getMetrics");
  const v = m.metrics.find((x) => x.name === "JSHeapUsedSize")?.value;
  return v != null ? Math.round((v / (1024 * 1024)) * 10) / 10 : null;
};

const forceGC = (cdp: CDPSession) => cdp.send("HeapProfiler.collectGarbage").catch(() => {});

async function waitReady(page: Page, timeout: number): Promise<void> {
  await page.waitForFunction(() => window.__sqlinsitePerf?.ready === true, undefined, { timeout });
}

// Scroll the Pages/Tables canvas with real wheel events, counting rendered frames
// to derive an FPS (exercises LOD fetch + canvas redraw). When `zoomOut` is set,
// first zoom all the way out (→ runs LOD, whole-file spans) so scrolling triggers
// heavy per-viewport data fetches — the case that surfaces server serialization.
async function scrollInteraction(page: Page, zoomOut: boolean, steps = 40, deltaY = 500, stepDelayMs = 40): Promise<Interaction> {
  const box = await page.locator("#canvas").boundingBox();
  if (!box) return { kind: "none" };

  if (zoomOut) {
    // Click "Zoom out" enough times to clamp at the minimum block size.
    const btn = page.locator("#zoom-out");
    for (let i = 0; i < 24; i++) await btn.click();
    await page.waitForTimeout(400); // let the runs-LOD data settle
  }

  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  const tStart = await page.evaluate(() => {
    window.__perfFrames = 0;
    const loop = () => { window.__perfFrames = (window.__perfFrames ?? 0) + 1; window.__perfRaf = requestAnimationFrame(loop); };
    window.__perfRaf = requestAnimationFrame(loop);
    return performance.now();
  });
  const t0 = Date.now();
  for (let i = 0; i < steps; i++) {
    await page.mouse.wheel(0, deltaY);
    await page.waitForTimeout(stepDelayMs);
  }
  const durationMs = Date.now() - t0;
  const frames = await page.evaluate(() => window.__perfFrames ?? 0);

  // Let scrolling settle so the deferred detail load runs. Keep the rAF loop alive
  // during the wait so the page keeps painting — headless Chromium can otherwise
  // pause requestAnimationFrame when idle, which would stall the settle render.
  await page.waitForTimeout(600);
  await page.evaluate(() => { if (window.__perfRaf) cancelAnimationFrame(window.__perfRaf); });

  // LOD data fetches over the whole scroll+settle window (runs / object-pages /
  // structural-pages): count reflects whether per-frame fetching was avoided,
  // latency reveals server-side queuing.
  const dataFetches = await page.evaluate((since) => {
    const re = /\/api\/(pages|runs|object\/pages|object\/runs|tables\/structural-pages)\?/;
    const ds = performance.getEntriesByType("resource")
      .filter((e) => e.startTime >= since && re.test(e.name))
      .map((e) => e.duration);
    const sum = ds.reduce((a, b) => a + b, 0);
    return { count: ds.length, maxMs: ds.length ? Math.round(Math.max(...ds)) : 0, avgMs: ds.length ? Math.round(sum / ds.length) : 0 };
  }, tStart);

  return { kind: "scroll", steps, durationMs, fps: Math.round((frames / (durationMs / 1000)) * 10) / 10, zoomedOut: zoomOut, dataFetches };
}

// Type a query into Monaco and run it, timing the round-trip to the results footer.
async function queryInteraction(page: Page, sql: string, timeout: number): Promise<Interaction> {
  const mod = process.platform === "darwin" ? "Meta" : "Control";
  const editor = page.locator(".monaco-editor textarea").first();
  await editor.click();
  await page.keyboard.press(`${mod}+A`);
  await page.keyboard.type(sql);
  const t0 = Date.now();
  await page.keyboard.press(`${mod}+Enter`);
  let rowCount: number | null = null;
  try {
    await page.waitForSelector(".rt-foot", { timeout });
    const txt = await page.locator(".rt-foot").first().innerText();
    const m = txt.match(/of ([\d,]+) rows/);
    if (m) rowCount = Number(m[1].replace(/,/g, ""));
  } catch {
    // no results footer (e.g. SQL error) — leave rowCount null
  }
  return { kind: "query", ranMs: Date.now() - t0, rowCount };
}

async function runInteraction(page: Page, opts: BrowserOpts, timeout: number): Promise<Interaction> {
  if (opts.view === "query") return queryInteraction(page, opts.query ?? "SELECT 1", timeout);
  if (opts.view === "pages" || opts.view === "tables" || opts.view == null) return scrollInteraction(page, !!opts.zoomOut);
  return { kind: "none" }; // tree, etc.
}

// Reload the same URL `repeat` times, recording retained heap (post-GC) each time
// to surface leaks that grow across navigations.
async function leakLoop(page: Page, cdp: CDPSession, url: string, repeat: number, timeout: number): Promise<LeakResult> {
  const samplesMB: number[] = [];
  for (let i = 0; i < repeat; i++) {
    await page.goto(url, { waitUntil: "commit", timeout });
    await waitReady(page, timeout);
    await forceGC(cdp);
    samplesMB.push((await readHeapMB(cdp)) ?? 0);
  }
  return { samplesMB, growthMB: Math.round((samplesMB[samplesMB.length - 1] - samplesMB[0]) * 10) / 10 };
}

// Load `url` in headless Chromium and capture: navigation→ready, load marks,
// retained + peak JS heap, long tasks, a scripted interaction, a Chromium trace
// (trace.json) and heap timeline (heap-timeline.json), and per-request network.
export async function profileFrontend(url: string, opts: BrowserOpts): Promise<FrontendResult> {
  const timeout = opts.timeoutMs ?? 120000;
  const browser = await chromium.launch({ headless: !opts.headed });
  const heap: HeapPoint[] = [];
  try {
    const context = await browser.newContext();
    const page = await context.newPage();
    await page.addInitScript(LONGTASK_INIT);
    const cdp = await context.newCDPSession(page);
    await cdp.send("Performance.enable");

    const finished: Request[] = [];
    page.on("requestfinished", (r) => finished.push(r));

    // Sample JS heap on an interval across load + interaction.
    const t0 = Date.now();
    let sampling = false;
    const heapTimer = setInterval(async () => {
      if (sampling) return;
      sampling = true;
      const mb = await readHeapMB(cdp).catch(() => null);
      if (mb != null) heap.push({ t: Date.now() - t0, heapMB: mb });
      sampling = false;
    }, 200);

    await browser.startTracing(page, { screenshots: false });
    await page.goto(url, { waitUntil: "commit", timeout });
    await waitReady(page, timeout);

    const marks = await page.evaluate(() => window.__sqlinsitePerf?.marks ?? {});
    const nav = await page.evaluate(() => {
      const t = performance.getEntriesByType("navigation")[0] as PerformanceNavigationTiming | undefined;
      return t
        ? { domContentLoadedMs: Math.round(t.domContentLoadedEventEnd), loadMs: Math.round(t.loadEventEnd) }
        : { domContentLoadedMs: null, loadMs: null };
    });

    await forceGC(cdp);
    const jsHeapUsedMB = await readHeapMB(cdp);

    // Scripted interaction for the loaded view.
    const interaction = await runInteraction(page, opts, timeout);

    clearInterval(heapTimer);
    const traceBuf = await browser.stopTracing();

    const longs = await page.evaluate(() => window.__perfLongTasks ?? []);
    const longTasks = { count: longs.length, totalMs: Math.round(longs.reduce((a, b) => a + b, 0)) };

    // Optional leak loop (navigates again; runs after the trace is closed).
    let leak: LeakResult | undefined;
    if ((opts.repeat ?? 1) > 1) leak = await leakLoop(page, cdp, url, opts.repeat!, timeout);

    // Per-endpoint network for /api/* plus a total transferred-bytes tally.
    const api: NetReq[] = [];
    let totalBytes = 0;
    for (const r of finished) {
      const sizes = await r.sizes().catch(() => null);
      const bytes = sizes?.responseBodySize ?? 0;
      totalBytes += bytes;
      if (!r.url().includes("/api/")) continue;
      const resp = await r.response().catch(() => null);
      const t = r.timing();
      const u = new URL(r.url());
      api.push({ url: u.pathname + u.search, method: r.method(), status: resp?.status() ?? 0, ms: t.responseEnd > 0 ? Math.round(t.responseEnd) : 0, bytes });
    }

    // Write large artifacts to the run dir.
    fs.writeFileSync(path.join(opts.outDir, "trace.json"), traceBuf);
    fs.writeFileSync(path.join(opts.outDir, "heap-timeline.json"), JSON.stringify(heap, null, 2));

    return {
      url,
      navToReadyMs: marks["app-ready"] != null ? Math.round(marks["app-ready"]) : null,
      marks,
      jsHeapUsedMB,
      heapPeakMB: heap.length ? Math.max(...heap.map((h) => h.heapMB)) : jsHeapUsedMB,
      longTasks,
      interaction,
      leak,
      nav,
      network: { total: finished.length, totalBytes, apiRequests: api },
      artifacts: ["trace.json", "heap-timeline.json"],
    };
  } finally {
    await browser.close();
  }
}
