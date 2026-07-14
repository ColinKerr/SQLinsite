import { beforeEach, describe, expect, it, vi } from "vitest";
import { useTree } from "./treeStore.ts";

function jsonResp(body: unknown) {
  return { ok: true, json: async () => body } as Response;
}

beforeEach(() => {
  useTree.setState({
    roots: null, childrenByKey: {}, expanded: new Set(), loading: new Set(),
    selectedPage: null, selectedKey: null, content: null, overview: null, contentLoading: false,
  });
  vi.stubGlobal("fetch", vi.fn(async (url: string) => {
    const u = String(url);
    if (u === "/api/tree/roots") {
      // Page 1, then a "table" grouping node holding table T's b-tree (page 2)
      // and one index T_n (page 3).
      return jsonResp({ roots: [
        { kind: "page", label: "sqlite_schema", page: 1, pageType: "table-leaf", objectId: null, hasChildren: 0 },
        { kind: "table", label: "T", page: null, pageType: null, objectId: 1, hasChildren: true,
          tableBtree: { kind: "page", label: "T (table)", page: 2, pageType: "table-interior", objectId: 1, hasChildren: 1 },
          indexes: [
            { kind: "page", label: "T_n (index)", page: 3, pageType: "index-leaf", objectId: 2, hasChildren: 0 },
          ] },
      ] });
    }
    if (u.startsWith("/api/tree/children?page=2")) {
      return jsonResp({ children: [
        { page: 5, kind: "child", pageType: "table-leaf", objectId: 1, hasChildren: 1 },
        { page: 6, kind: "child", pageType: "table-leaf", objectId: 1, hasChildren: 0 },
      ] });
    }
    if (u.startsWith("/api/tree/path?page=5")) {
      return jsonResp({ path: [{ page: 2, edgeKind: null }, { page: 5, edgeKind: "child" }] });
    }
    if (u.startsWith("/api/tree/path?page=3")) {
      return jsonResp({ path: [{ page: 3, edgeKind: null }] });
    }
    if (u.startsWith("/api/tree/object?id=1")) {
      return jsonResp({ overview: { objectId: 1, type: "table", name: "T", sql: "CREATE TABLE T(...)",
        pageCount: 4, rootPage: 2, rowCount: 42, indexes: [{ name: "T_n", pageCount: 2, rootPage: 3 }] } });
    }
    if (u.startsWith("/api/page/5/content")) return jsonResp({ pageNumber: 5, regions: [] });
    return jsonResp({});
  }));
});

describe("tree store", () => {
  it("loadRoots builds page + table grouping nodes", async () => {
    await useTree.getState().loadRoots();
    const roots = useTree.getState().roots!;
    expect(roots.map((r) => r.kind)).toEqual(["page", "table"]);
    expect(roots[0].page).toBe(1);
    expect(roots[0].hasChildren).toBe(false);   // Page 1 (leaf)
    expect(roots[1].page).toBeNull();           // table node has no page of its own
    expect(roots[1].hasChildren).toBe(true);    // has a b-tree
    expect(roots[1].key).toBe("t:1");
  });

  it("expanding a table node yields its b-tree and Indexes group (no fetch)", async () => {
    await useTree.getState().loadRoots();
    const table = useTree.getState().roots!.find((r) => r.kind === "table")!;
    await useTree.getState().toggle(table);
    const kids = useTree.getState().childrenByKey[table.key];
    expect(kids.map((n) => n.label)).toEqual(["T (table)", "Indexes"]);
    expect(kids[0].page).toBe(2);
    expect(kids[1].kind).toBe("indexes");
  });

  it("toggle expands a b-tree page node and loads its children (from /children)", async () => {
    await useTree.getState().loadRoots();
    const table = useTree.getState().roots!.find((r) => r.kind === "table")!;
    await useTree.getState().toggle(table);
    const btree = useTree.getState().childrenByKey[table.key].find((n) => n.page === 2)!;
    await useTree.getState().toggle(btree);
    expect(useTree.getState().expanded.has(btree.key)).toBe(true);
    expect(useTree.getState().childrenByKey[btree.key].map((n) => n.page)).toEqual([5, 6]);
  });

  it("Indexes group yields the index b-tree nodes", async () => {
    await useTree.getState().loadRoots();
    const table = useTree.getState().roots!.find((r) => r.kind === "table")!;
    await useTree.getState().toggle(table);
    const indexes = useTree.getState().childrenByKey[table.key].find((n) => n.kind === "indexes")!;
    await useTree.getState().toggle(indexes);
    const idxKids = useTree.getState().childrenByKey[indexes.key];
    expect(idxKids.map((n) => n.label)).toEqual(["T_n (index)"]);
    expect(idxKids[0].page).toBe(3);
  });

  it("selectTable loads the overview and marks the node selected", async () => {
    await useTree.getState().loadRoots();
    const table = useTree.getState().roots!.find((r) => r.kind === "table")!;
    await useTree.getState().selectTable(1, table.key);
    const s = useTree.getState();
    expect(s.selectedKey).toBe(table.key);
    expect(s.selectedPage).toBeNull();
    expect(s.overview?.name).toBe("T");
    expect(s.overview?.rowCount).toBe(42);
  });

  it("revealPage expands the synthetic table chain down to the page", async () => {
    await useTree.getState().loadRoots();
    await useTree.getState().revealPage(5);
    const s = useTree.getState();
    expect(s.selectedPage).toBe(5);
    expect(s.expanded.has("t:1")).toBe(true);              // table grouping node
    expect(s.expanded.has("t:1>table:2")).toBe(true);      // b-tree root page node
    expect(s.childrenByKey["t:1>table:2"].map((n) => n.page)).toContain(5);
  });

  it("revealPage loads roots first if the tree has not mounted (e.g. from Query view)", async () => {
    expect(useTree.getState().roots).toBeNull();
    await useTree.getState().revealPage(5);
    const s = useTree.getState();
    expect(s.roots).not.toBeNull();                        // roots loaded on demand
    expect(s.selectedPage).toBe(5);
    expect(s.expanded.has("t:1")).toBe(true);
  });

  it("toggle collapses an already-expanded node", async () => {
    await useTree.getState().loadRoots();
    const table = useTree.getState().roots!.find((r) => r.kind === "table")!;
    await useTree.getState().toggle(table);
    await useTree.getState().toggle(table);
    expect(useTree.getState().expanded.has(table.key)).toBe(false);
  });

  it("clicking a leaf node selects it (loads page content, no expand)", async () => {
    await useTree.getState().loadRoots();
    const page1 = useTree.getState().roots!.find((r) => r.page === 1)!;
    await useTree.getState().toggle(page1); // leaf → selects
    expect(useTree.getState().selectedPage).toBe(1);
    expect(useTree.getState().expanded.has(page1.key)).toBe(false);
  });
});
