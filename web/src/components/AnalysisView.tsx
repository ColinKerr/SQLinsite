import { useRef, useState } from "react";
import { useAnalysis, type AnalysisTab } from "../state/analysisStore.ts";
import { PredefinedMetrics } from "./PredefinedMetrics.tsx";
import { SqlEditor } from "./SqlEditor.tsx";
import { ResultsGrid } from "./ResultsGrid.tsx";
import { ExplainView } from "./ExplainResults.tsx";
import { RunningIndicator } from "./RunningIndicator.tsx";

const TABS: { id: AnalysisTab; label: string }[] = [
  { id: "metrics", label: "Predefined Metrics" },
  { id: "query", label: "Query Metrics" },
];

// The Query-Metrics results pane: the shared ResultsGrid (no row→page mapping,
// no pages/tables sub-tabs), or the shared Explain view when Explain was pressed.
function AnalysisResults() {
  const running = useAnalysis((s) => s.running);
  const mode = useAnalysis((s) => s.mode);
  const result = useAnalysis((s) => s.result);
  const explain = useAnalysis((s) => s.explain);

  if (running) return <RunningIndicator />;
  if (mode === "explain") return <ExplainView explain={explain} />;
  return (
    <ResultsGrid
      columns={result?.columns ?? []}
      rows={result?.error ? null : (result?.rows ?? null)}
      rowCount={result?.rowCount ?? 0}
      error={result?.error ?? null}
      runKey={result ? result.rowCount ?? 0 : null}
    />
  );
}

// The Query-Metrics sub-view: laid out like the Query view (shared editor on top,
// results below, resizable divider) over the unified analysis connection.
function QueryMetrics() {
  const sql = useAnalysis((s) => s.sql);
  const setSql = useAnalysis((s) => s.setSql);
  const run = useAnalysis((s) => s.run);
  const running = useAnalysis((s) => s.running);
  const doExplain = useAnalysis((s) => s.doExplain);
  const history = useAnalysis((s) => s.history);
  const loadHistory = useAnalysis((s) => s.loadHistory);

  const [editorPct, setEditorPct] = useState(33);
  const viewerRef = useRef<HTMLDivElement>(null);
  const startDrag = (e: React.MouseEvent) => {
    e.preventDefault();
    const onMove = (ev: MouseEvent) => {
      const rect = viewerRef.current?.getBoundingClientRect();
      if (!rect) return;
      setEditorPct(Math.max(15, Math.min(80, ((ev.clientY - rect.top) / rect.height) * 100)));
    };
    const onUp = () => {
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
    };
    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  };

  return (
    <div id="query-viewer" ref={viewerRef}>
      <div className="qv-pane" style={{ height: `${editorPct}%` }}>
        <SqlEditor
          sql={sql}
          onChange={setSql}
          onRun={() => void run()}
          running={running}
          onExplain={() => void doExplain()}
          history={history.map((h) => ({ id: h.id, sql: h.sql, meta: `${h.rowCount} rows` }))}
          onSelectHistory={(id) => void loadHistory(id)}
        />
      </div>
      <div id="qsplit" title="Drag to resize" onMouseDown={startDrag} />
      <div className="qv-pane" style={{ height: `${100 - editorPct}%` }}>
        <div className="aq-results"><AnalysisResults /></div>
      </div>
    </div>
  );
}

// The Analysis view content (right of the shared B-Tree Tree): a Predefined-metrics
// dashboard or a Query-Metrics editor over the unified analysis connection.
export function AnalysisView() {
  const tab = useAnalysis((s) => s.tab);
  const setTab = useAnalysis((s) => s.setTab);

  return (
    <div id="analysis-view">
      <div className="results-tabs">
        {TABS.map((t) => (
          <button key={t.id} className={"rtab" + (tab === t.id ? " active" : "")}
                  onClick={() => setTab(t.id)}>
            {t.label}
          </button>
        ))}
      </div>
      {tab === "metrics" ? <PredefinedMetrics /> : <QueryMetrics />}
    </div>
  );
}
