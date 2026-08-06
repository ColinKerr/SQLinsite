import { useQuery } from "../state/queryStore.ts";
import { formatCount } from "../core/format.ts";
import { ResultsGrid } from "./ResultsGrid.tsx";

// The Query view's results table: the shared ResultsGrid wired to the query store,
// with page-provenance features on (per-cell page tint, hover, click→Cell Details,
// overflow highlight, lazy row loading) and the pages/accesses footer.
export function ResultsTable() {
  const rows = useQuery((s) => s.rows);
  const run = useQuery((s) => s.run);
  const error = useQuery((s) => s.error);
  const loadMoreRows = useQuery((s) => s.loadMoreRows);
  const selectedCell = useQuery((s) => s.selectedCell);
  const setSelectedCell = useQuery((s) => s.setSelectedCell);
  const highlightPage = useQuery((s) => s.highlightPage);

  const footer = run && rows ? (
    <div className="rt-foot muted">
      showing {formatCount(rows.rows.length)} of {formatCount(run.rowCount)} rows
      {run.truncated ? " (truncated)" : ""} · {formatCount(run.pageCount)} pages · {formatCount(run.accesses)} accesses
    </div>
  ) : undefined;

  return (
    <ResultsGrid
      columns={rows?.columns ?? []}
      rows={rows && run ? rows.rows : null}
      rowCount={run?.rowCount ?? 0}
      error={error}
      runKey={run?.queryId ?? null}
      rowPages={rows?.rowPages}
      selectedCell={selectedCell}
      onSelectCell={(c) =>
        setSelectedCell(rows ? { rowIndex: c.rowIndex, colIndex: c.colIndex, column: rows.columns[c.colIndex], value: c.value, pages: c.pages } : null)}
      highlightPage={highlightPage}
      onLoadMore={loadMoreRows}
      footer={footer}
    />
  );
}
