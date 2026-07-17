import { useEffect, useMemo, useRef, useState } from "react";
import { useTree } from "../state/treeStore.ts";
import { useViz } from "../state/store.ts";
import { useQuery } from "../state/queryStore.ts";
import { useController } from "../state/ControllerContext.tsx";
import { flattenTree, type FlatNode, type TreeNode } from "../core/treeModel.ts";
import { colorForPage, GLYPH } from "../core/palette.ts";
import { pageTypeDesc } from "../core/pageTypes.ts";
import { TreeNodeContent } from "./TreeNodeContent.tsx";
import { NodeSearch } from "./NodeSearch.tsx";

// SQL identifier quoting for the Query view's "run this object" click.
const qi = (name: string) => `"${name.replace(/"/g, '""')}"`;

const ROW_H = 22;
const INDENT = 14;

// Page types shown in the key at the bottom of the tree (glyph symbology).
const KEY_TYPES = [
  "table-leaf", "table-interior", "index-leaf", "index-interior", "overflow",
  "freelist-trunk", "freelist-leaf", "pointer-map", "lock-byte", "unallocated",
];

// Virtualized, lazily-loaded b-tree tree. Only expanded nodes are flattened and
// only the visible row window is rendered, so it scales to huge files.
export function BTreeTree() {
  const roots = useTree((s) => s.roots);
  const childrenByKey = useTree((s) => s.childrenByKey);
  const expanded = useTree((s) => s.expanded);
  const loading = useTree((s) => s.loading);
  const selectedPage = useTree((s) => s.selectedPage);
  const selectedKey = useTree((s) => s.selectedKey);
  const toggle = useTree((s) => s.toggle);
  const selectPage = useTree((s) => s.selectPage);
  const selectTable = useTree((s) => s.selectTable);
  const selectIndexes = useTree((s) => s.selectIndexes);
  const highlightNode = useTree((s) => s.highlightNode);
  const loadMore = useTree((s) => s.loadMore);
  const pageSize = useViz((s) => s.meta?.meta.pageSize ?? 0);
  const view = useViz((s) => s.view);
  const objById = useViz((s) => s.objById);
  const controller = useController();

  const bodyRef = useRef<HTMLDivElement>(null);
  const [scrollTop, setScrollTop] = useState(0);
  const [height, setHeight] = useState(400);

  // Re-run once the body actually mounts (it isn't rendered while roots load), or
  // the ResizeObserver would never attach and the row window would stay tiny.
  useEffect(() => {
    const el = bodyRef.current;
    if (!el) return;
    const ro = new ResizeObserver(() => setHeight(el.clientHeight));
    ro.observe(el);
    setHeight(el.clientHeight);
    return () => ro.disconnect();
  }, [roots]);

  const flat: FlatNode[] = useMemo(
    () => (roots ? flattenTree(roots, childrenByKey, expanded) : []),
    [roots, childrenByKey, expanded],
  );

  // When the selection changes (e.g. via a page-detail pointer link that reveals
  // and expands the node), scroll it into view if it's off-screen.
  useEffect(() => {
    const el = bodyRef.current;
    if (el == null || selectedPage == null) return;
    const idx = flat.findIndex((f) => f.node.page === selectedPage);
    if (idx < 0) return;
    const y = idx * ROW_H;
    if (y < el.scrollTop || y > el.scrollTop + el.clientHeight - ROW_H) {
      el.scrollTop = Math.max(0, y - el.clientHeight / 2);
    }
  }, [selectedPage, flat]);

  if (!roots) return <div className="results-msg muted">Loading tree…</div>;

  const total = flat.length;
  const start = Math.max(0, Math.floor(scrollTop / ROW_H) - 5);
  const end = Math.min(total, start + Math.ceil(height / ROW_H) + 10);

  // Selecting a node does something different per view (the tree itself and its
  // shared selection highlight are the same everywhere):
  //  - Page Tree: show the node's detail/overview in the right pane.
  //  - Pages: scroll the canvas to the page (or the object's first block).
  //  - Tables: scroll to the object's band.
  //  - Query: run the object's data into the results view.
  const select = (node: TreeNode) => {
    if (view === "tree") {
      if (node.kind === "table" && node.objectId != null) void selectTable(node.objectId, node.key);
      else if (node.kind === "indexes" && node.objectId != null) void selectIndexes(node.objectId, node.key);
      else if (node.page != null) void selectPage(node.page);
      return;
    }
    highlightNode(node); // keep the shared highlight in sync in every view
    if (view === "pages" && node.page != null) {
      controller?.goToPage(node.page);
    } else if (view === "pages" || view === "tables") {
      if (node.objectId != null) useViz.getState().setSelectedObject(node.objectId);
      else if (node.page != null) controller?.goToPage(node.page);
    } else if (view === "query" && node.objectId != null) {
      const obj = objById.get(node.objectId);
      const table = obj ? (obj.type === "table" ? obj.name : obj.tableName) : undefined;
      if (table) void useQuery.getState().runSql(`SELECT * FROM ${qi(table)};`);
    }
  };

  // Clicking the row body: a collapsed node expands (body clicks never collapse —
  // only the chevron does), and the node is selected either way.
  const onRowClick = (node: TreeNode, isOpen: boolean) => {
    if (node.kind === "more") { void loadMore(node); return; }
    if (node.hasChildren && !isOpen) void toggle(node);
    select(node);
  };

  const rows = [];
  for (let i = start; i < end; i++) {
    const { node, depth, expanded: isOpen } = flat[i];
    const isSel = node.page != null
      ? node.page === selectedPage
      : node.key === selectedKey;
    rows.push(
      <div key={node.key} className={"tn-row" + (isSel ? " tn-sel" : "")}
           style={{ position: "absolute", top: i * ROW_H, height: ROW_H, left: 0, right: 0,
                    paddingLeft: 6 + depth * INDENT }}
           onClick={() => onRowClick(node, isOpen)}>
        {/* The chevron column toggles expand/collapse without changing selection
            (it stops the row click). For non-expandable rows it has no handler, so
            clicks fall through to the row body (select / load-more). */}
        <span className="tn-exp"
              onClick={node.kind !== "more" && node.hasChildren
                ? (e) => { e.stopPropagation(); void toggle(node); }
                : undefined}>
          {node.kind !== "more" && node.hasChildren &&
            <span className={"tn-arrow" + (isOpen ? " open" : "")}>›</span>}
        </span>
        {node.kind === "more" ? (
          <span className="tn-more">{loading.has(node.loaderParent ?? "") ? "Loading…" : "Load more…"}</span>
        ) : (
          <TreeNodeContent node={node} pageSize={pageSize} />
        )}
      </div>,
    );
  }

  return (
    <div className="btree">
      <NodeSearch pageSize={pageSize} />
      <div className="btree-body" ref={bodyRef}
           onScroll={(e) => setScrollTop(e.currentTarget.scrollTop)}>
        <div style={{ height: total * ROW_H, position: "relative" }}>{rows}</div>
      </div>
      <div className="btree-legend">
        {KEY_TYPES.map((t) => (
          <span key={t} className="bk-item" title={pageTypeDesc(t)}>
            <span className="bk-glyph" style={{ background: colorForPage(null, t) }}>{GLYPH[t] ?? "·"}</span>
            {t}
          </span>
        ))}
      </div>
    </div>
  );
}
