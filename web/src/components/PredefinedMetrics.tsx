import { useEffect, useState } from "react";
import { runAnalysisQuery } from "../core/api.ts";
import { useViz } from "../state/store.ts";
import { useAnalysis } from "../state/analysisStore.ts";
import { formatCount } from "../core/format.ts";

// ---- tiny hand-rolled (dependency-free) charts -----------------------------

function Gauge({ frac, label, danger }: { frac: number; label: string; danger?: boolean }) {
  const pct = Math.round(Math.max(0, Math.min(1, frac)) * 100);
  return (
    <div className="pm-gauge">
      <div className="pm-gauge-bar">
        <div className="pm-gauge-fill" style={{ width: `${pct}%`, background: danger ? "#e0a030" : "#3b7dd8" }} />
      </div>
      <div className="pm-gauge-label">{pct}% · {label}</div>
    </div>
  );
}

function BarList({ items, unit }: { items: { label: string; value: number }[]; unit?: string }) {
  const max = Math.max(1, ...items.map((i) => i.value));
  return (
    <div className="pm-bars">
      {items.map((it, i) => (
        <div className="pm-bar-row" key={i}>
          <span className="pm-bar-label" title={it.label}>{it.label}</span>
          <span className="pm-bar-track"><span className="pm-bar-fill" style={{ width: `${(it.value / max) * 100}%` }} /></span>
          <span className="pm-bar-val">{formatCount(it.value)}{unit ?? ""}</span>
        </div>
      ))}
    </div>
  );
}

function Histogram({ bins, labels }: { bins: number[]; labels: string[] }) {
  const max = Math.max(1, ...bins);
  return (
    <div className="pm-hist">
      {bins.map((v, i) => (
        <div className="pm-hist-col" key={i} title={`${labels[i]}: ${v}`}>
          <div className="pm-hist-bar" style={{ height: `${(v / max) * 100}%` }} />
          <div className="pm-hist-x">{labels[i]}</div>
        </div>
      ))}
    </div>
  );
}

