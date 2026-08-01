import { spawnSync } from "node:child_process";
import type { ProcStats, RssSample } from "./types.ts";

// Polls a process's resident memory and CPU via `ps` on an interval. RSS is in
// KB (as `ps` reports it); CPU is percent. Handles the process disappearing
// mid-run (samples simply stop accumulating).
export class ProcSampler {
  private samples: RssSample[] = [];
  private timer?: ReturnType<typeof setInterval>;

  constructor(private pid: number, private intervalMs = 250) {}

  start(): void {
    this.tick();
    this.timer = setInterval(() => this.tick(), this.intervalMs);
  }

  private tick(): void {
    const r = spawnSync("ps", ["-o", "rss=,pcpu=", "-p", String(this.pid)], { encoding: "utf8" });
    const line = (r.stdout ?? "").trim();
    if (!line) return;
    const [rssKB, cpu] = line.split(/\s+/).map(Number);
    if (Number.isFinite(rssKB)) this.samples.push({ t: Date.now(), rssKB, cpu: cpu || 0 });
  }

  stop(): ProcStats {
    if (this.timer) clearInterval(this.timer);
    const rss = this.samples.map((s) => s.rssKB);
    const cpus = this.samples.map((s) => s.cpu);
    const avg = cpus.length ? cpus.reduce((a, b) => a + b, 0) / cpus.length : 0;
    return {
      peakRssKB: rss.length ? Math.max(...rss) : 0,
      avgCpu: Math.round(avg * 10) / 10,
      maxCpu: cpus.length ? Math.max(...cpus) : 0,
      samples: this.samples.length,
    };
  }

  raw(): RssSample[] {
    return this.samples;
  }
}
