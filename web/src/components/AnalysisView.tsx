import { useAnalysis, type AnalysisTab } from "../state/analysisStore.ts";
import type { AnalysisResult } from "../core/types.ts";
import { PredefinedMetrics } from "./PredefinedMetrics.tsx";

const TABS: { id: AnalysisTab; label: string }[] = [
  { id: "metrics", label: "Predefined Metrics" },
  { id: "query", label: "Query Metrics" },
];

function fmt(v: unknown): string {
  return v === null || v === undefined ? "NULL" : String(v);
}

// The Query-Metrics results table (plain — no row→page mapping, no sub-tabs; the
// first 1,000 rows are shown).
function AnalysisResults({ result }: { result: AnalysisResult | null }) {
  if (!result) return <div className="results-msg muted">Run a query to see results.</div>;
  if (result.error) return <div className="results-msg error">{result.error}</div>;
  const cols = result.columns ?? [];
  const rows = result.rows ?? [];
  const shown = rows.slice(0, 1000);
  return (
    <div className="analysis-results">
      <table>
        <thead><tr>{cols.map((c, i) => <th key={i}>{c.name}</th>)}</tr></thead>
        <tbody>
          {shown.map((r, ri) => (
            <tr key={ri}>{r.map((v, ci) => <td key={ci}>{fmt(v)}</td>)}</tr>
          ))}
        </tbody>
      </table>
      {rows.length > shown.length && (
        <div className="muted">showing {shown.length} of {result.rowCount} rows</div>
      )}
    </div>
  );
}

// The Analysis view content (right of the shared B-Tree Tree): a Predefined-metrics
// dashboard or a Query-Metrics editor over the unified analysis connection.
export function AnalysisView() {
  const tab = useAnalysis((s) => s.tab);
  const setTab = useAnalysis((s) => s.setTab);
  const sql = useAnalysis((s) => s.sql);
  const setSql = useAnalysis((s) => s.setSql);
  const run = useAnalysis((s) => s.run);
  const running = useAnalysis((s) => s.running);
  const result = useAnalysis((s) => s.result);

  return (
    <div id="analysis-view">
      <div className="results-tabs">
        {TABS.map((t) => (
          <button key={t.id} className={"rtab" + (tab === t.id ? " active" : "")}
                  onClick={() => setTab(t.id)}>
            {t.label}
          </button>
        ))}
      </div>
      {tab === "metrics" ? (
        <PredefinedMetrics />
      ) : (
        <div id="analysis-query">
          <div className="aq-editor">
            <textarea value={sql} onChange={(e) => setSql(e.target.value)}
                      spellCheck={false}
                      placeholder="SQL over the primary db + map. / profile. / manifest." />
            <div className="aq-bar">
              <button onClick={() => void run()} disabled={running}>
                {running ? "Running…" : "Run"}
              </button>
            </div>
          </div>
          <div className="aq-results"><AnalysisResults result={result} /></div>
        </div>
      )}
    </div>
  );
}
