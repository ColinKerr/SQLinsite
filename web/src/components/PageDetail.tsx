import { useMemo, useRef, useState } from "react";
import { useTree } from "../state/treeStore.ts";
import type { PageCell, PageColumn } from "../core/types.ts";
import { regionColor, regionLabel, pageTypeDesc } from "../core/pageTypes.ts";
import { colorForPage, GLYPH } from "../core/palette.ts";

function valueText(c: PageColumn): string {
  if (c.type === "null") return "NULL";
  if (c.type === "blob") return `BLOB(${c.bytes ?? 0} bytes)`;
  if (c.type === "text") return `"${String(c.value ?? "")}"${c.truncated ? "…" : ""}`;
  return String(c.value);
}

// A small graphical page representation matching a b-tree tree node: type glyph +
// color + page number. Clicking navigates to that page and expands+selects it in
// the tree.
function PageCard({ page, pageType, label, onClick }:
                  { page: number; pageType?: string; label?: string; onClick: (p: number) => void }) {
  return (
    <button className="pgcard" onClick={() => onClick(page)} title={pageType ? pageTypeDesc(pageType) : `page ${page}`}>
      <span className="pgcard-glyph" style={{ background: colorForPage(null, pageType ?? "") }}>
        {pageType ? (GLYPH[pageType] ?? "·") : "·"}
      </span>
      <span className="pgcard-num">{label ?? `p${page}`}</span>
    </button>
  );
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
  const rowRefs = useRef<(HTMLDivElement | null)[]>([]);

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
  const scrollToRegion = (i: number) =>
    rowRefs.current[i]?.scrollIntoView({ block: "start", behavior: "smooth" });

  return (
    <div className="page-detail">
      {/* Fixed section: page number, header info, schematic. */}
      <div className="pd-fixed">
        <div className="pd-head">
          Page {content.pageNumber} · <span className="muted">{content.pageType}</span>
          <span className="pd-desc">{pageTypeDesc(content.pageType)}</span>
        </div>

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
                 onClick={() => scrollToRegion(i)}
                 title={`${regionLabel(r.kind)} — ${r.length} bytes @ ${r.offset}`} />
          ))}
        </div>
      </div>

      {/* Full Page Contents (scrolls). */}
      <div className="pd-scroll">
        <div className="pd-table">
          {content.regions.map((r, i) => {
            const cell = r.kind === "cell" ? cellByIndex(r.cellIndex) : undefined;
            return (
              <div key={i} ref={(el) => { rowRefs.current[i] = el; }}
                   className={"pd-row" + (hover === i ? " hi" : "")}
                   onMouseEnter={() => setHover(i)} onMouseLeave={() => setHover(null)}>
                <div className="pd-rowhead">
                  <span className="pd-swatch" style={{ background: regionColor(r.kind) }} />
                  <span className="pd-rkind">{regionLabel(r.kind)}{r.cellIndex != null ? ` #${r.cellIndex}` : ""}</span>
                  <span className="pd-range muted">{r.offset}–{r.offset + r.length} ({r.length}B)</span>
                </div>
                {cell && <CellData cell={cell} onNav={revealPage} typeOf={pointerType} />}
              </div>
            );
          })}
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

function CellData({ cell, onNav, typeOf }:
                  { cell: PageCell; onNav: (p: number) => void; typeOf: (p: number) => string | undefined }) {
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
                <td className="pd-ctype">{c.serialType} <span className="muted">({c.serialName})</span></td>
                <td className="pd-cval">
                  <span className="pd-cvaltext">{valueText(c)}</span>
                  {c.fromOverflow && (
                    <span className="pd-ovf" title="stored in overflow page(s)">
                      ⇢ overflow
                      {(c.overflowPages ?? []).map((p) => (
                        <PageCard key={p} page={p} pageType="overflow" onClick={onNav} />
                      ))}
                    </span>
                  )}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </div>
  );
}
