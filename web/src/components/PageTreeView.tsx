import { useTree } from "../state/treeStore.ts";
import { PageDetail } from "./PageDetail.tsx";
import { TableOverview } from "./TableOverview.tsx";
import { IndexOverview } from "./IndexOverview.tsx";

// The Page Tree view content (to the right of the shared Navigation Panel): the
// detail pane for the node selected in the tree — a page's detail, or a table /
// index overview.
export function PageTreeView() {
  const overviewMode = useTree((s) => s.overviewMode);
  return (
    <div className="tv-pane" style={{ flex: "1 1 auto" }}>
      {overviewMode === "index" ? <IndexOverview />
        : overviewMode === "table" ? <TableOverview />
        : <PageDetail />}
    </div>
  );
}
