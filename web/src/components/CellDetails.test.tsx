import { beforeEach, describe, expect, it, vi } from "vitest";
import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { SchemaPanel } from "./SchemaPanel.tsx";
import { useQuery } from "../state/queryStore.ts";
import { useViz } from "../state/store.ts";
import { useTree } from "../state/treeStore.ts";

function jsonResp(body: unknown) {
  return { ok: true, json: async () => body } as Response;
}

beforeEach(() => {
  useQuery.setState({ schema: { tables: [], views: [] }, selectedCell: null });
  useViz.setState({ view: "query" });
  useTree.setState({ selectedPage: null, roots: null, expanded: new Set(), childrenByKey: {} });
  vi.stubGlobal("fetch", vi.fn(async (url: string) => {
    const u = String(url);
    if (u === "/api/schema") return jsonResp({ tables: [], views: [] });
    if (u.startsWith("/api/page/")) {
      return jsonResp({ pageNumber: 3, pageType: "overflow", objectId: 1, cellCount: 0,
                        freeBytes: 0 });
    }
    return jsonResp({});
  }));
});

describe("cell details panel", () => {
  it("is hidden until a cell is selected", () => {
    render(<SchemaPanel />);
    expect(screen.queryByText(/Go to page/)).toBeNull();
  });

  it("shows the clicked cell's value + page links, and jumps on click", async () => {
    render(<SchemaPanel />);
    useQuery.setState({
      selectedCell: {
        rowIndex: 0, colIndex: 1, value: "xxxxxxxx", pages: [2, 3],
        column: { name: "big", sourceTable: "T", sourceColumn: "big" },
      },
    });

    expect(await screen.findByText("big")).toBeInTheDocument();
    expect(screen.getByText("T.big")).toBeInTheDocument();
    expect(screen.getByText("xxxxxxxx")).toBeInTheDocument();

    const link3 = screen.getByRole("button", { name: "Go to page 3" });
    expect(screen.getByRole("button", { name: "Go to page 2" })).toBeInTheDocument();

    // Lazy page detail renders after fetch resolves (one line per page).
    await waitFor(() => expect(screen.getAllByText(/overflow/).length).toBe(2));

    fireEvent.click(link3);
    expect(useViz.getState().view).toBe("tree");
    expect(useTree.getState().selectedPage).toBe(3);
  });

  it("closes when the ✕ button is clicked", async () => {
    render(<SchemaPanel />);
    useQuery.setState({
      selectedCell: {
        rowIndex: 0, colIndex: 0, value: 1, pages: [2],
        column: { name: "id", sourceTable: "T", sourceColumn: "id" },
      },
    });
    fireEvent.click(await screen.findByTitle("Close"));
    expect(useQuery.getState().selectedCell).toBeNull();
  });
});
