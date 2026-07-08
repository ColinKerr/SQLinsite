import { useEffect, useState } from "react";
import type { PageDetail, SchemaColumn, SchemaTable, SchemaView } from "../core/types.ts";
import { useViz } from "../state/store.ts";
import { useQuery } from "../state/queryStore.ts";
import { useTree } from "../state/treeStore.ts";
import { fetchPage } from "../core/api.ts";
import { colorForPageNumber } from "../core/palette.ts";
import { formatCellText } from "../core/columnFit.ts";

const qi = (name: string) => `"${name.replace(/"/g, '""')}"`;

function RunButton({ sql }: { sql: string }) {
  const runSql = useQuery((s) => s.runSql);
  return (
    <button
      className="run-btn"
      title="Run"
      onClick={(e) => { e.preventDefault(); e.stopPropagation(); void runSql(sql); }}
    >
      ▶
    </button>
  );
}

function Info({ pages, acc, count }: { pages?: number; acc?: number; count?: number }) {
  return (
    <span className="node-info">
      {count != null && <span className="count">{count}</span>}
      {pages != null && <span className="count" title="pages">{pages.toLocaleString()}p</span>}
      {acc != null && acc > 0 && <span className="acc" title="accessed pages">{acc.toLocaleString()}a</span>}
    </span>
  );
}

function ColumnList({ table, columns }: { table: string; columns: SchemaColumn[] }) {
  return (
    <details className="tree-branch">
      <summary><span className="node-name">Columns</span><Info count={columns.length} /></summary>
      {columns.map((c) => (
        <div className="tree-leaf" key={c.name}>
          <span className="node-name">{c.name}<span className="col-type"> {c.type}</span></span>
          <RunButton sql={`SELECT ${qi(c.name)} FROM ${qi(table)};`} />
        </div>
      ))}
    </details>
  );
}

function TableNode({ t }: { t: SchemaTable }) {
  return (
    <details className="tree-branch">
      <summary>
        <span className="node-name">{t.name}</span>
        <Info pages={t.pageCount} acc={t.accessedPages} />
        <RunButton sql={`SELECT * FROM ${qi(t.name)};`} />
      </summary>
      <ColumnList table={t.name} columns={t.columns} />
      <details className="tree-branch">
        <summary><span className="node-name">Indexes</span><Info count={t.indexes.length} /></summary>
        {t.indexes.map((i) => (
          <div className="tree-leaf" key={i.name}>
            <span className="node-name">{i.name}</span>
            <Info pages={i.pageCount} acc={i.accessedPages} />
            <RunButton sql={`SELECT * FROM ${qi(t.name)};`} />
          </div>
        ))}
      </details>
      <details className="tree-branch">
        <summary><span className="node-name">Triggers</span><Info count={t.triggers.length} /></summary>
        {t.triggers.map((tr) => (
          <div className="tree-leaf" key={tr}><span className="node-name">{tr}</span></div>
        ))}
      </details>
    </details>
  );
}

function ViewNode({ v }: { v: SchemaView }) {
  return (
    <details className="tree-branch">
      <summary>
        <span className="node-name">{v.name}</span>
        <RunButton sql={`SELECT * FROM ${qi(v.name)};`} />
      </summary>
      <ColumnList table={v.name} columns={v.columns} />
    </details>
  );
}

// The lower portion of the schema panel: details for the results-table cell the
// user clicked, including clickable links to the page(s) holding the cell's bytes.
function CellDetailsPanel() {
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

export function SchemaPanel() {
  const legendWidth = useViz((s) => s.legendWidth);
  const schema = useQuery((s) => s.schema);
  const loadSchema = useQuery((s) => s.loadSchema);

  useEffect(() => { if (!schema) void loadSchema(); }, [schema, loadSchema]);

  return (
    <aside id="schema" style={{ width: legendWidth }}>
      <div className="schema-tree">
        {!schema ? (
          <div className="muted">Loading schema…</div>
        ) : (
          <>
            <details className="tree-branch" open>
              <summary><span className="node-name">Tables</span><Info count={schema.tables.length} /></summary>
              {schema.tables.map((t) => <TableNode key={t.name} t={t} />)}
            </details>
            <details className="tree-branch" open>
              <summary><span className="node-name">Views</span><Info count={schema.views.length} /></summary>
              {schema.views.map((v) => <ViewNode key={v.name} v={v} />)}
            </details>
          </>
        )}
      </div>
      <CellDetailsPanel />
    </aside>
  );
}