// A metric card that runs canned SQL over the unified analysis connection and hands
// its rows to `render`. Re-runs when `deps` change (scope/source selection).
function Metric({ title, sql, render, deps = [] }: {
  title: string; sql: string;
  render: (rows: unknown[][]) => JSX.Element; deps?: unknown[];
}) {
  const [rows, setRows] = useState<unknown[][] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const openSql = useAnalysis((s) => s.openSql);
  useEffect(() => {
    let cancelled = false;
    void runAnalysisQuery(sql).then((r) => {
      if (cancelled) return;
      if (r.error) { setError(r.error); setRows(null); }
      else { setError(null); setRows(r.rows ?? []); }
    });
    return () => { cancelled = true; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, deps);
  return (
    <div className="pm-card">
      <div className="pm-card-head">
        <span className="pm-title">{title}</span>
        <button className="pm-sql" title="Open this SQL in Query Metrics" onClick={() => void openSql(sql)}>SQL</button>
      </div>
      {error ? <div className="results-msg error">{error}</div>
        : rows === null ? <div className="muted">…</div>
        : render(rows)}
    </div>
  );
}

const FREE = "('freelist-trunk','freelist-leaf','unallocated')";
const n = (v: unknown) => (typeof v === "number" ? v : Number(v) || 0);

// SQL fragment restricting profile rows to the selected sources.
function selClause(selSources: Set<number>, sourceCount: number): string {
  if (sourceCount === 0 || selSources.size === 0 || selSources.size === sourceCount) return "";
  return ` WHERE sourceId IN (${[...selSources].join(",")})`;
}

export function PredefinedMetrics() {
  const hasManifest = useViz((s) => s.hasManifest && s.manifestMatch);
  const hasProfile = useViz((s) => s.hasProfile);
  const ppb = useViz((s) => s.pagesPerBlock);
  const blockSize = useViz((s) => (s.meta?.blockSize as number) ?? 0);
  const selSources = useViz((s) => s.selSources);
  const sourceCount = useViz((s) => s.sourceCount);
  const sel = selClause(selSources, sourceCount);
  const selKey = [...selSources].join(",");

  return (
    <div id="predefined-metrics">
      <h3>Storage efficiency</h3>
      <div className="pm-grid">
        <Metric title="Free space" sql={
          `SELECT (SELECT count(*) FROM map.pages WHERE pageType IN ${FREE}) AS freePages,` +
          ` (SELECT count(*) FROM map.pages) AS total`}
          render={(r) => {
            const free = n(r[0]?.[0]), total = n(r[0]?.[1]);
            return (<>
              {ppb > 0 && <div className="pm-note">≈ {formatCount(Math.floor(free / ppb))} reclaimable blocks</div>}
              <Gauge frac={total ? free / total : 0} label={`${formatCount(free)} free of ${formatCount(total)} pages`} danger />
            </>);
          }} />

        <Metric title="Fill factor (leaf pages)" sql={
          `SELECT 1.0 - CAST(COALESCE(SUM(freeBytes),0) AS REAL) /` +
          ` (count(*) * (SELECT pageSize FROM map.meta)) FROM map.pages WHERE pageType LIKE '%-leaf'`}
          render={(r) => <Gauge frac={n(r[0]?.[0])} label="pages filled (higher is denser)" />} />

        <Metric title="Pages per object (top 10)" sql={
          `SELECT o.name, count(*) FROM map.pages p JOIN map.objects o ON o.id=p.objectId` +
          ` GROUP BY o.id ORDER BY 2 DESC LIMIT 10`}
          render={(r) => <BarList items={r.map((x) => ({ label: String(x[0]), value: n(x[1]) }))} />} />
      </div>

      {hasManifest && (<>
        <h3>Block analysis</h3>
        <div className="pm-grid">
          <Metric title="Changed vs shared with parent" sql={
            `SELECT SUM(sharedWithParent) AS shared, count(*)-SUM(sharedWithParent) AS changed` +
            ` FROM manifest.blocks WHERE dbId=(SELECT selectedDbId FROM manifest.meta)`}
            render={(r) => {
              const shared = n(r[0]?.[0]), changed = n(r[0]?.[1]), tot = shared + changed;
              return (<>
                <div className="pm-note">{tot ? Math.round((100 * changed) / tot) : 0}% of blocks rewritten this checkpoint</div>
                <BarList items={[{ label: "changed / new", value: changed }, { label: "shared w/ parent", value: shared }]} unit=" blk" />
              </>);
            }} />

          <Metric title="Blocks by free fraction" sql={
            `WITH b AS (SELECT (pageNumber-1)/(SELECT pagesPerBlock FROM manifest.meta) AS bi,` +
            ` avg(CASE WHEN pageType IN ${FREE} THEN 1.0 ELSE 0.0 END) AS f FROM map.pages GROUP BY bi)` +
            ` SELECT CAST(MIN(f*10,9) AS INT) AS bin, count(*) FROM b GROUP BY bin ORDER BY bin`}
            render={(r) => {
              const bins = new Array(10).fill(0);
              for (const x of r) bins[n(x[0])] = n(x[1]);
              return <Histogram bins={bins} labels={bins.map((_, i) => `${i * 10}%`)} />;
            }} />
        </div>
      </>)}

      {hasProfile && hasManifest && (<>
        <h3>Session working set</h3>
        <div className="pm-grid">
          <Metric title="Blocks a session loads" deps={[selKey]} sql={
            `SELECT count(DISTINCT (pageNumber-1)/(SELECT pagesPerBlock FROM manifest.meta)) AS blocks,` +
            ` count(DISTINCT pageNumber) AS pages FROM profile.page_access${sel}`}
            render={(r) => {
              const blocks = n(r[0]?.[0]), pages = n(r[0]?.[1]);
              const bytes = blocks * blockSize, locality = blocks * ppb ? pages / (blocks * ppb) : 0;
              return (<>
                <div className="pm-note">≈ {(bytes / (1024 * 1024)).toFixed(0)} MB downloaded · locality {(locality * 100).toFixed(1)}%</div>
                <BarList items={[{ label: "distinct blocks pulled", value: blocks }, { label: "pages accessed", value: pages }]} />
              </>);
            }} />
        </div>
      </>)}
    </div>
  );
}
