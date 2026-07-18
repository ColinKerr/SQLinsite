import type { TreeNode } from "../core/treeModel.ts";
import type { ObjectInfo } from "../core/types.ts";
import type { NodeActivate } from "../state/treeSelectionStore.ts";

// Pure factories for each view's b-tree node-activation handler. The view wires
// them with its own hooks and registers the result; keeping the logic here makes
// it unit-testable without rendering the (canvas / Monaco) view components.

const qi = (name: string) => `"${name.replace(/"/g, '""')}"`;

// The table a node resolves to: a table node/page is its own table; an index
// node/page maps to the table it indexes (its `tableName`).
function tableOf(node: TreeNode, objById: Map<number, ObjectInfo>): string | undefined {
  if (node.objectId == null) return undefined;
  const obj = objById.get(node.objectId);
  if (!obj) return undefined;
  return obj.type === "table" ? obj.name : obj.tableName ?? undefined;
}

// Query view: run the query for the node's table, filling the results view.
export function makeQueryActivate(
  objById: Map<number, ObjectInfo>, runSql: (sql: string) => void,
): NodeActivate {
  return (node) => {
    const table = tableOf(node, objById);
    if (table) runSql(`SELECT * FROM ${qi(table)};`);
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
