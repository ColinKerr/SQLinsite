import type { TreeNode } from "../core/treeModel.ts";
import { colorForPage, GLYPH } from "../core/palette.ts";
import { formatBytes } from "../core/format.ts";

// The visual content of a b-tree tree node — icon (page-type glyph / table color
// block / Indexes folder) + name + muted subtree size (pages · bytes). Shared by
// the tree rows and the Node Search dropdown so both render nodes identically.
export function TreeNodeContent({ node, pageSize }: { node: TreeNode; pageSize: number }) {
  return (
    <>
      {node.kind === "table" ? (
        // A solid color block (same object color as this table's pages), same
        // size/shape as a page glyph but with no icon.
        <span className="tn-glyph" style={{ background: colorForPage(node.objectId ?? null, "") }} />
      ) : node.pageType ? (
        <span className="tn-glyph"
              style={{ background: colorForPage(node.objectId ?? null, node.pageType) }}>
          {GLYPH[node.pageType] ?? "·"}
        </span>
      ) : node.kind === "indexes" ? (
        <span className="tn-glyph tn-folder">⊞</span>
      ) : null}
      <span className="tn-label">{node.label}</span>
      {node.edgeKind === "overflow" && <span className="tn-tag">overflow</span>}
      {/* Subtree size (this node + its children): page count and bytes, muted. */}
      {node.subtreePageCount != null && (
        <span className="tn-size muted">
          {node.subtreePageCount} {node.subtreePageCount === 1 ? "page" : "pages"}
          {pageSize > 0 && ` · ${formatBytes(node.subtreePageCount * pageSize)}`}
        </span>
      )}
    </>
  );
}
