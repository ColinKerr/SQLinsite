import { useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import {
  type ColumnDef, type ColumnSizingState, getCoreRowModel, useReactTable,
} from "@tanstack/react-table";
import { useVirtualizer } from "@tanstack/react-virtual";
import { colorForPageNumber } from "../core/palette.ts";
import { fitColumnWidths, formatCellText } from "../core/columnFit.ts";
import { formatCount } from "../core/format.ts";
import type { QueryColumn } from "../core/types.ts";

const ROW_H = 24;
const HEADER_H = 26;
const IDX_W = 48;
const MEASURE_FONT = "12px -apple-system, system-ui, sans-serif";
const EMPTY: never[] = [];

interface HoverCell { c: number; pages: number[]; x: number; y: number; }

// A clicked cell reported to the caller (drives the Query view's Cell Details panel).
export interface GridCell {
  rowIndex: number;
  colIndex: number;
  column: GridColumn;
  value: unknown;
  pages: number[];
}
// Columns need a name; provenance is optional (analysis columns have none).
export type GridColumn = { name: string; sourceTable?: string | null; sourceColumn?: string | null };

export interface ResultsGridProps {
  columns: GridColumn[];
  rows: unknown[][] | null;        // null → not run yet (show emptyMessage)
  rowCount: number;                // total rows (for footer + lazy-load stop)
  error?: string | null;
  runKey: number | string | null;  // identity that reseeds column widths on a new result
  emptyMessage?: string;
  footer?: ReactNode;              // defaults to "showing N of M rows"
  // Query-view-only features; omitted in the Analysis view (no row→page mapping).
  rowPages?: number[][][];
  selectedCell?: { rowIndex: number; colIndex: number } | null;
  onSelectCell?: (cell: GridCell) => void;
  highlightPage?: number | null;
  onLoadMore?: () => void;
}

// Results grid, virtualized on BOTH axes (rows and columns) so it scales to
// millions of rows × thousands of columns. TanStack Table owns the column model
// (sizing, resize, the pinned `#` index column); TanStack Virtual windows the rows
// and the non-pinned "center" columns — only visible rows × visible columns mount.
// Page-provenance features (cell tint, hover tooltip, click-to-details, lazy load)
// activate only when the corresponding props are supplied.
export function ResultsGrid({
  columns, rows, rowCount, error, runKey, emptyMessage = "Run a query to see results.",
  footer, rowPages, selectedCell, onSelectCell, highlightPage = null, onLoadMore,
}: ResultsGridProps) {
  const bodyRef = useRef<HTMLDivElement>(null);
  const [hover, setHover] = useState<HoverCell | null>(null);
  const [columnSizing, setColumnSizing] = useState<ColumnSizingState>({});

  const dataRows = rows ?? (EMPTY as unknown[][]);
  const queryCols = columns ?? EMPTY;

  // A shared canvas 2D context for measuring header/cell text (auto-fit widths).
  const measure = useMemo(() => {
    const ctx = document.createElement("canvas").getContext("2d");
    if (ctx) ctx.font = MEASURE_FONT;
    return (text: string) => (ctx ? ctx.measureText(text).width : text.length * 7);
  }, []);

  // A pinned `#` column plus one column per result column (id = data index).
  const tableColumns = useMemo<ColumnDef<unknown[]>[]>(() => [
    { id: "#", header: "#", enableResizing: false },
    ...queryCols.map((c, di) => ({ id: String(di), header: c.name })),
  ], [runKey]); // eslint-disable-line react-hooks/exhaustive-deps

  // Auto-fit column widths from the loaded rows, re-seeded on each run.
  useEffect(() => {
    if (!rows) return;
    const w = fitColumnWidths(queryCols as QueryColumn[], rows, measure);
    const sizing: ColumnSizingState = { "#": IDX_W };
    queryCols.forEach((_, di) => { sizing[String(di)] = w[di]; });
    setColumnSizing(sizing);
    setHover(null);
  }, [runKey]); // eslint-disable-line react-hooks/exhaustive-deps

  const table = useReactTable({
    data: dataRows,
    columns: tableColumns,
    state: { columnSizing, columnPinning: { left: ["#"] } },
    onColumnSizingChange: setColumnSizing,
    columnResizeMode: "onChange",
    enableColumnResizing: true,
    manualSorting: true,      // groundwork: sorting/filtering will run server-side
    manualFiltering: true,
    getCoreRowModel: getCoreRowModel(),
  });
  const centerCols = table.getCenterVisibleLeafColumns();
  const headerById = useMemo(
    () => new Map(table.getFlatHeaders().map((h) => [h.column.id, h])),
    [table, tableColumns, columnSizing],
  );

  const rowVirtualizer = useVirtualizer({
    count: dataRows.length,
    getScrollElement: () => bodyRef.current,
    estimateSize: () => ROW_H,
    overscan: 10,
  });
  const colVirtualizer = useVirtualizer({
    horizontal: true,
    count: centerCols.length,
    getScrollElement: () => bodyRef.current,
    estimateSize: (i) => centerCols[i].getSize(),
    overscan: 3,
  });
  // Re-measure the (variable-width) columns when widths change or the run changes.
  useEffect(() => { colVirtualizer.measure(); }, [columnSizing, runKey]); // eslint-disable-line react-hooks/exhaustive-deps

  if (error) return <div className="results-msg error">{error}</div>;
  if (!rows) return <div className="results-msg muted">{emptyMessage}</div>;

  const vCols = colVirtualizer.getVirtualItems();
  const vRows = rowVirtualizer.getVirtualItems();
  const totalW = IDX_W + colVirtualizer.getTotalSize();
  const totalH = HEADER_H + rowVirtualizer.getTotalSize();

  return (
    <div className="results-table">
      <div className="rt-body" ref={bodyRef}
           onMouseLeave={() => setHover(null)}
           onScroll={(e) => {
             if (!onLoadMore) return;
             const el = e.currentTarget;
             // Lazy-load the next window when within ~15 rows of the loaded end.
             if (el.scrollTop + el.clientHeight > el.scrollHeight - 15 * ROW_H) void onLoadMore();
           }}>
        <div style={{ position: "relative", width: totalW, height: totalH }}>
          {/* Header: sticky to the top; the `#` cell sticky to the left. */}
          <div className="rt-header-row" style={{ width: totalW, height: HEADER_H }}>
            <div className="rt-cell rt-th rt-idx rt-pin rt-pin-head" style={{ width: IDX_W, height: HEADER_H }}>#</div>
            {vCols.map((vc) => {
              const col = centerCols[vc.index];
              const qc = queryCols[Number(col.id)];
              const resize = headerById.get(col.id)?.getResizeHandler();
              return (
                <div className="rt-cell rt-th" key={col.id}
                     style={{ position: "absolute", left: IDX_W + vc.start, width: vc.size, height: HEADER_H, top: 0 }}
                     title={qc?.sourceTable ? `${qc.sourceTable}.${qc.sourceColumn}` : "expression / computed"}>
                  <span className="rt-th-name">{qc?.name}</span>
                  <div className="rt-resizer" onMouseDown={resize} onTouchStart={resize} />
                </div>
              );
            })}
          </div>
          {/* Rows: absolutely positioned by the row virtualizer. */}
          {vRows.map((vr) => {
            const ri = vr.index;
            return (
              <div className={"rt-row" + (ri % 2 ? " rt-even" : "")} key={ri}
                   style={{ position: "absolute", top: HEADER_H + vr.start, height: ROW_H, width: totalW }}>
                <div className="rt-cell rt-idx rt-pin" style={{ width: IDX_W, height: ROW_H }}>{ri + 1}</div>
                {vCols.map((vc) => {
                  const col = centerCols[vc.index];
                  const di = Number(col.id);
                  const value = dataRows[ri][di];
                  const pages = rowPages?.[ri]?.[di] ?? [];
                  const selected = selectedCell?.rowIndex === ri && selectedCell?.colIndex === di;
                  // Emphasize cells whose bytes (partly) live on the highlighted
                  // (overflow) page, so the overflow node's contribution stands out.
                  const highlit = highlightPage != null && pages.includes(highlightPage);
                  return (
                    <div className={"rt-cell" + (selected ? " rt-sel" : "") + (highlit ? " rt-hl" : "")} key={col.id}
                         style={{ position: "absolute", left: IDX_W + vc.start, width: vc.size, height: ROW_H }}
                         onClick={onSelectCell
                           ? () => onSelectCell({ rowIndex: ri, colIndex: di, column: queryCols[di], value, pages })
                           : undefined}
                         onMouseMove={(e) => {
                           if (pages.length === 0) { setHover(null); return; }
                           setHover({ c: di, pages, x: e.clientX, y: e.clientY });
                         }}>
                      {pages.length > 0 && (
                        <div className="rt-segs">
                          {pages.map((p, k) => (
                            <div className={"rt-seg" + (p === highlightPage ? " rt-seg-hl" : "")}
                                 key={k} style={{ background: colorForPageNumber(p) }} />
                          ))}
                        </div>
                      )}
                      <span className="rt-val">{formatCellText(value)}</span>
                    </div>
                  );
                })}
              </div>
            );
          })}
        </div>
      </div>
      {hover && <HoverTip hover={hover} column={queryCols[hover.c]} />}
      {footer ?? (
        <div className="rt-foot muted">
          showing {formatCount(dataRows.length)} of {formatCount(rowCount)} rows
        </div>
      )}
    </div>
  );
}

// Lightweight, non-interactive tooltip that follows the cursor (offset to the
// lower-right). Click the cell for the full details panel and page links.
function HoverTip({ hover, column }: { hover: HoverCell; column?: GridColumn }) {
  const provenance = column?.sourceTable
    ? `${column.sourceTable}.${column.sourceColumn}`
    : "expression / computed";
  const left = Math.min(hover.x + 14, window.innerWidth - 200);
  const top = Math.min(hover.y + 16, window.innerHeight - 60);
  return (
    <div className="rt-tip" style={{ left, top }}>
      <div className="rt-tip-head">{provenance}</div>
      <div className="rt-tip-pages">
        {hover.pages.map((p) => (
          <span className="rt-tip-page" key={p}>
            <span className="rt-tip-swatch" style={{ background: colorForPageNumber(p) }} />
            page {p}
          </span>
        ))}
      </div>
    </div>
  );
}
