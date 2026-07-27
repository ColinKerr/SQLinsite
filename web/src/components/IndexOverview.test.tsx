import { afterEach, describe, expect, it } from "vitest";
import { render, screen, within } from "@testing-library/react";
import { IndexOverview } from "./IndexOverview.tsx";
import { useTree } from "../state/treeStore.ts";
import { useViz } from "../state/store.ts";
import type { Meta } from "../core/types.ts";

afterEach(() => {
  useTree.setState({ overview: null, overviewMode: null, contentLoading: false });
});

describe("IndexOverview", () => {
  it("lists each index with size and its CREATE INDEX statement", () => {
    useViz.setState({ meta: { meta: { pageSize: 4096, pageCount: 10 } } as Meta });
    useTree.setState({
      overviewMode: "index",
      contentLoading: false,
      overview: {
        objectId: 1, type: "table", name: "T", sql: null, pageCount: 3, rootPage: 2, rowCount: 5,
        indexes: [{ name: "T_n", pageCount: 2, rootPage: 3, sql: "CREATE INDEX T_n ON T(n)" }],
      },
    });
    render(<IndexOverview />);

    expect(screen.getByRole("columnheader", { name: "statement" })).toBeInTheDocument();
    const row = screen.getByText("T_n").closest("tr")!;
    expect(within(row).getByText("8 KB")).toBeInTheDocument();               // 2 pages × 4096
    expect(within(row).getByText(/CREATE INDEX T_n/)).toBeInTheDocument();   // the statement
    expect(within(row).getByRole("button", { name: /p3/ })).toBeInTheDocument(); // root page card
  });
});
