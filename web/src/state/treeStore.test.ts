import { beforeEach, describe, expect, it, vi } from "vitest";
import { useTree } from "./treeStore.ts";

function jsonResp(body: unknown) {
  return { ok: true, json: async () => body } as Response;
}

beforeEach(() => {
  useTree.setState({
    roots: null, childrenByKey: {}, expanded: new Set(), loading: new Set(),
    selectedPage: null, content: null, contentLoading: false,
  });
  vi.stubGlobal("fetch", vi.fn(async (url: string) => {
    const u = String(url);
    if (u === "/api/tree/roots") {
      return jsonResp({ roots: [
        { kind: "page", label: "Page 1", page: 1, pageType: "table-leaf", objectId: null, hasChildren: 0 },
        { kind: "page", label: "T (table)", page: 2, pageType: "table-interior", objectId: 1, hasChildren: 1 },
      ] });
    }
    if (u.startsWith("/api/tree/children?page=2")) {
      return jsonResp({ children: [
        { page: 5, kind: "child", pageType: "table-leaf", objectId: 1, hasChildren: 1 },
        { page: 6, kind: "child", pageType: "table-leaf", objectId: 1, hasChildren: 0 },
      ] });
    }
    if (u.startsWith("/api/page/5/content")) return jsonResp({ pageNumber: 5, regions: [] });
    return jsonResp({});
  }));
});

describe("tree store", () => {
  it("loadRoots builds root nodes and coerces hasChildren", async () => {
    await useTree.getState().loadRoots();
    const roots = useTree.getState().roots!;
    expect(roots.map((r) => r.page)).toEqual([1, 2]);
    expect(roots[0].hasChildren).toBe(false); // Page 1 (leaf)
    expect(roots[1].hasChildren).toBe(true);  // T interior
  });

  it("toggle expands a page node and loads its children (from /children)", async () => {
    await useTree.getState().loadRoots();
    const t = useTree.getState().roots!.find((r) => r.page === 2)!;
    await useTree.getState().toggle(t);
    expect(useTree.getState().expanded.has(t.key)).toBe(true);
    // The regression: children come from the response's `children` key, not `pages`.
    expect(useTree.getState().childrenByKey[t.key].map((n) => n.page)).toEqual([5, 6]);
  });

  it("toggle collapses an already-expanded node", async () => {
    await useTree.getState().loadRoots();
    const t = useTree.getState().roots!.find((r) => r.page === 2)!;
    await useTree.getState().toggle(t);
    await useTree.getState().toggle(t);
    expect(useTree.getState().expanded.has(t.key)).toBe(false);
  });

  it("clicking a leaf node selects it (loads page content, no expand)", async () => {
    await useTree.getState().loadRoots();
    const page1 = useTree.getState().roots!.find((r) => r.page === 1)!;
    await useTree.getState().toggle(page1); // leaf → selects
    expect(useTree.getState().selectedPage).toBe(1);
    expect(useTree.getState().expanded.has(page1.key)).toBe(false);
  });
});
