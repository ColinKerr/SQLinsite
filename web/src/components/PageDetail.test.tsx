import { afterEach, describe, expect, it } from "vitest";
import { render, screen } from "@testing-library/react";
import { PageDetail } from "./PageDetail.tsx";
import { useTree } from "../state/treeStore.ts";
import type { PageContent } from "../core/types.ts";

// Overflow page as the server returns it: the payload region is annotated with
// the owning cell's index, and `cells` holds that cell reduced to the slice that
// physically lives on this overflow page (segments stripped, value = the slice).
const overflowContent: PageContent = {
  pageNumber: 3,
  pageType: "overflow",
  pageSize: 4096,
  usableSize: 4096,
  ownerPage: 2,
  header: { nextPage: 0 },
  regions: [
    { offset: 0, length: 4, kind: "overflow-header" },
    { offset: 4, length: 4092, kind: "payload", cellIndex: 0 },
  ],
  cells: [
    {
      cellIndex: 0,
      offset: 2177,
      size: 1919,
      rowid: 1,
      columns: [
        {
          serialType: 12013,
          serialName: "text(6000)",
          type: "text",
          value: "OVERFLOW_SLICE_MARKER",
          bytes: 21,
          truncated: false,
        },
      ],
    },
  ],
  pointers: [],
};

afterEach(() => {
  useTree.setState({ selectedPage: null, content: null, contentLoading: false });
});

describe("PageDetail — overflow pages", () => {
  it("renders the owning cell's slice for an overflow page's payload region", () => {
    useTree.setState({ selectedPage: 3, content: overflowContent, contentLoading: false });
    render(<PageDetail />);

    // Regression: the owning cell's decoded value must appear. Previously the
    // "Full Page Contents" loop only rendered `kind === "cell"` regions, so an
    // overflow page (whose region is `kind === "payload"`) showed no cell data.
    expect(screen.getByText(/OVERFLOW_SLICE_MARKER/)).toBeInTheDocument();
    // It's shown as the owning cell (rowid 1), owned by page 2.
    expect(screen.getByText(/rowid/)).toBeInTheDocument();
    expect(screen.getByText(/owned by/)).toBeInTheDocument();
  });
});

describe("PageDetail — header", () => {
  const tableLeaf: PageContent = {
    pageNumber: 6, pageType: "table-leaf", pageSize: 4096, usableSize: 4096,
    header: { type: 13, typeName: "table leaf", cellCount: 272 },
    object: { name: "T", type: "table" },
    rowCount: 272,
    regions: [{ offset: 0, length: 8, kind: "page-header" }],
    cells: [], pointers: [],
  };

  it("shows the owning b-tree and the row count for a table page", () => {
    useTree.setState({ selectedPage: 6, content: tableLeaf, contentLoading: false });
    render(<PageDetail />);
    // Line 1: which table/index b-tree the page is part of.
    expect(screen.getByText(/part of/)).toBeInTheDocument();
    expect(screen.getByText("T")).toBeInTheDocument();
    expect(screen.getByText(/\(table\)/)).toBeInTheDocument();
    // Line 2: the row count (its own line; 272 also appears as cellCount below).
    expect(document.querySelector(".pd-rowcount")?.textContent).toMatch(/272\s*rows/);
  });

  it("omits the row count for a page with no rowCount (e.g. an index page)", () => {
    useTree.setState({
      selectedPage: 8, contentLoading: false,
      content: { ...tableLeaf, pageNumber: 8, pageType: "index-leaf",
        object: { name: "T_s", type: "index" }, rowCount: undefined },
    });
    render(<PageDetail />);
    expect(screen.getByText(/\(index\)/)).toBeInTheDocument();
    expect(document.querySelector(".pd-rowcount")).toBeNull();
  });
});
