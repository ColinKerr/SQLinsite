import { useEffect, useMemo, useRef, useState } from "react";
import { useQuery } from "../state/queryStore.ts";
import { colorForPageNumber } from "../core/palette.ts";
import { fitColumnWidths, formatCellText, MIN_COL } from "../core/columnFit.ts";

const ROW_H = 24;
const IDX_W = 48;
const MEASURE_FONT = "12px -apple-system, system-ui, sans-serif";

interface HoverCell { c: number; pages: number[]; x: number; y: number; }

// Virtualized results grid. Columns are resizable and auto-fit the first page of
// data on each run. Each cell is shaded by the page(s) that store its bytes (one
// vertical segment per page). Hovering shows a lightweight tooltip that follows
// the cursor; clicking a cell opens the details panel in the schema panel.
export function ResultsTable() {
  const rows = useQuery((s) => s.rows);
  const run = useQuery((s) => s.run);
  const error = useQuery((s) => s.error);
  const running = useQuery((s) => s.running);
  const loadMoreRows = useQuery((s) => s.loadMoreRows);
  const selectedCell = useQuery((s) => s.selectedCell);
  const setSelectedCell = useQuery((s) => s.setSelectedCell);

  const bodyRef = useRef<HTMLDivElement>(null);
  const headerRef = useRef<HTMLDivElement>(null);
  const [scrollTop, setScrollTop] = useState(0);
  const [height, setHeight] = useState(400);

  const [widths, setWidths] = useState<number[]>([]);
  const [hover, setHover] = useState<HoverCell | null>(null);

  const cols = rows?.columns ?? [];

  // A shared canvas 2D context for measuring text (auto-fit), created once.
  const measure = useMemo(() => {
    const canvas = document.createElement("canvas");
    const ctx = canvas.getContext("2d");
    if (ctx) ctx.font = MEASURE_FONT;
    return (text: string) => (ctx ? ctx.measureText(text).width : text.length * 7);
  }, []);

  // Re-fit column widths once per run, from the first loaded window of rows.
  useEffect(() => {
    if (!rows) return;
    setWidths(fitColumnWidths(rows.columns, rows.rows, measure));
    setHover(null);
  }, [run?.queryId]); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    const el = bodyRef.current;
    if (!el) return;
    const ro = new ResizeObserver(() => setHeight(el.clientHeight));
    ro.observe(el);
    setHeight(el.clientHeight);
    return () => ro.disconnect();
  }, [rows]);

  if (error) return <div className="results-msg error">{error}</div>;
  if (running && !rows) return <div className="results-msg muted">Running…</div>;
  if (!rows || !run) return <div className="results-msg muted">Run a query to see results.</div>;

  const total = rows.rows.length;
  const start = Math.max(0, Math.floor(scrollTop / ROW_H) - 5);
  const end = Math.min(total, start + Math.ceil(height / ROW_H) + 10);
  // Fall back to a default width until the per-run auto-fit effect populates
  // `widths` (also covers the frame where the column set just changed).
  const effWidths = widths.length === cols.length ? widths : cols.map(() => 140);
  const grid = `${IDX_W}px ` + effWidths.map((w) => `${w}px`).join(" ");
  const totalWidth = IDX_W + effWidths.reduce((a, b) => a + b, 0);

  const slice = [];
  for (let i = start; i < end; i++) slice.push(i);

  return (
    <div className="results-table">
      <div className="rt-header" ref={headerRef}>
        <div className="rt-header-inner" style={{ gridTemplateColumns: grid, width: totalWidth }}>
          <div className="rt-cell rt-idx">#</div>
          {cols.map((c, i) => (
            <div className="rt-cell rt-th" key={i}
                 title={c.sourceTable ? `${c.sourceTable}.${c.sourceColumn}` : "expression / computed"}>
              <span className="rt-th-name">{c.name}</span>
              <ColumnResizer index={i} onResize={setWidths} widthsRef={effWidths} />
            </div>
          ))}
        </div>
      </div>
      <div className="rt-body" ref={bodyRef}
           onMouseLeave={() => setHover(null)}
           onScroll={(e) => {
             const el = e.currentTarget;
             setScrollTop(el.scrollTop);
             if (headerRef.current) headerRef.current.scrollLeft = el.scrollLeft;
             // Lazy-load the next window when within ~15 rows of the loaded end.
             if (el.scrollTop + el.clientHeight > el.scrollHeight - 15 * ROW_H) void loadMoreRows();
           }}>
        <div style={{ height: total * ROW_H, width: totalWidth, position: "relative" }}>
          {slice.map((i) => (
            <div className="rt-row" key={i}
                 style={{ position: "absolute", top: i * ROW_H, height: ROW_H, left: 0,
                          width: totalWidth, display: "grid", gridTemplateColumns: grid }}>
              <div className="rt-cell rt-idx">{i + 1}</div>
              {cols.map((c, ci) => {
                const pages = rows.rowPages[i]?.[ci] ?? [];
                const selected = selectedCell?.rowIndex === i && selectedCell?.colIndex === ci;
                return (
                  <div className={"rt-cell" + (selected ? " rt-sel" : "")} key={ci}
                       onClick={() => setSelectedCell({
                         rowIndex: i, colIndex: ci, column: c, value: rows.rows[i][ci], pages,
                       })}
                       onMouseMove={(e) => {
                         if (pages.length === 0) { setHover(null); return; }
                         setHover({ c: ci, pages, x: e.clientX, y: e.clientY });
                       }}>
                    {pages.length > 0 && (
                      <div className="rt-segs">
                        {pages.map((p, k) => (
                          <div className="rt-seg" key={k} style={{ background: colorForPageNumber(p) }} />
                        ))}
                      </div>
                    )}
                    <span className="rt-val">{formatCellText(rows.rows[i][ci])}</span>
                  </div>
                );
              })}
            </div>
          ))}
        </div>
      </div>
      {hover && <HoverTip hover={hover} column={cols[hover.c]} />}
      <div className="rt-foot muted">
        showing {total.toLocaleString()} of {run.rowCount.toLocaleString()} rows
        {run.truncated ? " (truncated)" : ""} · {run.pageCount.toLocaleString()} pages · {run.accesses.toLocaleString()} accesses
      </div>
    </div>
  );
}

// Drag handle on a header cell's right edge; updates that column's width.
function ColumnResizer({ index, onResize, widthsRef }:
                       { index: number; onResize: (fn: (w: number[]) => number[]) => void; widthsRef: number[] }) {
  const drag = useRef<{ startX: number; startW: number } | null>(null);
  return (
    <div className="rt-resizer"
         onPointerDown={(e) => {
           e.preventDefault();
           e.stopPropagation();
           (e.target as HTMLElement).setPointerCapture(e.pointerId);
           drag.current = { startX: e.clientX, startW: widthsRef[index] ?? MIN_COL };
         }}
         onPointerMove={(e) => {
           if (!drag.current) return;
           const next = Math.max(MIN_COL, drag.current.startW + (e.clientX - drag.current.startX));
           onResize((ws) => ws.map((w, i) => (i === index ? next : w)));
         }}
         onPointerUp={(e) => {
           drag.current = null;
           (e.target as HTMLElement).releasePointerCapture(e.pointerId);
         }} />
  );
}

// Lightweight, non-interactive tooltip that follows the cursor (offset to the
// lower-right). Click the cell for the full details panel and page links.
function HoverTip({ hover, column }: {
  hover: HoverCell;
  column: { sourceTable: string | null; sourceColumn: string | null };
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
