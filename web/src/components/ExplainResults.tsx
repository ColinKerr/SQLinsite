import { useQuery } from "../state/queryStore.ts";
import type { ExplainResult } from "../core/types.ts";

function SimpleTable({ title, data }: { title: string; data?: { columns: string[]; rows: unknown[][] } }) {
  if (!data) return null;
  return (
    <div className="explain-block">
      <div className="dd-title">{title}</div>
      <table className="simple-table">
        <thead>
          <tr>{data.columns.map((c, i) => <th key={i}>{c}</th>)}</tr>
        </thead>
        <tbody>
          {data.rows.map((r, ri) => (
            <tr key={ri}>{r.map((v, ci) => <td key={ci}>{v === null ? "" : String(v)}</td>)}</tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

// Presentational Explain view (query plan + bytecode); shared by the Query and
// Analysis views. Both run EXPLAIN QUERY PLAN + EXPLAIN and render the results here.
export function ExplainView({ explain }: { explain: ExplainResult | null }) {
  if (!explain) return <div className="results-msg muted">Press Explain to see the query plan.</div>;
  if (explain.error) return <div className="results-msg error">{explain.error}</div>;
  return (
    <div className="explain-results">
      <SimpleTable title="EXPLAIN QUERY PLAN" data={explain.queryPlan} />
      <SimpleTable title="EXPLAIN (bytecode)" data={explain.explain} />
    </div>
  );
}

// The Query view's Explain tab, wired to the query store.
export function ExplainResults() {
  const explain = useQuery((s) => s.explain);
  return <ExplainView explain={explain} />;
}
