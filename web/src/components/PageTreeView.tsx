import { useMemo } from "react";
import { useTree } from "../state/treeStore.ts";
import { useRegisterNodeActivation } from "../state/treeSelectionStore.ts";
import { makeTreeActivate } from "./nodeActivation.ts";
import { PageDetail } from "./PageDetail.tsx";
import { TableOverview } from "./TableOverview.tsx";
import { IndexOverview } from "./IndexOverview.tsx";

// The Page Tree view content (to the right of the shared Navigation Panel): the
// detail pane for the node selected in the tree — a page's detail, or a table /
// index overview.
export function PageTreeView() {
  const overviewMode = useTree((s) => s.overviewMode);
  const selectTable = useTree((s) => s.selectTable);
  const selectIndexes = useTree((s) => s.selectIndexes);
  const selectPage = useTree((s) => s.selectPage);

  // Activating a node loads its detail/overview into the pane on the right.
  useRegisterNodeActivation(useMemo(
    () => makeTreeActivate({ selectTable, selectIndexes, selectPage }),
    [selectTable, selectIndexes, selectPage],
  ));

  return (
    <div className="tv-pane" style={{ flex: "1 1 auto" }}>
      {overviewMode === "index" ? <IndexOverview />
        : overviewMode === "table" ? <TableOverview />
        : <PageDetail />}
    </div>
  );
}
