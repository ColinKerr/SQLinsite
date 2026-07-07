import { useEffect, useMemo, useRef, useState } from "react";
import { useTree } from "../state/treeStore.ts";
import { flattenTree, type FlatNode, type TreeNode } from "../core/treeModel.ts";
import { colorForPage, GLYPH } from "../core/palette.ts";
import { pageTypeDesc } from "../core/pageTypes.ts";

const ROW_H = 22;
const INDENT = 14;

// Hover popover text: the page-type description plus basic page details.
function nodeTitle(node: TreeNode): string {
  if (!node.pageType) return node.label;
  const parts = [
    node.cellCount != null ? `${node.cellCount} cells` : null,
    node.freeBytes != null ? `${node.freeBytes} bytes free` : null,
    node.rowidMin != null && node.rowidMax != null ? `rowids ${node.rowidMin}–${node.rowidMax}` : null,
  ].filter(Boolean);
  return `${node.label} — ${pageTypeDesc(node.pageType)}` + (parts.length ? "\n" + parts.join(" · ") : "");
}

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
  const toggle = useTree((s) => s.toggle);
  const selectPage = useTree((s) => s.selectPage);
  const loadMore = useTree((s) => s.loadMore);

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

  // Clicking the row body selects the node; expand/collapse is the arrow's job.
  const onRowClick = (node: TreeNode) => {
    if (node.kind === "more") { void loadMore(node); return; }
    if (node.page != null) void selectPage(node.page);
  };

  const rows = [];
  for (let i = start; i < end; i++) {
    const { node, depth, expanded: isOpen } = flat[i];
    const isSel = node.page != null && node.page === selectedPage;
    rows.push(
      <div key={node.key} className={"tn-row" + (isSel ? " tn-sel" : "")}
           style={{ position: "absolute", top: i * ROW_H, height: ROW_H, left: 0, right: 0,
                    paddingLeft: 6 + depth * INDENT }}
           onClick={() => onRowClick(node)}
           title={nodeTitle(node)}>
        <span className="tn-exp" onClick={(e) => { e.stopPropagation();
                    if (node.kind === "more") void loadMore(node); else if (node.hasChildren) void toggle(node); }}>
          {node.kind !== "more" && node.hasChildren &&
            <span className={"tn-arrow" + (isOpen ? " open" : "")}>›</span>}
        </span>
        {node.kind === "more" ? (
          <span className="tn-more">{loading.has(node.loaderParent ?? "") ? "Loading…" : "Load more…"}</span>
        ) : (
          <>
            {node.pageType && (
              <span className="tn-glyph"
                    style={{ background: colorForPage(node.objectId ?? null, node.pageType) }}>
                {GLYPH[node.pageType] ?? "·"}
              </span>
            )}
            <span className="tn-label">{node.label}</span>
            {node.edgeKind === "overflow" && <span className="tn-tag">overflow</span>}
          </>
        )}
      </div>,
    );
  }

  return (
    <div className="btree">
      <div className="btree-head">b-tree tree</div>
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
