import { useQuery } from "../state/queryStore.ts";

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

export function ExplainResults() {
  const explain = useQuery((s) => s.explain);
  if (!explain) return <div className="results-msg muted">Press Explain to see the query plan.</div>;
  if (explain.error) return <div className="results-msg error">{explain.error}</div>;
  return (
    <div className="explain-results">
      <SimpleTable title="EXPLAIN QUERY PLAN" data={explain.queryPlan} />
      <SimpleTable title="EXPLAIN (bytecode)" data={explain.explain} />
    </div>
  );
}
