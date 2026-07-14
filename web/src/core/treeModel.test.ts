import { describe, it, expect } from "vitest";
import {
  btreeChildNode, childNode, flattenTree, indexesGroupNode, moreNode, rootNode, type TreeNode,
} from "./treeModel.ts";

describe("treeModel", () => {
  it("builds roots and coerces hasChildren", () => {
    expect(rootNode({ kind: "freelist", label: "Freelist", page: null, pageType: "freelist-trunk", objectId: null, hasChildren: true }).key).toBe("freelist");
    expect(rootNode({ kind: "other", label: "All other pages", page: null, pageType: null, objectId: null, hasChildren: true }).key).toBe("other");
    const t = rootNode({ kind: "page", label: "T (table)", page: 2, pageType: "table-leaf", objectId: 1, hasChildren: 1 });
    expect(t).toMatchObject({ key: "r:2", page: 2, hasChildren: true });
    const leaf = rootNode({ kind: "page", label: "Page 1", page: 1, pageType: "table-leaf", objectId: null, hasChildren: 0 });
    expect(leaf.hasChildren).toBe(false);
  });

  it("a table root is a grouping node keyed by object id, carrying its b-trees", () => {
    const tableBtree = { kind: "page" as const, label: "T (table)", page: 2, pageType: "table-interior", objectId: 1, hasChildren: 1 };
    const indexes = [{ kind: "page" as const, label: "T_n (index)", page: 3, pageType: "index-leaf", objectId: 2, hasChildren: 0 }];
    const n = rootNode({ kind: "table", label: "T", page: null, pageType: null, objectId: 1, hasChildren: true, tableBtree, indexes });
    expect(n).toMatchObject({ key: "t:1", kind: "table", page: null, hasChildren: true });
    expect(n.tableBtree).toEqual(tableBtree);
    expect(n.indexes).toEqual(indexes);
  });

  it("btreeChildNode / indexesGroupNode build deterministic keys under a table node", () => {
    const tableBtree = { kind: "page" as const, label: "T (table)", page: 2, pageType: "table-interior", objectId: 1, hasChildren: 1 };
    const bt = btreeChildNode("t:1", tableBtree, "table");
    expect(bt.key).toBe("t:1>table:2");
    expect(bt).toMatchObject({ kind: "page", page: 2, hasChildren: true, edgeKind: "child" });

    const idx = [{ kind: "page" as const, label: "T_n (index)", page: 3, pageType: "index-leaf", objectId: 2, hasChildren: 0 }];
    const grp = indexesGroupNode("t:1", idx);
    expect(grp.key).toBe("t:1>indexes");
    expect(grp).toMatchObject({ kind: "indexes", hasChildren: true });
    expect(btreeChildNode(grp.key, idx[0], "index").key).toBe("t:1>indexes>index:3");
  });

  it("childNode uses the parent key + edge kind for a unique key", () => {
    const c = childNode("r:5", { page: 9, kind: "overflow", pageType: "overflow", objectId: null, hasChildren: 0 });
    expect(c.key).toBe("r:5>overflow:9");
    expect(c.edgeKind).toBe("overflow");
    expect(c.hasChildren).toBe(false);
  });

  it("flattenTree only descends into expanded nodes, tracking depth", () => {
    const roots: TreeNode[] = [
      { key: "r:2", kind: "page", label: "T", page: 2, pageType: "table-interior", hasChildren: true },
      { key: "other", kind: "other", label: "All other pages", page: null, pageType: null, hasChildren: true },
    ];
    const children = {
      "r:2": [childNode("r:2", { page: 3, kind: "child", pageType: "table-leaf", objectId: 1, hasChildren: 0 })],
      "other": [moreNode("other", "other", 99)],
    };

    // Nothing expanded → only the two roots at depth 0.
    let flat = flattenTree(roots, children, new Set());
    expect(flat.map((f) => f.node.key)).toEqual(["r:2", "other"]);
    expect(flat.every((f) => f.depth === 0)).toBe(true);

    // Expand r:2 → its child appears at depth 1, before the sibling root.
    flat = flattenTree(roots, children, new Set(["r:2"]));
    expect(flat.map((f) => f.node.key)).toEqual(["r:2", "r:2>child:3", "other"]);
    expect(flat[1].depth).toBe(1);
  });
});
