import { useTree } from "../state/treeStore.ts";
import { PageCard } from "./PageDetail.tsx";

// Right-hand pane shown when a table grouping node is selected in the b-tree tree:
// an overview of the table (its SQL, page/row counts, root page) and its indexes.
// Root-page controls navigate + reveal the corresponding b-tree in the tree.
export function TableOverview() {
  const overview = useTree((s) => s.overview);
  const loading = useTree((s) => s.contentLoading);
  const revealPage = useTree((s) => s.revealPage);

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
          {rowCount != null && <span className="pd-field"><b>rows</b> {rowCount}</span>}
          {pageCount != null && <span className="pd-field"><b>pages</b> {pageCount}</span>}
          <span className="pd-field"><b>indexes</b> {indexes.length}</span>
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
            {indexes.length === 0 ? (
              <div className="muted">No indexes.</div>
            ) : (
              <table className="pd-coltable">
                <thead><tr><th>name</th><th>pages</th><th>root</th></tr></thead>
                <tbody>
                  {indexes.map((ix) => (
                    <tr key={ix.name}>
                      <td>{ix.name}</td>
                      <td className="muted">{ix.pageCount ?? "—"}</td>
                      <td>{ix.rootPage != null
                        ? <PageCard page={ix.rootPage} onClick={revealPage} />
                        : "—"}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}
