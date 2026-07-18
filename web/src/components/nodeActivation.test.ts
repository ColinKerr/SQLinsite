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

  it("runs SELECT * FROM the table for a table node", () => {
    const runSql = vi.fn();
    makeQueryActivate(objById, runSql)(node({ kind: "table", objectId: 1 }));
    expect(runSql).toHaveBeenCalledWith('SELECT * FROM "T";');
  });

  it("maps an index node to its owning table", () => {
    const runSql = vi.fn();
    makeQueryActivate(objById, runSql)(node({ objectId: 2, page: 8 }));
    expect(runSql).toHaveBeenCalledWith('SELECT * FROM "T";');
  });

  it("does nothing for a node with no object", () => {
    const runSql = vi.fn();
    makeQueryActivate(objById, runSql)(node({ objectId: null, page: 1 }));
    expect(runSql).not.toHaveBeenCalled();
  });
});

describe("makeCanvasActivate", () => {
  it("Pages view: a page node scrolls to that page", () => {
    const goToPage = vi.fn(), setObj = vi.fn();
    makeCanvasActivate("pages", goToPage, setObj)(node({ page: 5, objectId: 1 }));
    expect(goToPage).toHaveBeenCalledWith(5);
    expect(setObj).not.toHaveBeenCalled();
  });

  it("an object/grouping node selects the object (controller scrolls to it)", () => {
    const goToPage = vi.fn(), setObj = vi.fn();
    makeCanvasActivate("pages", goToPage, setObj)(node({ kind: "table", page: null, objectId: 1 }));
    expect(setObj).toHaveBeenCalledWith(1);
  });

  it("Tables view: a page node scrolls to its object's band", () => {
    const goToPage = vi.fn(), setObj = vi.fn();
    makeCanvasActivate("tables", goToPage, setObj)(node({ page: 5, objectId: 1 }));
    expect(setObj).toHaveBeenCalledWith(1);   // band, not the individual page
    expect(goToPage).not.toHaveBeenCalled();
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
