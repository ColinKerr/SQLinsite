import { spawn, type ChildProcess } from "node:child_process";
import http from "node:http";
import net from "node:net";
import type { HttpSample, TimedRun } from "./types.ts";

export const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

// Ask the OS for a free TCP port (bind :0, read the assigned port, release it).
// We pass this concrete port to the server rather than parsing its stdout — the
// server's "serving on …" line is fully buffered when stdout is a pipe (not a
// TTY), so it never arrives until the process exits.
export function getFreePort(): Promise<number> {
  return new Promise((resolve, reject) => {
    const srv = net.createServer();
    srv.on("error", reject);
    srv.listen(0, "127.0.0.1", () => {
      const addr = srv.address();
      const port = typeof addr === "object" && addr ? addr.port : 0;
      srv.close(() => resolve(port));
    });
  });
}

// Run a command to completion, timing wall clock and capturing peak RSS via the
// BSD `/usr/bin/time -l` wrapper (macOS prints "N  maximum resident set size" in
// bytes to stderr). The wrapped command's own stdio is forwarded so long builds
// and maps show progress.
export async function runTimed(cmd: string, args: string[]): Promise<TimedRun> {
  const start = performance.now();
  const child = spawn("/usr/bin/time", ["-l", cmd, ...args], {
    stdio: ["ignore", "inherit", "pipe"],
  });
  let stderr = "";
  child.stderr.on("data", (d) => {
    stderr += d.toString();
    process.stderr.write(d);
  });
  const exitCode = await new Promise<number>((res) => child.on("close", (c) => res(c ?? -1)));
  const wallMs = performance.now() - start;
  const m = stderr.match(/(\d+)\s+maximum resident set size/);
  const peakRssKB = m ? Math.round(Number(m[1]) / 1024) : null;
  return { wallMs, peakRssKB, exitCode };
}

// A long-running server bound to a concrete port. `base` is the URL to hit;
// readiness is confirmed separately via waitForReady polling /api/meta.
export interface ServeHandle {
  child: ChildProcess;
  base: string;
}

export function startServe(bin: string, mapFile: string, dbFile: string | null, port: number): ServeHandle {
  const args = ["visualize", "serve", "--map-file", mapFile, "--port", String(port)];
  if (dbFile) args.push("--db-file", dbFile);
  const child = spawn(bin, args, { stdio: ["ignore", "pipe", "pipe"] });
  // Drain/forward the server's output so its pipe never fills, but don't depend
  // on it for readiness (see getFreePort).
  child.stdout!.on("data", (d) => process.stderr.write(d));
  child.stderr!.on("data", (d) => process.stderr.write(d));
  return { child, base: `http://127.0.0.1:${port}` };
}

// Stop a child gracefully (SIGINT, like Ctrl-C on the server), escalating to
// SIGKILL if it doesn't exit within `graceMs`.
export async function stopProcess(child: ChildProcess, graceMs = 3000): Promise<void> {
  if (child.exitCode != null || child.signalCode) return;
  child.kill("SIGINT");
  const exited = await Promise.race([
    new Promise<boolean>((res) => child.on("exit", () => res(true))),
    sleep(graceMs).then(() => false),
  ]);
  if (!exited) child.kill("SIGKILL");
}

// GET a URL, returning status/latency/size plus the body text (bodies here are
// bounded — meta and small page ranges).
export function httpGet(url: string): Promise<HttpSample & { body: string }> {
  return new Promise((resolve, reject) => {
    const start = performance.now();
    const req = http.get(url, (res) => {
      let body = "";
      let bytes = 0;
      res.on("data", (c: Buffer) => {
        bytes += c.length;
        body += c.toString();
      });
      res.on("end", () =>
        resolve({ url, status: res.statusCode ?? 0, ms: performance.now() - start, bytes, body }),
      );
    });
    req.on("error", reject);
    req.setTimeout(30000, () => req.destroy(new Error(`timeout: ${url}`)));
  });
}

// Poll `/api/meta` until it answers 200, returning ms elapsed since `sinceMs`.
export async function waitForReady(baseUrl: string, sinceMs: number, timeoutMs = 120000): Promise<number> {
  const start = performance.now();
  while (performance.now() - start < timeoutMs) {
    try {
      const s = await httpGet(`${baseUrl}/api/meta`);
      if (s.status === 200) return performance.now() - sinceMs;
    } catch {
      // server not up yet
    }
    await sleep(200);
  }
  throw new Error("server did not become ready within timeout");
}
