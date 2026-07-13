import type { PageBasics, TreeChild, TreeRoot } from "./types.ts";

// A node in the b-tree tree. `page` is null for the virtual roots and for the
// "load more" loader rows.
export interface TreeNode extends PageBasics {
  key: string;
  kind: "page" | "freelist" | "other" | "more";
  label: string;
  page: number | null;
  pageType: string | null;
  edgeKind?: string; // how it attaches to its parent: child | overflow | freelist-leaf | freelist-trunk
  hasChildren: boolean;
  objectId?: number | null;
  // Loader ("more") rows:
  loaderParent?: string;
  loaderKind?: "freelist" | "other";
  after?: number;
}

// The subset of PageBasics fields, copied verbatim onto a node.
function basics(x: PageBasics): PageBasics {
  return { cellCount: x.cellCount, freeBytes: x.freeBytes };
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
  if (r.kind === "other") {
    return { key: "other", kind: "other", label: r.label, page: null,
             pageType: null, hasChildren: true };
  }
  return { key: `r:${r.page}`, kind: "page", label: r.label, page: r.page,
           pageType: r.pageType, objectId: r.objectId, hasChildren: truthy(r.hasChildren),
           ...basics(r) };
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

export function moreNode(parentKey: string, loaderKind: "freelist" | "other", after: number): TreeNode {
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
