import { useEffect, useRef, useState } from "react";
import Editor, { type OnMount } from "@monaco-editor/react";
import { formatSql } from "../core/formatSql.ts";

// A history entry the editor can list and re-open. `meta` is a short right-aligned
// label (Query view: page count; Analysis view: row count).
export interface EditorHistoryItem { id: number; sql: string; meta?: string; }

export interface SqlEditorProps {
  sql: string;
  onChange: (sql: string) => void;
  onRun: () => void;
  running: boolean;
  // Optional controls — shown only when a handler/list is supplied, so the same
  // control bar serves both the Query and Analysis views.
  onExplain?: () => void;
  history?: EditorHistoryItem[];
  onSelectHistory?: (id: number) => void;
}

function ControlBar({ sql, onChange, onRun, running, onExplain, history, onSelectHistory }: SqlEditorProps) {
  const [histOpen, setHistOpen] = useState(false);
  const rootRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!histOpen) return;
    const onDoc = (e: MouseEvent) => {
      if (rootRef.current && !rootRef.current.contains(e.target as Node)) setHistOpen(false);
    };
    document.addEventListener("click", onDoc);
    return () => document.removeEventListener("click", onDoc);
  }, [histOpen]);

  return (
    <div id="query-control-bar">
      <button className="qbtn" title="Run (Ctrl+Enter)" disabled={running} onClick={onRun}>
        ▶ Run
      </button>
      {history && (
        <div className="dropdown" ref={rootRef}>
          <button className="qbtn" aria-expanded={histOpen} onClick={() => setHistOpen((o) => !o)}>History</button>
          {histOpen && (
            <div className="dropdown-panel">
              {history.length === 0 && <div className="muted">No queries yet</div>}
              {history.map((h) => (
                <div className="hist-item" key={h.id} onClick={() => { setHistOpen(false); onSelectHistory?.(h.id); }}>
                  <span className="hist-sql">{h.sql}</span>
                  {h.meta && <span className="muted">{h.meta}</span>}
                </div>
              ))}
            </div>
          )}
        </div>
      )}
      <button className="qbtn" title="Format SQL" onClick={() => onChange(formatSql(sql))}>Format</button>
      {onExplain && <button className="qbtn" title="Explain" onClick={onExplain}>Explain</button>}
    </div>
  );
}

// The shared SQL editor: an embedded Monaco editor over a control bar (Run,
// optional History, Format, optional Explain). State/handlers come from props so
// both the Query view (on useQuery) and the Analysis view (on useAnalysis) reuse it.
export function SqlEditor(props: SqlEditorProps) {
  const { sql, onChange, onRun } = props;
  // Keep Ctrl+Enter bound to the latest onRun without re-mounting the editor.
  const onRunRef = useRef(onRun);
  onRunRef.current = onRun;

  const onMount: OnMount = (editor, monaco) => {
    editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter, () => { onRunRef.current(); });
  };

  return (
    <div id="query-editor">
      <ControlBar {...props} />
      <div className="editor-host">
        <Editor
          language="sql"
          theme="vs-dark"
          value={sql}
          onChange={(v) => onChange(v ?? "")}
          onMount={onMount}
          options={{
            minimap: { enabled: false }, fontSize: 13, scrollBeyondLastLine: false,
            automaticLayout: true, lineNumbersMinChars: 3,
          }}
        />
      </div>
    </div>
  );
}
