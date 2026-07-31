import { useTree } from "../state/treeStore.ts";
import { useViz } from "../state/store.ts";
import { formatBytes, formatCount } from "../core/format.ts";
import { PageCard } from "./PageCard.tsx";
import { IndexTable } from "./IndexTable.tsx";

// Right-hand pane shown when a table grouping node is selected in the b-tree tree:
// an overview of the table (its SQL, page/row counts, size, root page) and its
// indexes. Root-page controls navigate + reveal the corresponding b-tree.
export function TableOverview() {
  const overview = useTree((s) => s.overview);
  const loading = useTree((s) => s.contentLoading);
  const revealPage = useTree((s) => s.revealPage);
  const pageSize = useViz((s) => s.meta?.meta.pageSize ?? 0);

  if (!overview) return <div className="results-msg muted">{loading ? "Loading overview…" : "No overview."}</div>;

  const { name, type, sql, pageCount, rowCount, rootPage, indexes } = overview;
  return (
    <div className="page-detail">
      <div className="pd-fixed">
        <div className="pd-head">
          {name} · <span className="muted">{type}</span>
          {rootPage != null &&
            <span className="pd-owner">root <PageCard page={rootPage} onClick={revealPage} /></span>}
        </div>
        <div className="pd-headerfields">
          {rowCount != null && <span className="pd-field"><b>rows</b> {formatCount(rowCount)}</span>}
          {pageCount != null && <span className="pd-field"><b>pages</b> {formatCount(pageCount)}</span>}
          {pageCount != null && pageSize > 0 &&
            <span className="pd-field"><b>size</b> {formatBytes(pageCount * pageSize)}</span>}
          <span className="pd-field"><b>indexes</b> {formatCount(indexes.length)}</span>
        </div>
      </div>

      <div className="pd-scroll">
        <div className="pd-table">
          {sql && (
            <div className="to-section">
              <div className="to-title">SQL</div>
              <pre className="to-sql">{sql}</pre>
            </div>
          )}
          <div className="to-section">
            <div className="to-title">Indexes</div>
            <IndexTable indexes={indexes} pageSize={pageSize} onNav={revealPage} />
          </div>
        </div>
      </div>
    </div>
  );
}
