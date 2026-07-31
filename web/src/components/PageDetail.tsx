import { useMemo, useRef, useState, type ReactNode } from "react";
import { useTree } from "../state/treeStore.ts";
import type { PageCell, PageColumn, PageSegment } from "../core/types.ts";
import { regionColor, regionLabel, pageTypeDesc } from "../core/pageTypes.ts";
import { colorForPageNumber } from "../core/palette.ts";
import { formatSectorBytes, formatCount } from "../core/format.ts";
import { PageCard } from "./PageCard.tsx";
import { TableInteriorCells } from "./TableInteriorCells.tsx";

function valueText(c: PageColumn): string {
  if (c.type === "null") return "NULL";
  if (c.type === "blob") return `BLOB(${formatSectorBytes(c.bytes ?? 0)})`;
  if (c.type === "text") return `"${String(c.value ?? "")}"${c.truncated ? "…" : ""}`;
  return String(c.value);
}

// Right-hand pane: a fixed header/schematic section over a scrolling "Full Page
// Contents" table (color-coded to the schematic) and a fixed key bar. Pointers in
// the header and cell records are clickable page controls; clicking a schematic
// block scrolls the table to that region.
export function PageDetail() {
  const content = useTree((s) => s.content);
  const contentLoading = useTree((s) => s.contentLoading);
  const selectedPage = useTree((s) => s.selectedPage);
  const revealPage = useTree((s) => s.revealPage);
  const [hover, setHover] = useState<number | null>(null);
  const rowRefs = useRef<(HTMLElement | null)[]>([]);

  const kinds = useMemo(() => {
    const set = new Set<string>();
    content?.regions.forEach((r) => set.add(r.kind));
    return [...set];
  }, [content]);

  if (selectedPage == null) return <div className="results-msg muted">Select a page in the tree.</div>;
  if (!content) return <div className="results-msg muted">{contentLoading ? "Loading page…" : "No page detail."}</div>;

  const total = content.pageSize || 1;
  const cellByIndex = (i?: number) => content.cells.find((c) => c.cellIndex === i);
  const typeName = content.header["typeName"] as string | undefined;
  const pointerType = (toPage: number) => content.pointers.find((p) => p.toPage === toPage)?.pageType;
  // For an overflow page shown via its owner, the owner's leaf is the "leaf" for
  // segment colouring (its slice renders white).
  const leafPage = content.ownerPage ?? content.pageNumber;
  const scrollToRegion = (i: number) =>
    rowRefs.current[i]?.scrollIntoView({ block: "start", behavior: "smooth" });

  return (
    <div className="page-detail">
      {/* Fixed section: page number, header info, schematic. */}
      <div className="pd-fixed">
        <div className="pd-head">
          {content.object &&
            <span><b>{content.object.name}</b> <span className="muted">({content.object.type})</span></span>}
        </div>
        <div className="pd-head">
          Page {content.pageNumber} · <span className="pd-desc">{pageTypeDesc(content.pageType)}</span>
          {content.ownerPage != null &&
            <span className="pd-owner">owned by <PageCard page={content.ownerPage} onClick={revealPage} /></span>}
        </div>

        {/* Row count of this page's subtree (table-interior / table-leaf pages). */}
        {content.rowCount != null && (
          <div className="pd-head pd-rowcount">
            <b>Row Count: </b> {formatCount(content.rowCount)}
          </div>
        )}

        <div className="pd-headerfields">
          {Object.entries(content.header).map(([k, v]) => {
            if (k === "typeName" || v == null) return null;
            if (k === "rightmostPointer" && typeof v === "number" && v > 0) {
              return <span key={k} className="pd-field"><b>{k}</b> <PageCard page={v} pageType={pointerType(v)} onClick={revealPage} /></span>;
            }
            const shown = k === "type" && typeName ? `${v} (${typeName})` : String(v);
            return <span key={k} className="pd-field"><b>{k}</b> {shown}</span>;
          })}
        </div>

        {/* Horizontal schematic: block width ∝ its byte length; click → scroll. */}
        <div className="pd-schematic">
          {content.regions.map((r, i) => (
            <div key={i} className={"pd-block" + (hover === i ? " hi" : "")}
                 style={{ width: `${(r.length / total) * 100}%`, background: regionColor(r.kind) }}
                 onMouseEnter={() => setHover(i)} onMouseLeave={() => setHover(null)}
                 onClick={() => { scrollToRegion(i); }}
                 title={`${regionLabel(r.kind)} — ${formatSectorBytes(r.length)} @ ${r.offset}`} />
          ))}
        </div>
        <div className="pd-head">
          {(() => {
            const out: ReactNode[] = [];
            content.regions.forEach((r, i) => {
              // Cell regions (and an overflow page's payload region, which the
              // server annotates with the owning cell) render their contents in
              // the scrolling section below, not as a bare header row here.
              if (r.kind === "cell" || (r.kind === "payload" && r.cellIndex != null)) {
                return;
              }
              out.push(
                <div key={i} ref={(el) => { rowRefs.current[i] = el; }}
                     className={"pd-row" + (hover === i ? " hi" : "")}
                     onMouseEnter={() => setHover(i)} onMouseLeave={() => setHover(null)}>
                  <div className="pd-rowhead">
                    <span className="pd-swatch" style={{ background: regionColor(r.kind) }} />
                    <span className="pd-rkind">{regionLabel(r.kind)}</span>
                    <span className="pd-range muted">{r.offset}–{r.offset + r.length} ({formatSectorBytes(r.length)})</span>
                  </div>
                </div>,
              );
            });
            return out;
          })()}
        </div>
      </div>

      {/* Full Page Contents (scrolls). Table-interior pages render their divider
          cells as one "Table Interior Cell control" instead of per-cell rows. */}
      <div className="pd-scroll">
        <div className="pd-table">
          {(() => {
            const isInterior = content.pageType === "table-interior";
            const out: ReactNode[] = [];
            let interiorDone = false;
            content.regions.forEach((r, i) => {
              if (isInterior && r.kind === "cell") {
                if (!interiorDone) {
                  interiorDone = true;
                  out.push(
                    <TableInteriorCells key="int" content={content} hover={hover}
                                        setHover={setHover} rowRefs={rowRefs} onNav={revealPage}
                                        pointerType={pointerType} />,
                  );
                }
                return;
              }
              // A normal cell, or an overflow page's payload region carrying the
              // owning cell's slice (server sets its cellIndex).
              else if (!isInterior && (r.kind === "cell" || (r.kind === "payload" && r.cellIndex != null))){
                const cell = r.cellIndex != null ? cellByIndex(r.cellIndex) : undefined;
                out.push(
                  <div key={i} ref={(el) => { rowRefs.current[i] = el; }}
                      className={"pd-row" + (hover === i ? " hi" : "")}
                      onMouseEnter={() => setHover(i)} onMouseLeave={() => setHover(null)}>
                    <div className="pd-rowhead">
                      <span className="pd-swatch" style={{ background: regionColor(r.kind) }} />
                      <span className="pd-rkind">{regionLabel(r.kind)}{r.cellIndex != null ? ` #${r.cellIndex}` : ""}</span>
                      <span className="pd-range muted">{r.offset}–{r.offset + r.length} ({formatSectorBytes(r.length)})</span>
                    </div>
                    {cell && <CellData cell={cell} onNav={revealPage} typeOf={pointerType} leafPage={leafPage} />}
                  </div>,
                );
              }
            });
            return out;
          })()}
        </div>
      </div>

      <div className="pd-legend">
        {kinds.map((k) => (
          <span key={k} className="pd-leg">
            <span className="pd-swatch" style={{ background: regionColor(k) }} />{regionLabel(k)}
          </span>
        ))}
      </div>
    </div>
  );
}


