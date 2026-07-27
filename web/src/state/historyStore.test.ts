import { beforeEach, describe, expect, it, vi } from "vitest";
import { useHistory } from "./historyStore.ts";
import { useViz } from "./store.ts";
import { useTree } from "./treeStore.ts";
import { entryToParams, paramsToEntry, type NavEntry, type NavNode } from "../core/history.ts";

const E = (view: NavEntry["view"], node: NavNode = null): NavEntry => ({ view, node, label: `${view}` });

function jsonResp(body: unknown) {
  return { ok: true, json: async () => body } as Response;
}

beforeEach(() => {
  localStorage.clear();
  history.replaceState(null, "", "/");
  useHistory.setState({ entries: [], index: -1, applying: false });
  useViz.setState({ view: "pages", selectedObject: null, objById: new Map() });
  useTree.setState({ roots: null, childrenByKey: {}, expanded: new Set(), selectedPage: null });
  vi.stubGlobal("fetch", vi.fn(async () => jsonResp({})));
});

describe("core/history url round-trip", () => {
  it("encodes and decodes each node kind", () => {
    expect(entryToParams(E("tree", { page: 5 }))).toBe("view=tree&page=5");
    expect(entryToParams(E("pages", { object: 3 }))).toBe("view=pages&obj=3");
    expect(entryToParams(E("query"))).toBe("view=query");
    expect(paramsToEntry("?view=pages&obj=3")).toEqual({ view: "pages", node: { object: 3 } });
    expect(paramsToEntry("?view=tree&page=5")).toEqual({ view: "tree", node: { page: 5 } });
    expect(paramsToEntry("?nope=1")).toBeNull();
  });
});

describe("historyStore.record", () => {
  it("dedups consecutive identical entries", () => {
    useHistory.getState().record(E("pages"));
    useHistory.getState().record(E("pages"));
    expect(useHistory.getState().entries).toHaveLength(1);
    expect(useHistory.getState().index).toBe(0);
  });

  it("truncates forward history when recording after going back", () => {
    const h = useHistory.getState();
    h.record(E("pages"));
    h.record(E("tables"));
    h.record(E("query"));
    expect(useHistory.getState().index).toBe(2);
    useHistory.getState().back(); // -> tables (index 1)
    expect(useHistory.getState().index).toBe(1);
    useHistory.getState().record(E("tree")); // truncates "query"
    expect(useHistory.getState().entries.map((e) => e.view)).toEqual(["pages", "tables", "tree"]);
    expect(useHistory.getState().index).toBe(2);
  });

  it("is a no-op while applying (guard)", () => {
    useHistory.setState({ applying: true });
    useHistory.getState().record(E("tables"));
    expect(useHistory.getState().entries).toHaveLength(0);
  });
});

describe("historyStore back/forward/jumpTo", () => {
  it("applies the target view when moving through the stack", () => {
    const h = useHistory.getState();
    h.record(E("pages"));
    h.record(E("tables"));
    h.record(E("query"));
    useHistory.getState().back();
    expect(useViz.getState().view).toBe("tables");
    expect(useHistory.getState().index).toBe(1);
    useHistory.getState().back();
    expect(useViz.getState().view).toBe("pages");
    useHistory.getState().forward();
    expect(useViz.getState().view).toBe("tables");
    useHistory.getState().jumpTo(2);
    expect(useViz.getState().view).toBe("query");
  });

  it("restores a selected object when applying a page-view entry", () => {
    const h = useHistory.getState();
    h.record(E("tree"));
    h.record(E("pages", { object: 7 }));
    useHistory.getState().back();       // tree
    expect(useViz.getState().selectedObject).toBeNull();
    useHistory.getState().forward();    // pages · object 7
    expect(useViz.getState().view).toBe("pages");
    expect(useViz.getState().selectedObject).toBe(7);
  });

  it("does not push new entries when navigating back/forward", () => {
    const h = useHistory.getState();
    h.record(E("pages"));
    h.record(E("tables"));
    useHistory.getState().back();
    useHistory.getState().forward();
    expect(useHistory.getState().entries).toHaveLength(2);
  });
});

describe("historyStore persistence", () => {
  it("persists to localStorage and hydrates back", () => {
    const h = useHistory.getState();
    h.record(E("pages"));
    h.record(E("tables", { object: 2 }));
    const saved = JSON.parse(localStorage.getItem("sqlinsite:navhistory")!);
    expect(saved.entries).toHaveLength(2);
    expect(saved.index).toBe(1);

    useHistory.setState({ entries: [], index: -1 });
    useHistory.getState().hydrate();
    expect(useHistory.getState().entries).toHaveLength(2);
    expect(useHistory.getState().index).toBe(1);
  });

  it("mirrors the current entry into the URL", () => {
    useHistory.getState().record(E("tree", { page: 9 }));
    expect(location.search).toBe("?view=tree&page=9");
  });
});

describe("historyStore.restoreFromUrl", () => {
  it("applies and records a position named by the URL", async () => {
    history.replaceState(null, "", "?view=tree&page=4");
    useHistory.getState().restoreFromUrl();
    await Promise.resolve();
    expect(useViz.getState().view).toBe("tree");
    expect(useTree.getState().selectedPage).toBe(4);
    expect(useHistory.getState().entries).toHaveLength(1);
  });

  it("seeds the current position on a fresh start (Back has a target)", () => {
    useHistory.getState().restoreFromUrl(); // no URL params, empty stack
    expect(useHistory.getState().entries).toHaveLength(1);
    expect(useHistory.getState().entries[0].view).toBe("pages");
  });
});
