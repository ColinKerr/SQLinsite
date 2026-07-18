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

// Pages/Tables view: scroll the canvas to the node — the page's own block in Pages
// view, otherwise the start of the object it belongs to (the controller scrolls to
// the object's band in Tables and its first block in Pages).
export function makeCanvasActivate(
  view: string, goToPage: (page: number) => void, setSelectedObject: (id: number) => void,
): NodeActivate {
  return (node) => {
    if (view === "pages" && node.page != null) goToPage(node.page);
    else if (node.objectId != null) setSelectedObject(node.objectId);
    else if (node.page != null) goToPage(node.page);
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
