import type { ObjectInfo } from "../core/types.ts";
import type { NodeActivate } from "../state/treeSelectionStore.ts";

// Pure factories for each view's b-tree node-activation handler. The view wires
// them with its own hooks and registers the result; keeping the logic here makes
// it unit-testable without rendering the (canvas / Monaco) view components.

// Query view: fill the results view with the activated node's rows.
//  - Table grouping node → the whole table.
//  - Table interior / leaf / overflow page → that page's exact rows (interior =
//    its subtree, leaf = its own rows, overflow = its owner leaf's rows), fetched
//    a run-batch at a time by the store.
//  - Any index node (index b-tree page or the "Indexes" grouping node) → nothing.
export function makeQueryActivate(deps: {
  objById: Map<number, ObjectInfo>;
  runObjectQuery: (table: string) => void;
  runPageQuery: (page: number, overflow: boolean) => void;
}): NodeActivate {
  return (node) => {
    if (node.kind === "indexes") return;                      // Indexes grouping node
    const obj = node.objectId != null ? deps.objById.get(node.objectId) : undefined;
    if (obj?.type !== "table") return;                        // index pages / sqlite_schema / none
    if (node.kind === "table") { deps.runObjectQuery(obj.name); return; }  // whole table
    if (node.kind === "page" && node.page != null) {
      const pt = node.pageType ?? "";
      if (pt === "table-leaf" || pt === "table-interior" || pt === "overflow")
        deps.runPageQuery(node.page, pt === "overflow");
    }
  };
}

// Pages/Tables view: link a tree node to the canvas (see PAGES_AND_TABLES_VIEWS.md).
// A node click never switches the content view.
//  - Any page node → scroll to and select that page's block in the current view
//    (its grid cell in Pages, its band cell in Tables — passing the owning objectId
//    so Tables can locate the band).
//  - Table grouping node → select the object: the controller scrolls to and selects
//    the table's first leaf page in the Pages view, or the object's band in Tables.
//  - Index grouping node (and any other non-table grouping node) → nothing.
export function makeCanvasActivate(
  selectPage: (page: number, objectId: number | null, pageType: string | null) => void,
  setSelectedObject: (id: number) => void,
): NodeActivate {
  return (node) => {
    if (node.page != null) selectPage(node.page, node.objectId ?? null, node.pageType ?? null);
    else if (node.kind === "table" && node.objectId != null) setSelectedObject(node.objectId);
  };
}

// Page Tree view: load the node's detail/overview into the right-hand pane.
export function makeTreeActivate(deps: {
  selectTable: (objectId: number, key: string) => void;
  selectIndexes: (objectId: number, key: string) => void;
  selectPage: (page: number) => void;
}): NodeActivate {
  return (node) => {
    if (node.kind === "table" && node.objectId != null) deps.selectTable(node.objectId, node.key);
    else if (node.kind === "indexes" && node.objectId != null) deps.selectIndexes(node.objectId, node.key);
    else if (node.page != null) deps.selectPage(node.page);
  };
}
