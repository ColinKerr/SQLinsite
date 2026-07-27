import type { PageBasics, TreeBtree, TreeChild, TreeRoot, TreeSearchMatch } from "./types.ts";

// A node in the b-tree tree. `page` is null for the virtual roots (including the
// "table"/"indexes" grouping nodes) and for the "load more" loader rows.
export interface TreeNode extends PageBasics {
  key: string;
  kind: "page" | "table" | "indexes" | "freelist" | "pointermap" | "other" | "more";
  label: string;
  page: number | null;
  pageType: string | null;
  edgeKind?: string; // how it attaches to its parent: child | overflow | freelist-leaf | freelist-trunk
  hasChildren: boolean;
  objectId?: number | null;
  // Grouping nodes ("table"/"indexes"): the b-tree(s) to build children from
  // client-side (no fetch — schema-object cardinality is small).
  tableBtree?: TreeBtree | null;
  indexes?: TreeBtree[];
  // Loader ("more") rows:
  loaderParent?: string;
  loaderKind?: "freelist" | "pointermap" | "other";
  after?: number;
}

// The subset of PageBasics fields, copied verbatim onto a node.
function basics(x: PageBasics): PageBasics {
  return { cellCount: x.cellCount, freeBytes: x.freeBytes, subtreePageCount: x.subtreePageCount };
}

// Sum of subtree page counts across a set of b-trees (for grouping-node totals).
function sumSubtree(btrees: TreeBtree[]): number {
  return btrees.reduce((n, b) => n + (b.subtreePageCount ?? 0), 0);
}

export interface FlatNode {
  node: TreeNode;
  depth: number;
  expanded: boolean;
}

const truthy = (v: number | boolean | undefined) => v === true || v === 1;

export function rootNode(r: TreeRoot): TreeNode {
  if (r.kind === "freelist") {
    return { key: "freelist", kind: "freelist", label: r.label, page: null,
             pageType: "freelist-trunk", hasChildren: true };
  }
  if (r.kind === "pointermap") {
    return { key: "pointermap", kind: "pointermap", label: r.label, page: null,
             pageType: "pointer-map", hasChildren: true };
  }
  if (r.kind === "other") {
    return { key: "other", kind: "other", label: r.label, page: null,
             pageType: null, hasChildren: true };
  }
  if (r.kind === "table") {
    // A grouping node: its children (b-tree page + Indexes group) build from the
    // inlined payload without a fetch. `hasChildren` is true when it has a b-tree.
    // Its subtree size covers the table b-tree plus every index b-tree.
    const indexes = r.indexes ?? [];
    return { key: `t:${r.objectId}`, kind: "table", label: r.label, page: null,
             pageType: null, objectId: r.objectId,
             hasChildren: r.tableBtree != null || indexes.length > 0,
             tableBtree: r.tableBtree ?? null, indexes,
             subtreePageCount: (r.tableBtree?.subtreePageCount ?? 0) + sumSubtree(indexes) };
  }
  return { key: `r:${r.page}`, kind: "page", label: r.label, page: r.page,
           pageType: r.pageType, objectId: r.objectId, hasChildren: truthy(r.hasChildren),
           ...basics(r) };
}

// A table/index b-tree root page node, built from the roots payload (no fetch).
// `rel` namespaces the key segment ("table" | "index") under `parentKey`.
export function btreeChildNode(parentKey: string, b: TreeBtree, rel: "table" | "index"): TreeNode {
  return {
    key: `${parentKey}>${rel}:${b.page}`,
    kind: "page",
    label: b.label,
    page: b.page,
    pageType: b.pageType,
    objectId: b.objectId ?? null,
    edgeKind: "child",
    hasChildren: truthy(b.hasChildren),
    ...basics(b),
  };
}

// A page node for a Node Search match (rendered only in the search dropdown).
export function searchNode(m: TreeSearchMatch): TreeNode {
  return {
    key: `search:${m.page}`,
    kind: "page",
    label: m.label,
    page: m.page,
    pageType: m.pageType,
    objectId: m.objectId ?? null,
    hasChildren: truthy(m.hasChildren),
    ...basics(m),
  };
}

// The "Indexes" grouping node under a table node; carries the index b-trees so
// its children build client-side. `objectId` is the owning table's id (used to
// fetch the Index Overview); its subtree size is the sum of the index b-trees.
export function indexesGroupNode(
  tableKey: string, tableObjectId: number | null | undefined, indexes: TreeBtree[],
): TreeNode {
  return { key: `${tableKey}>indexes`, kind: "indexes", label: "Indexes", page: null,
           pageType: null, objectId: tableObjectId ?? null, hasChildren: indexes.length > 0,
           indexes, subtreePageCount: sumSubtree(indexes) };
}

// A page child under `parentKey`; `edge` overrides the API's edge kind (used for
// freelist trunks, which arrive without one).
export function childNode(parentKey: string, c: TreeChild, edge?: string): TreeNode {
  const edgeKind = edge ?? c.kind;
  return {
    key: `${parentKey}>${edgeKind}:${c.page}`,
    kind: "page",
    label: `Page ${c.page}`,
    page: c.page,
    pageType: c.pageType,
    objectId: c.objectId,
    edgeKind,
    hasChildren: truthy(c.hasChildren),
    ...basics(c),
  };
}

export function moreNode(parentKey: string, loaderKind: "freelist" | "pointermap" | "other", after: number): TreeNode {
  return { key: `${parentKey}>more:${after}`, kind: "more", label: "Load more…", page: null,
           pageType: null, hasChildren: false, loaderParent: parentKey, loaderKind, after };
}

// Depth-first flatten of the expanded nodes into a render list.
export function flattenTree(
  roots: TreeNode[],
  childrenByKey: Record<string, TreeNode[]>,
  expanded: Set<string>,
): FlatNode[] {
  const out: FlatNode[] = [];
  const walk = (nodes: TreeNode[], depth: number) => {
    for (const node of nodes) {
      const isExpanded = expanded.has(node.key);
      out.push({ node, depth, expanded: isExpanded });
      if (isExpanded) {
        const kids = childrenByKey[node.key];
        if (kids) walk(kids, depth + 1);
      }
    }
  };
  walk(roots, 0);
  return out;
}
