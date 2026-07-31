import { useTree } from "../state/treeStore.ts";
import { useViz } from "../state/store.ts";
import { formatCount } from "../core/format.ts";
import { IndexTable } from "./IndexTable.tsx";

// Right-hand pane shown when a table's "Indexes" grouping node is selected: the
// same indexes table as the Table Overview, plus a Statement column (each index's
// CREATE INDEX SQL). Backed by the owning table's overview.
export function IndexOverview() {
  const overview = useTree((s) => s.overview);
  const loading = useTree((s) => s.contentLoading);
  const revealPage = useTree((s) => s.revealPage);
  const pageSize = useViz((s) => s.meta?.meta.pageSize ?? 0);

  if (!overview) return <div className="results-msg muted">{loading ? "Loading overview…" : "No overview."}</div>;

  const { name, indexes } = overview;
  return (
    <div className="page-detail">
      <div className="pd-fixed">
        <div className="pd-head">
          Indexes · <span className="muted">{name}</span>
        </div>
        <div className="pd-headerfields">
          <span className="pd-field"><b>indexes</b> {formatCount(indexes.length)}</span>
        </div>
      </div>

      <div className="pd-scroll">
        <div className="pd-table">
          <div className="to-section">
            <IndexTable indexes={indexes} pageSize={pageSize} showStatement onNav={revealPage} />
          </div>
        </div>
      </div>
    </div>
  );
}
