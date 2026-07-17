import { beforeEach, describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { BTreeTree } from "./BTreeTree.tsx";
import { useTree } from "../state/treeStore.ts";
import { useViz } from "../state/store.ts";
import { useQuery } from "../state/queryStore.ts";

function jsonResp(body: unknown) {
  return { ok: true, json: async () => body } as Response;
}

// jsdom lacks ResizeObserver, which BTreeTree observes on mount.
class RO { observe() {} unobserve() {} disconnect() {} }

beforeEach(() => {
  vi.stubGlobal("ResizeObserver", RO);
  // The Page Tree view drives the node-click → detail behavior these tests cover.
  useViz.setState({ view: "tree" });
  useTree.setState({
    roots: null, childrenByKey: {}, expanded: new Set(), loading: new Set(),
    selectedPage: null, selectedKey: null, content: null, overview: null, contentLoading: false,
  });
  vi.stubGlobal("fetch", vi.fn(async (url: string) => {
    const u = String(url);
    if (u === "/api/tree/roots") {
      return jsonResp({ roots: [
        { kind: "page", label: "sqlite_schema", page: 1, pageType: "table-leaf", objectId: null, hasChildren: 0 },
        { kind: "table", label: "Widget", page: null, pageType: null, objectId: 1, hasChildren: true,
          tableBtree: { kind: "page", label: "Widget (table)", page: 2, pageType: "table-interior", objectId: 1, hasChildren: 1 },
          indexes: [] },
      ] });
    }
    if (u.startsWith("/api/tree/children?page=2")) {
      return jsonResp({ children: [{ page: 5, kind: "child", pageType: "table-leaf", objectId: 1, hasChildren: 0 }] });
    }
    if (u.startsWith("/api/tree/object?id=1")) {
      return jsonResp({ overview: { objectId: 1, type: "table", name: "Widget", sql: "", pageCount: 1, rootPage: 2, rowCount: 0, indexes: [] } });
    }
    if (u.startsWith("/api/page/1/content")) return jsonResp({ pageNumber: 1, regions: [] });
    return jsonResp({});
  }));
});

// The "Widget" table node's row (its label is exactly "Widget").
const widgetRow = () => screen.getByText("Widget").closest(".tn-row") as HTMLElement;

describe("BTreeTree click behavior", () => {
  it("clicking a collapsed node's body expands and selects it", async () => {
    await useTree.getState().loadRoots();
    render(<BTreeTree />);

    fireEvent.click(screen.getByText("Widget"));            // body click on collapsed node
    // Expands: its b-tree child appears.
    expect(await screen.findByText("Widget (table)")).toBeInTheDocument();
    expect(useTree.getState().expanded.has("t:1")).toBe(true);
    // And selects it (grouping node → overview).
    await waitFor(() => expect(useTree.getState().selectedKey).toBe("t:1"));
  });

  it("the chevron toggles expand/collapse without changing selection", async () => {
    await useTree.getState().loadRoots();
    render(<BTreeTree />);

    fireEvent.click(screen.getByText("Widget"));            // expand + select Widget
    await screen.findByText("Widget (table)");
    fireEvent.click(screen.getByText("sqlite_schema"));     // select page 1 instead
    await waitFor(() => expect(useTree.getState().selectedPage).toBe(1));

    // Collapse via the chevron: selection must NOT move to Widget.
    fireEvent.click(widgetRow().querySelector(".tn-arrow") as HTMLElement);
    expect(useTree.getState().expanded.has("t:1")).toBe(false);   // collapsed
    expect(useTree.getState().selectedPage).toBe(1);              // selection unchanged
    expect(useTree.getState().selectedKey).toBeNull();
  });

  it("clicking an expanded node's body selects it without collapsing", async () => {
    await useTree.getState().loadRoots();
    render(<BTreeTree />);

    fireEvent.click(screen.getByText("Widget"));            // expand + select
    await screen.findByText("Widget (table)");
    fireEvent.click(screen.getByText("sqlite_schema"));     // move selection away
    await waitFor(() => expect(useTree.getState().selectedPage).toBe(1));

    fireEvent.click(screen.getByText("Widget"));            // body click while expanded
    await waitFor(() => expect(useTree.getState().selectedKey).toBe("t:1")); // re-selected
    expect(useTree.getState().expanded.has("t:1")).toBe(true);               // still expanded
  });
});

describe("BTreeTree per-view node clicks", () => {
  it("Pages view: clicking a table node scrolls to its object", async () => {
    useViz.setState({ view: "pages", selectedObject: null,
      objById: new Map([[1, { id: 1, type: "table", name: "Widget" }]]) as never });
    await useTree.getState().loadRoots();
    render(<BTreeTree />);

    fireEvent.click(screen.getByText("Widget"));
    // The canvas controller subscribes to selectedObject and scrolls to it.
    expect(useViz.getState().selectedObject).toBe(1);
    expect(useTree.getState().selectedKey).toBe("t:1"); // shared highlight still updates
  });

  it("Query view: clicking a table node runs SELECT * FROM the table", async () => {
    const runSql = vi.fn();
    useViz.setState({ view: "query",
      objById: new Map([[1, { id: 1, type: "table", name: "Widget" }]]) as never });
    useQuery.setState({ runSql: runSql as unknown as (sql: string) => Promise<void> });
    await useTree.getState().loadRoots();
    render(<BTreeTree />);

    fireEvent.click(screen.getByText("Widget"));
    expect(runSql).toHaveBeenCalledWith('SELECT * FROM "Widget";');
  });
});
