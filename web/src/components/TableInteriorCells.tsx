import { useMemo, type RefObject } from "react";
import { type ColumnDef, flexRender, getCoreRowModel, useReactTable } from "@tanstack/react-table";
import type { PageContent } from "../core/types.ts";
import { PageCard } from "./PageDetail.tsx";

// One control row: a divider cell, or the synthetic final "rightmost" pointer row.
interface IntRow {
  kind: "cell" | "rightmost";
  label: string;                    // cell number, or "rightmost"
  count?: number;
  ranges?: [number, number][];
  bytesText: string;
  page?: number;                    // left-child (or rightmost-pointer) page
  regionIndex: number;              // schematic region this row links to (-1 if none)
}

// Per-column <td> class (the header/value markup itself comes from the column defs).
const TD_CLASS: Record<string, string> = { cell: "muted", bytes: "muted", rowids: "int-rowids" };

const bytes = (offset: number, len: number) => `${offset}–${offset + len} (${len}B)`;
// Rowids aren't contiguous (deletions leave gaps): render the runs as a mix of
// ranges "s–e" and single ids "s", e.g. "1–4, 6, 11–60".
const rowRanges = (ranges?: [number, number][]) =>
  ranges && ranges.length ? ranges.map(([s, e]) => (s === e ? `${s}` : `${s}–${e}`)).join(", ") : "—";

// The "Table Interior Cell control": table-interior pages hold no record data —
// each divider cell is just a rowid + a child pointer — so they render as a table
// of Cell (record) / Row Count / Bytes / Page / Row Ids rows (plus a final row for
// the header's rightmost pointer). Headless TanStack Table drives the column model;
// rows link to the Horizontal Schematic (hover highlights the matching cell block,
// and a schematic click highlights/scrolls to the row).
export function TableInteriorCells({ content, hover, setHover, rowRefs, onNav, pointerType }: {
  content: PageContent;
  hover: number | null;
  setHover: (i: number | null) => void;
  rowRefs: RefObject<(HTMLElement | null)[]>;
  onNav: (p: number) => void;
  pointerType: (toPage: number) => string | undefined;
}) {
  const data = useMemo<IntRow[]>(() => {
    const regionOf = (cellIndex: number) =>
      content.regions.findIndex((r) => r.cellIndex === cellIndex);
    const rows: IntRow[] = content.cells.map((c) => ({
      kind: "cell",
      label: String(c.cellIndex),
      count: c.rowidCount,
      ranges: c.rowidRanges,
      bytesText: bytes(c.offset, c.size),
      page: c.leftChild,
      regionIndex: regionOf(c.cellIndex),
    }));
    const rightmost = content.header["rightmostPointer"] as number | undefined;
    if (rightmost != null && rightmost > 0) {
      const ph = content.regions.find((r) => r.kind === "page-header");
      rows.push({
        kind: "rightmost",
        label: "rightmost",
        count: content.rightmostRowids?.count,
        ranges: content.rightmostRowids?.ranges,
        bytesText: ph ? bytes(ph.offset + ph.length - 4, 4) : "—",
        page: rightmost,
        regionIndex: content.regions.findIndex((r) => r.kind === "page-header"),
      });
    }
    return rows;
  }, [content]);

  const columns = useMemo<ColumnDef<IntRow>[]>(() => [
    { id: "cell", header: "Cell (record)", cell: ({ row }) => row.original.label },
    { id: "count", header: "Row Count", cell: ({ row }) => row.original.count ?? "—" },
    { id: "bytes", header: "Bytes", cell: ({ row }) => row.original.bytesText },
    {
      id: "page", header: "Page",
      cell: ({ row }) => row.original.page != null
        ? <PageCard page={row.original.page} pageType={pointerType(row.original.page)} onClick={onNav} />
        : null,
    },
    { id: "rowids", header: "Row Ids", cell: ({ row }) => rowRanges(row.original.ranges) },
  ], [onNav, pointerType]);

  const table = useReactTable({ data, columns, getCoreRowModel: getCoreRowModel() });

  return (
    <table className="pd-intcell">
      {content.rowidCapped && (
        <caption className="int-capped">
          Row ids/counts truncated — this page's subtree is too large to enumerate fully.
        </caption>
      )}
      <thead>
        {table.getHeaderGroups().map((hg) => (
          <tr key={hg.id}>
            {hg.headers.map((h) => (
              <th key={h.id}>{flexRender(h.column.columnDef.header, h.getContext())}</th>
            ))}
          </tr>
        ))}
      </thead>
      <tbody>
        {table.getRowModel().rows.map((row) => {
          const ri = row.original.regionIndex;
          const base = row.original.kind === "rightmost" ? "int-rightmost" : "int-row";
          return (
            <tr key={row.id}
                ref={(el) => { if (ri >= 0) rowRefs.current![ri] = el; }}
                className={base + (hover === ri ? " hi" : "")}
                onMouseEnter={() => setHover(ri)} onMouseLeave={() => setHover(null)}>
              {row.getVisibleCells().map((cell) => (
                <td key={cell.id} className={TD_CLASS[cell.column.id]}>
                  {flexRender(cell.column.columnDef.cell, cell.getContext())}
                </td>
              ))}
            </tr>
          );
        })}
      </tbody>
    </table>
  );
}
