import { useEffect, useMemo, useRef, useState } from "react";
import {
  type ColumnDef, type ColumnSizingState, getCoreRowModel, useReactTable,
} from "@tanstack/react-table";
import { useVirtualizer } from "@tanstack/react-virtual";
import { useQuery } from "../state/queryStore.ts";
import { colorForPageNumber } from "../core/palette.ts";
import { fitColumnWidths, formatCellText } from "../core/columnFit.ts";

const ROW_H = 24;
const HEADER_H = 26;
const IDX_W = 48;
const MEASURE_FONT = "12px -apple-system, system-ui, sans-serif";
const EMPTY: never[] = [];

interface HoverCell { c: number; pages: number[]; x: number; y: number; }

// Results grid, virtualized on BOTH axes (rows and columns) so it scales to
// millions of rows × thousands of columns. TanStack Table owns the column model
// (sizing, resize, the pinned `#` index column); TanStack Virtual windows the rows
// and the non-pinned "center" columns — only visible rows × visible columns mount.
// Each cell is tinted by the page(s) that store its bytes; hovering shows a
// follow-cursor tooltip; clicking opens the details panel in the schema panel.
export function ResultsTable() {
  const rows = useQuery((s) => s.rows);
  const run = useQuery((s) => s.run);
  const error = useQuery((s) => s.error);
  const running = useQuery((s) => s.running);
  const loadMoreRows = useQuery((s) => s.loadMoreRows);
  const selectedCell = useQuery((s) => s.selectedCell);
  const setSelectedCell = useQuery((s) => s.setSelectedCell);
  const highlightPage = useQuery((s) => s.highlightPage);

  const bodyRef = useRef<HTMLDivElement>(null);
  const [hover, setHover] = useState<HoverCell | null>(null);
  const [columnSizing, setColumnSizing] = useState<ColumnSizingState>({});

  const dataRows = rows?.rows ?? (EMPTY as unknown[][]);
  const queryCols = rows?.columns ?? EMPTY;

  // A shared canvas 2D context for measuring header/cell text (auto-fit widths).
  const measure = useMemo(() => {
    const ctx = document.createElement("canvas").getContext("2d");
    if (ctx) ctx.font = MEASURE_FONT;
    return (text: string) => (ctx ? ctx.measureText(text).width : text.length * 7);
  }, []);

  // A pinned `#` column plus one column per result column (id = data index).
  const columns = useMemo<ColumnDef<unknown[]>[]>(() => [
    { id: "#", header: "#", enableResizing: false },
    ...queryCols.map((c, di) => ({ id: String(di), header: c.name })),
  ], [run?.queryId]); // eslint-disable-line react-hooks/exhaustive-deps

  // Auto-fit column widths from the first loaded window, re-seeded on each run.
  useEffect(() => {
    if (!rows) return;
    const w = fitColumnWidths(rows.columns, rows.rows, measure);
    const sizing: ColumnSizingState = { "#": IDX_W };
    rows.columns.forEach((_, di) => { sizing[String(di)] = w[di]; });
    setColumnSizing(sizing);
    setHover(null);
  }, [run?.queryId]); // eslint-disable-line react-hooks/exhaustive-deps

  const table = useReactTable({
    data: dataRows,
    columns,
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
    [table, columns, columnSizing],
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
  useEffect(() => { colVirtualizer.measure(); }, [columnSizing, run?.queryId]); // eslint-disable-line react-hooks/exhaustive-deps

  if (error) return <div className="results-msg error">{error}</div>;
  if (running && !rows) return <div className="results-msg muted">Running…</div>;
  if (!rows || !run) return <div className="results-msg muted">Run a query to see results.</div>;

  const vCols = colVirtualizer.getVirtualItems();
  const vRows = rowVirtualizer.getVirtualItems();
  const totalW = IDX_W + colVirtualizer.getTotalSize();
  const totalH = HEADER_H + rowVirtualizer.getTotalSize();

  return (
    <div className="results-table">
      <div className="rt-body" ref={bodyRef}
           onMouseLeave={() => setHover(null)}
           onScroll={(e) => {
             const el = e.currentTarget;
             // Lazy-load the next window when within ~15 rows of the loaded end.
             if (el.scrollTop + el.clientHeight > el.scrollHeight - 15 * ROW_H) void loadMoreRows();
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
                  const pages = rows.rowPages[ri]?.[di] ?? [];
                  const selected = selectedCell?.rowIndex === ri && selectedCell?.colIndex === di;
                  // Emphasize cells whose bytes (partly) live on the highlighted
                  // (overflow) page, so the overflow node's contribution stands out.
                  const highlit = highlightPage != null && pages.includes(highlightPage);
                  return (
                    <div className={"rt-cell" + (selected ? " rt-sel" : "") + (highlit ? " rt-hl" : "")} key={col.id}
                         style={{ position: "absolute", left: IDX_W + vc.start, width: vc.size, height: ROW_H }}
                         onClick={() => setSelectedCell({ rowIndex: ri, colIndex: di, column: queryCols[di], value, pages })}
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
      <div className="rt-foot muted">
        showing {dataRows.length.toLocaleString()} of {run.rowCount.toLocaleString()} rows
        {run.truncated ? " (truncated)" : ""} · {run.pageCount.toLocaleString()} pages · {run.accesses.toLocaleString()} accesses
      </div>
    </div>
  );
}

// Lightweight, non-interactive tooltip that follows the cursor (offset to the
// lower-right). Click the cell for the full details panel and page links.
function HoverTip({ hover, column }: {
  hover: HoverCell;
  column?: { sourceTable: string | null; sourceColumn: string | null };
}) {
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
