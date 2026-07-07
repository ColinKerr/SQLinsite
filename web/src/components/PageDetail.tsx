import { useMemo, useState } from "react";
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

// A small graphical page representation: type glyph + color + page number,
// clickable to navigate.
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

// Right-hand pane: header detail, outgoing pointers as page cards, a
// byte-proportional schematic, and a color-coded table of the decoded contents
// (cell values in full, with overflow-sourced data clearly flagged).
export function PageDetail() {
  const content = useTree((s) => s.content);
  const contentLoading = useTree((s) => s.contentLoading);
  const selectedPage = useTree((s) => s.selectedPage);
  const selectPage = useTree((s) => s.selectPage);
  const [hover, setHover] = useState<number | null>(null);

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

  return (
    <div className="page-detail">
      <div className="pd-scroll">
      <div className="pd-head">
        Page {content.pageNumber} · <span className="muted">{content.pageType}</span>
        <span className="pd-desc">{pageTypeDesc(content.pageType)}</span>
      </div>

      {/* Header fields (horizontal); the b-tree type byte shows its english name. */}
      <div className="pd-headerfields">
        {Object.entries(content.header).map(([k, v]) => {
          if (k === "typeName" || v == null) return null;
          const shown = k === "type" && typeName ? `${v} (${typeName})` : String(v);
          return <span key={k} className="pd-field"><b>{k}</b> {shown}</span>;
        })}
      </div>

      {/* Horizontal schematic: block width ∝ its byte length. */}
      <div className="pd-schematic">
        {content.regions.map((r, i) => (
          <div key={i} className={"pd-block" + (hover === i ? " hi" : "")}
               style={{ width: `${(r.length / total) * 100}%`, background: regionColor(r.kind) }}
               onMouseEnter={() => setHover(i)} onMouseLeave={() => setHover(null)}
               title={`${regionLabel(r.kind)} — ${r.length} bytes @ ${r.offset}`} />
        ))}
      </div>

      <div className="pd-table">
        {content.regions.map((r, i) => {
          const cell = r.kind === "cell" ? cellByIndex(r.cellIndex) : undefined;
          return (
            <div key={i} className={"pd-row" + (hover === i ? " hi" : "")}
                 onMouseEnter={() => setHover(i)} onMouseLeave={() => setHover(null)}>
              <div className="pd-rowhead">
                <span className="pd-swatch" style={{ background: regionColor(r.kind) }} />
                <span className="pd-rkind">{regionLabel(r.kind)}{r.cellIndex != null ? ` #${r.cellIndex}` : ""}</span>
                <span className="pd-range muted">{r.offset}–{r.offset + r.length} ({r.length}B)</span>
              </div>
              {cell && <CellData cell={cell} onNav={selectPage} />}
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

function CellData({ cell, onNav }: { cell: PageCell; onNav: (p: number) => void }) {
  return (
    <div className="pd-cell">
      <div className="pd-cellmeta">
        {cell.rowid != null && <span className="pd-kv">rowid {cell.rowid}</span>}
        {cell.leftChild != null && <span className="pd-kv">left child <PageCard page={cell.leftChild} onClick={onNav} /></span>}
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
