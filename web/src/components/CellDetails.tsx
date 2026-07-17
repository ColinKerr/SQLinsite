import { useEffect, useState } from "react";
import type { PageDetail } from "../core/types.ts";
import { useViz } from "../state/store.ts";
import { useQuery } from "../state/queryStore.ts";
import { useTree } from "../state/treeStore.ts";
import { fetchPage } from "../core/api.ts";
import { colorForPageNumber } from "../core/palette.ts";
import { formatCellText } from "../core/columnFit.ts";

// Details for the results-table cell the user clicked: its provenance, value, and
// clickable links to the page(s) holding the cell's bytes. Shown in the Query view
// (below the results); hidden when no cell is selected.
export function CellDetails() {
  const selectedCell = useQuery((s) => s.selectedCell);
  const setSelectedCell = useQuery((s) => s.setSelectedCell);
  const setView = useViz((s) => s.setView);
  const revealPage = useTree((s) => s.revealPage);
  const [details, setDetails] = useState<Record<number, PageDetail | null>>({});

  // Open the b-tree tree view and expand+select the node for this page.
  const goToPage = (p: number) => { setView("tree"); void revealPage(p); };

  const pages = selectedCell?.pages ?? [];
  useEffect(() => {
    setDetails({});
    let cancelled = false;
    for (const p of pages) {
      void fetchPage(p).then((pd) => { if (!cancelled) setDetails((d) => ({ ...d, [p]: pd })); });
    }
    return () => { cancelled = true; };
  }, [selectedCell]); // eslint-disable-line react-hooks/exhaustive-deps

  if (!selectedCell) return null;
  const { column, value } = selectedCell;
  const provenance = column.sourceTable
    ? `${column.sourceTable}.${column.sourceColumn}`
    : "expression / computed";

  return (
    <div className="cell-details">
      <div className="cd-head">
        <span className="cd-title">{column.name}</span>
        <button className="cd-close" title="Close" onClick={() => setSelectedCell(null)}>✕</button>
      </div>
      <div className="cd-provenance">{provenance}</div>
      <div className="cd-value" title="cell value">{formatCellText(value)}</div>
      <div className="cd-pages-label">
        {pages.length === 0 ? "no page mapping" : pages.length === 1 ? "Page" : "Pages"}
      </div>
      {pages.map((p) => {
        const d = details[p];
        return (
          <div className="cd-page" key={p}>
            <span className="cd-swatch" style={{ background: colorForPageNumber(p) }} />
            <button className="cd-link" onClick={() => goToPage(p)}>Go to page {p}</button>
            {d && (
              <span className="cd-meta">
                {d.pageType}
                {d.cellCount != null ? ` · ${d.cellCount} cells` : ""}
                {d.profile ? ` · ${d.profile.reads}r/${d.profile.writes}w` : ""}
              </span>
            )}
          </div>
        );
      })}
    </div>
  );
}
