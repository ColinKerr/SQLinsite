import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { RESULTS_DIR } from "./config.ts";

// Synthetic SQLite generator for the perf harness. The goal is to reproduce the
// bottlenecks seen on real large files — chiefly `/api/tables/structural-groups`,
// which full-scans the map's `pages` table 4× (no index on pageType), so its cost
// scales with *page count*. We reach a high page count cheaply by using a small
// page_size plus overflow blobs (each row spans several pages), which also mirrors
// the real file's overflow-heavy profile (≈half its pages are overflow).
//
// Deterministic in structure (fixed sizes → fixed page layout); blob *contents*
// come from randomblob, which doesn't affect page counts or query cost.

export interface GenOpts {
  pages: number; // approximate target page count in the mapped db
  tables: number; // number of tables (each with 2 indexes) → object/tree fidelity
  blobBytes: number; // per-row blob → drives overflow pages
  pageSize: number; // small = more pages per byte
  out: string;
}

const SCALES: Record<string, number> = {
  small: 50_000, // ~25 MB, structural-groups scan is fast but the shape shows
  medium: 300_000, // ~150 MB
  large: 1_000_000, // ~0.5 GB, structural-groups clearly dominates load
  xlarge: 4_000_000, // ~2 GB, approaches the real file's ~5s magnitude
};

// Pages consumed per row ≈ one leaf slot share + the blob's overflow chain.
function pagesPerRow(blobBytes: number, pageSize: number): number {
  const usableOverflow = pageSize - 4; // 4-byte next-page pointer per overflow page
  return 1 + Math.max(0, Math.ceil((blobBytes - pageSize / 4) / usableOverflow));
}

function buildSql(o: GenOpts): string {
  const perRow = pagesPerRow(o.blobBytes, o.pageSize);
  // Leave headroom for index/interior pages (~15%); solve rows from target pages.
  const rowsPerTable = Math.max(1, Math.round((o.pages * 0.85) / (o.tables * perRow)));
  const lines: string[] = [
    `PRAGMA page_size=${o.pageSize};`,
    "PRAGMA journal_mode=OFF;",
    "PRAGMA synchronous=OFF;",
    "BEGIN;",
  ];
  for (let t = 0; t < o.tables; t++) {
    lines.push(
      `CREATE TABLE t${t} (id INTEGER PRIMARY KEY, k INTEGER, s TEXT, body BLOB);`,
      `CREATE INDEX t${t}_k ON t${t}(k);`,
      `CREATE INDEX t${t}_s ON t${t}(s);`,
      `INSERT INTO t${t}(k,s,body) WITH RECURSIVE c(i) AS (` +
        `SELECT 1 UNION ALL SELECT i+1 FROM c WHERE i < ${rowsPerTable}) ` +
        `SELECT i, 'k'||i, randomblob(${o.blobBytes}) FROM c;`,
    );
  }
  lines.push("COMMIT;", "PRAGMA optimize;");
  return lines.join("\n") + "\n";
}

// Pipe the generated SQL to the `sqlite3` CLI to build the db file.
function runSqlite(dbPath: string, sql: string): Promise<void> {
  return new Promise((resolve, reject) => {
    const child = spawn("sqlite3", [dbPath], { stdio: ["pipe", "inherit", "inherit"] });
    child.on("error", reject);
    child.on("close", (code) => (code === 0 ? resolve() : reject(new Error(`sqlite3 exited ${code}`))));
    child.stdin!.write(sql);
    child.stdin!.end();
  });
}

export async function generate(o: GenOpts): Promise<{ out: string; bytes: number }> {
  fs.mkdirSync(path.dirname(o.out), { recursive: true });
  fs.rmSync(o.out, { force: true }); // fresh db each time
  const perRow = pagesPerRow(o.blobBytes, o.pageSize);
  const rowsPerTable = Math.max(1, Math.round((o.pages * 0.85) / (o.tables * perRow)));
  console.log(`▶ generating ${o.out}`);
  console.log(`  target ~${o.pages.toLocaleString()} pages · ${o.tables} tables × ${rowsPerTable.toLocaleString()} rows · ${o.blobBytes}B blob · page_size ${o.pageSize}`);
  const t0 = Date.now();
  await runSqlite(o.out, buildSql(o));
  const bytes = fs.statSync(o.out).size;
  console.log(`  done in ${((Date.now() - t0) / 1000).toFixed(1)}s · ${(bytes / (1024 * 1024)).toFixed(1)} MB`);
  return { out: o.out, bytes };
}

// ---- CLI -------------------------------------------------------------------

const HELP = `Synthetic SQLite generator for the perf harness.

Usage:
  npm run generate -- --scale medium [--out <path>]
  npm run generate -- --pages 1000000 --tables 12 --blob 2048 --page-size 512

Options:
  --scale <s>     small | medium | large | xlarge (target page count).
  --pages <n>     Explicit target page count (overrides --scale).
  --tables <n>    Tables, each with 2 indexes (default 12).
  --blob <n>      Per-row blob bytes; drives overflow pages (default 2048).
  --page-size <n> SQLite page size; smaller = more pages per byte (default 512).
  --out <path>    Output db (default perf/results/synth/<scale>.db).
  -h, --help      This help.

Then profile it (the harness maps it first):
  npm run perf -- --db <path>`;

async function main() {
  const argv = process.argv.slice(2);
  let scale = "medium";
  let pages: number | null = null;
  let tables = 12, blob = 2048, pageSize = 512;
  let out: string | null = null;
  for (let i = 0; i < argv.length; i++) {
    const [flag, inlineVal] = argv[i].split(/=(.*)/s);
    const val = () => inlineVal ?? argv[++i];
    switch (flag) {
      case "--scale": scale = val(); break;
      case "--pages": pages = Number(val()); break;
      case "--tables": tables = Number(val()); break;
      case "--blob": blob = Number(val()); break;
      case "--page-size": pageSize = Number(val()); break;
      case "--out": out = val(); break;
      case "-h": case "--help": console.log(HELP); return;
      default: throw new Error(`unknown argument: ${flag}`);
    }
  }
  const target = pages ?? SCALES[scale];
  if (!target) throw new Error(`unknown --scale '${scale}' (small|medium|large|xlarge)`);
  const outPath = out ?? path.join(RESULTS_DIR, "synth", `${pages ? `p${pages}` : scale}.db`);
  await generate({ pages: target, tables, blobBytes: blob, pageSize, out: outPath });
  console.log(`\nProfile it with:\n  npm run perf -- --db '${outPath}'`);
}

main().catch((e) => {
  console.error(`\n✗ ${e.message}`);
  process.exit(1);
});
