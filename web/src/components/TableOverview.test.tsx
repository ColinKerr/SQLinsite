import { afterEach, describe, expect, it } from "vitest";
import { render, screen, within } from "@testing-library/react";
import { TableOverview } from "./TableOverview.tsx";
import { useTree } from "../state/treeStore.ts";

afterEach(() => {
  useTree.setState({ overview: null, contentLoading: false });
});

describe("TableOverview", () => {
  it("renders a table's stats, SQL and index list", () => {
    useTree.setState({
      overview: {
        objectId: 1, type: "table", name: "T",
        sql: "CREATE TABLE T(id INTEGER PRIMARY KEY, n INTEGER)",
        pageCount: 4, rootPage: 2, rowCount: 42,
        indexes: [{ name: "T_n", pageCount: 2, rootPage: 3 }],
      },
      contentLoading: false,
    });
    render(<TableOverview />);

    // Row count is shown in the "rows N" field.
    expect(screen.getByText("rows").parentElement!.textContent).toContain("42");
    expect(screen.getByText(/CREATE TABLE T/)).toBeInTheDocument();           // SQL
    // The index appears with its root-page control (p3).
    const idxRow = screen.getByText("T_n").closest("tr")!;
    expect(within(idxRow).getByRole("button", { name: /p3/ })).toBeInTheDocument();
  });

  it("shows a placeholder while the overview is loading", () => {
    useTree.setState({ overview: null, contentLoading: true });
    render(<TableOverview />);
    expect(screen.getByText(/Loading overview/)).toBeInTheDocument();
  });
});