/**
 * Component to display the type of a PageColumn, including segments which come from overflow pages.
 * 
 * @param column - The PageColumn to render the type of.
 * @param leafPage - The page number of the leaf page for the row the column belongs to.
 * @param onNav - Callback to navigate to a page when a PageCard is clicked.
 * @returns 
 */
function TypeSegments({ column, leafPage, onNav }:
                  { column: PageColumn; leafPage: number; onNav: (p: number) => void }) {
  return (
    <span>
      {column.serialName}
      {column.segments && (
        column.segments.length === 1 ?
          <PageCard page={column.segments[0].page} pageType="overflow" colorByNumber onClick={onNav} /> :
          <span className="pd-bytes">{column.segments.map((s, k) => (
            <span key={k}>{" "}{s.page === leafPage
              ? <span className="pd-seg-leaf">{formatSectorBytes(s.bytes)}</span>
              : <span key={k}>{k > 0 ? ", " : ""}<Segment seg={s} leafPage={leafPage} column={column} forTypeColumn={true} onNav={onNav} /></span>}</span>
          ))}</span>
      )}
    </span>
  )
}

/**
 * Component to display the value of a PageColumn, including segments which come from overflow pages.
 * 
 * @param column - The PageColumn to render the value of.
 * @param leafPage - The page number of the leaf page for the row the column belongs to.
 * @param onNav - Callback to navigate to a page when a PageCard is clicked. 
 */
