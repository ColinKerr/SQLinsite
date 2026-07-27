import { beforeEach, describe, expect, it } from "vitest";
import { render, screen } from "@testing-library/react";
import { ResultsTable } from "./ResultsTable.tsx";
import { useQuery } from "../state/queryStore.ts";
import type { QueryColumn, RowsResponse, RunSummary } from "../core/types.ts";

// jsdom has no layout (getBoundingClientRect → 0), so TanStack Virtual windows to
// zero items here — these are smoke tests of the store wiring and non-virtualized
// chrome; the grid/virtualization itself is verified end-to-end via headless Chrome.
const col = (name: string): QueryColumn => ({ name, sourceTable: null, sourceColumn: null });

beforeEach(() => {
  useQuery.setState({ rows: null, run: null, error: null, running: false, selectedCell: null });
});

describe("ResultsTable", () => {
  it("prompts to run a query when there are no results", () => {
    render(<ResultsTable />);
    expect(screen.getByText(/Run a query to see results/)).toBeInTheDocument();
  });

  it("shows an error message", () => {
    useQuery.setState({ error: "syntax error" });
    render(<ResultsTable />);
    expect(screen.getByText("syntax error")).toBeInTheDocument();
  });

  it("renders the footer counts for a completed run", () => {
    const run: RunSummary = {
      queryId: 1, columns: [col("a"), col("b")], rowCount: 3, truncated: false,
      pageCount: 2, accesses: 5, profile: { pages: [] },
    };
    const rows: RowsResponse = {
      columns: [col("a"), col("b")], rows: [[1, 2], [3, 4], [5, 6]],
      rowPages: [[[], []], [[], []], [[], []]], rowCount: 3,
    };
    useQuery.setState({ run, rows });
    const { container } = render(<ResultsTable />);
    expect(container.querySelector(".rt-foot")?.textContent).toContain("showing 3 of 3 rows");
    expect(container.querySelector(".rt-foot")?.textContent).toContain("2 pages");
  });
});
