import { describe, expect, it, vi } from "vitest";
import { makeCanvasActivate, makeQueryActivate, makeTreeActivate } from "./nodeActivation.ts";
import type { TreeNode } from "../core/treeModel.ts";
import type { ObjectInfo } from "../core/types.ts";

const obj = (o: Partial<ObjectInfo>): ObjectInfo =>
  ({ id: 0, type: "table", name: "", tableName: null, rootPage: 0, pageCount: 0,
     startPage: 0, startLeafPage: 0, ...o });
const node = (n: Partial<TreeNode>): TreeNode =>
  ({ key: "k", kind: "page", label: "", page: null, pageType: null, hasChildren: false, ...n });

describe("makeQueryActivate", () => {
  const objById = new Map<number, ObjectInfo>([
    [1, obj({ id: 1, type: "table", name: "T" })],
    [2, obj({ id: 2, type: "index", name: "T_n", tableName: "T" })],
  ]);
  const mk = () => {
    const runObjectQuery = vi.fn(), runPageQuery = vi.fn();
    return { runObjectQuery, runPageQuery,
             activate: makeQueryActivate({ objById, runObjectQuery, runPageQuery }) };
  };

  it("runs the whole table for a table grouping node", () => {
    const { runObjectQuery, runPageQuery, activate } = mk();
    activate(node({ kind: "table", objectId: 1 }));
    expect(runObjectQuery).toHaveBeenCalledWith("T");
    expect(runPageQuery).not.toHaveBeenCalled();
  });

  it("runs a table leaf page's rows (not overflow)", () => {
    const { runPageQuery, activate } = mk();
    activate(node({ kind: "page", page: 5, objectId: 1, pageType: "table-leaf" }));
    expect(runPageQuery).toHaveBeenCalledWith(5, false);
  });

  it("runs a table interior page's subtree rows", () => {
    const { runPageQuery, activate } = mk();
    activate(node({ kind: "page", page: 2, objectId: 1, pageType: "table-interior" }));
    expect(runPageQuery).toHaveBeenCalledWith(2, false);
  });

  it("runs the owning leaf's rows and flags overflow for an overflow page", () => {
    const { runPageQuery, activate } = mk();
    activate(node({ kind: "page", page: 9, objectId: 1, pageType: "overflow" }));
    expect(runPageQuery).toHaveBeenCalledWith(9, true);
  });

  it("does nothing for an index page node", () => {
    const { runObjectQuery, runPageQuery, activate } = mk();
    activate(node({ kind: "page", page: 8, objectId: 2, pageType: "index-leaf" }));
    expect(runObjectQuery).not.toHaveBeenCalled();
    expect(runPageQuery).not.toHaveBeenCalled();
  });

  it("does nothing for the Indexes grouping node", () => {
    const { runObjectQuery, runPageQuery, activate } = mk();
    activate(node({ kind: "indexes", objectId: 1 }));
    expect(runObjectQuery).not.toHaveBeenCalled();
    expect(runPageQuery).not.toHaveBeenCalled();
  });

  it("does nothing for a node with no object", () => {
    const { runObjectQuery, runPageQuery, activate } = mk();
    activate(node({ kind: "page", objectId: null, page: 1, pageType: "table-leaf" }));
    expect(runObjectQuery).not.toHaveBeenCalled();
    expect(runPageQuery).not.toHaveBeenCalled();
  });
});

describe("makeCanvasActivate", () => {
  it("a page node selects that page's block in the current view, passing its object + type", () => {
    const selectPage = vi.fn(), setObj = vi.fn();
    makeCanvasActivate(selectPage, setObj)(node({ kind: "page", page: 5, objectId: 1, pageType: "table-leaf" }));
    expect(selectPage).toHaveBeenCalledWith(5, 1, "table-leaf");   // objectId/type locate the band
    expect(setObj).not.toHaveBeenCalled();
  });

  it("a structural page node (no objectId) passes its page type so Tables finds the group band", () => {
    const selectPage = vi.fn(), setObj = vi.fn();
    makeCanvasActivate(selectPage, setObj)(node({ kind: "page", page: 2, objectId: null, pageType: "pointer-map" }));
    expect(selectPage).toHaveBeenCalledWith(2, null, "pointer-map");
    expect(setObj).not.toHaveBeenCalled();
  });

  it("an index b-tree page node still scrolls to its page (only index *grouping* is inert)", () => {
    const selectPage = vi.fn(), setObj = vi.fn();
    makeCanvasActivate(selectPage, setObj)(node({ kind: "page", page: 8, objectId: 2, pageType: "index-leaf" }));
    expect(selectPage).toHaveBeenCalledWith(8, 2, "index-leaf");
    expect(setObj).not.toHaveBeenCalled();
  });

  it("a table grouping node selects the object (first leaf in Pages, band in Tables)", () => {
    const selectPage = vi.fn(), setObj = vi.fn();
    makeCanvasActivate(selectPage, setObj)(node({ kind: "table", page: null, objectId: 1 }));
    expect(setObj).toHaveBeenCalledWith(1);
    expect(selectPage).not.toHaveBeenCalled();
  });

  it("the Indexes grouping node does nothing", () => {
    const selectPage = vi.fn(), setObj = vi.fn();
    makeCanvasActivate(selectPage, setObj)(node({ kind: "indexes", page: null, objectId: 1 }));
    expect(selectPage).not.toHaveBeenCalled();
    expect(setObj).not.toHaveBeenCalled();
  });
});

describe("makeTreeActivate", () => {
  it("dispatches table / indexes / page nodes to their loaders", () => {
    const selectTable = vi.fn(), selectIndexes = vi.fn(), selectPage = vi.fn();
    const activate = makeTreeActivate({ selectTable, selectIndexes, selectPage });
    activate(node({ kind: "table", objectId: 1, key: "t:1" }));
    activate(node({ kind: "indexes", objectId: 1, key: "t:1>indexes" }));
    activate(node({ kind: "page", page: 5 }));
    expect(selectTable).toHaveBeenCalledWith(1, "t:1");
    expect(selectIndexes).toHaveBeenCalledWith(1, "t:1>indexes");
    expect(selectPage).toHaveBeenCalledWith(5);
  });
});