function ValueSegments({ column, leafPage, onNav }:
                  { column: PageColumn; leafPage: number; onNav: (p: number) => void }) {
  return (
    !column.segments ? 
      <span className="pd-cvaltext">{valueText(column)}</span>
      : column.type === "blob" ? 
      (
        <span className="pd-cvaltext">BLOB {column.segments.map((s, k) => (
            <span key={k}>{k > 0 ? ", " : ""}<Segment seg={s} leafPage={leafPage} column={column} forTypeColumn={false} onNav={onNav} /></span>
          ))}
        </span>
      ) : (
        <span className="pd-cvaltext">
          {column.segments.map((s, k) =>
            <span key={k}>
              <Segment seg={s} leafPage={leafPage} column={column} forTypeColumn={false} onNav={onNav} />
            </span>
          )}
        </span>
      )
  );
}

/**
 * Component to display a single PageSegment.
 * 
 * @param seg - The PageSegment to render.
 * @param leafPage - The page number of the leaf page for the row.
 * @param column - The column containing this segment.
 * @param forTypeColumn - If true, display the type of the segment.
 * @param onNav - Callback to navigate to a page when a PageCard is clicked.
 */
function Segment({ seg, leafPage, column, forTypeColumn, onNav }:
                  { seg: PageSegment; leafPage: number; column: PageColumn; forTypeColumn: boolean; onNav: (p: number) => void }) {
  
  const text = forTypeColumn || column.type == "blob" ? formatSectorBytes(seg.bytes) : column.type === "int" || column.type === "real" ? valueText(column) : seg.text;
  if (seg.page === leafPage)
    return <span className="pd-seg-leaf">{text}</span>;
  
  return (
    <span className="pd-seg-ovf" style={{ background: colorForPageNumber(seg.page) }} title={!forTypeColumn ? `overflow page ${seg.page}` : undefined}>
      {forTypeColumn ? 
        <span>
          {text}
          <span> → </span>
          <PageCard page={seg.page} pageType="overflow" colorByNumber onClick={onNav} />
        </span> : text}
    </span>
  );
}

function CellData({ cell, onNav, typeOf, leafPage }:
                  { cell: PageCell; onNav: (p: number) => void;
                    typeOf: (p: number) => string | undefined; leafPage: number }) {
  return (
    <div className="pd-cell">
      <div className="pd-cellmeta">
        {cell.rowid != null && <span className="pd-kv">rowid {cell.rowid}</span>}
        {cell.leftChild != null && <span className="pd-kv">left child <PageCard page={cell.leftChild} pageType={typeOf(cell.leftChild)} onClick={onNav} /></span>}
        {cell.overflowPage != null && <span className="pd-kv">overflow <PageCard page={cell.overflowPage} pageType="overflow" onClick={onNav} /></span>}
      </div>
      {cell.columns && cell.columns.length > 0 && (
        <table className="pd-coltable">
          <thead><tr><th>#</th><th>type</th><th>value</th></tr></thead>
          <tbody>
            {cell.columns.map((c, i) => (
              <tr key={i}>
                <td className="muted">{i}</td>
                <td className="pd-ctype">
                  <TypeSegments column={c} leafPage={leafPage} onNav={onNav} />
                </td>
                <td className="pd-cval">
                  <ValueSegments column={c} leafPage={leafPage} onNav={onNav} />
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </div>
  );
}
