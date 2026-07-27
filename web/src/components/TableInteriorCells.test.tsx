import { describe, expect, it, vi } from "vitest";
import { render, screen, fireEvent, within } from "@testing-library/react";
import { TableInteriorCells } from "./TableInteriorCells.tsx";
import type { PageContent } from "../core/types.ts";

const content: PageContent = {
  pageNumber: 2,
  pageType: "table-interior",
  pageSize: 4096,
  usableSize: 4096,
  header: { type: 5, typeName: "table interior", rightmostPointer: 87 },
  regions: [
    { offset: 0, length: 12, kind: "page-header" },
    { offset: 12, length: 4, kind: "cellptr-array" },
    { offset: 20, length: 5, kind: "cell", cellIndex: 0 },
    { offset: 25, length: 6, kind: "cell", cellIndex: 1 },
  ],
  cells: [
    // rowids 1..60 with 8 and 15 deleted → runs [1–7],[9–14],[16–60], count 58.
    { cellIndex: 0, offset: 20, size: 5, leftChild: 3, rowid: 60,
      rowCount: 58, rowRuns: [{"startRowId": 1, "endRowId": 7, "rowCount": 7},
        {"startRowId": 9, "endRowId": 14, "rowCount": 6},
        {"startRowId": 16, "endRowId": 60, "rowCount": 45}] },
    { cellIndex: 1, offset: 25, size: 6, leftChild: 4, rowid: 120,
      rowCount: 60, rowRuns: [{"startRowId": 61, "endRowId": 120, "rowCount": 60}] },
  ],
  pointers: [
    { toPage: 3, kind: "child", pageType: "table-leaf" },
    { toPage: 4, kind: "child", pageType: "table-leaf" },
    { toPage: 87, kind: "child", pageType: "table-leaf" },
  ],
  rightmostRowRuns: { rowCount: 40, rowRuns: [{"startRowId": 121, "endRowId": 160, "rowCount": 40}] },
};

function renderControl(setHover = vi.fn()) {
  const rowRefs = { current: [] as (HTMLElement | null)[] };
  render(
    <TableInteriorCells content={content} hover={null} setHover={setHover}
                        rowRefs={rowRefs} onNav={vi.fn()} pointerType={() => "table-leaf"} />,
  );
  return { rowRefs, setHover };
}

describe("Table Interior Cell control", () => {
  it("renders the five columns, a row per divider cell, and a rightmost row", () => {
    renderControl();
    for (const h of ["Cell (record)", "Row Count", "Bytes", "Page", "Row Ids"])
      expect(screen.getByRole("columnheader", { name: h })).toBeInTheDocument();

    const rows = screen.getAllByRole("row");
    expect(rows).toHaveLength(4); // header + 2 cells + rightmost

    const cell0 = rows[1];
    // Non-contiguous rowids render as a mix of runs and single ids; count is exact.
    expect(within(cell0).getByText("1–7, 9–14, 16–60")).toBeInTheDocument();
    expect(within(cell0).getByText("58")).toBeInTheDocument();
    expect(within(cell0).getByText("20–25 (5B)")).toBeInTheDocument();
    expect(within(cell0).getByRole("button", { name: /p3/ })).toBeInTheDocument();

    const last = rows[3];
    expect(within(last).getByText("rightmost")).toBeInTheDocument();
    expect(within(last).getByText("121–160")).toBeInTheDocument();
    expect(within(last).getByText("40")).toBeInTheDocument();
    expect(within(last).getByRole("button", { name: /p87/ })).toBeInTheDocument();
  });

  it("links a row to its schematic region on hover", () => {
    const { setHover } = renderControl();
    const cell1 = screen.getAllByRole("row")[2];
    fireEvent.mouseEnter(cell1);
    expect(setHover).toHaveBeenCalledWith(3); // cell 1's region index in `regions`
  });
});
