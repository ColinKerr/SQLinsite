import { useEffect, useRef, useState } from "react";
import Editor, { type OnMount } from "@monaco-editor/react";
import { formatSql } from "../core/formatSql.ts";
import { useQuery } from "../state/queryStore.ts";

function ControlBar() {
  const runCurrent = useQuery((s) => s.runCurrent);
  const running = useQuery((s) => s.running);
  const doExplain = useQuery((s) => s.doExplain);
  const sql = useQuery((s) => s.sql);
  const setSql = useQuery((s) => s.setSql);
  const history = useQuery((s) => s.history);
  const loadHistory = useQuery((s) => s.loadHistory);
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
      <button className="qbtn" title="Run (Ctrl+Enter)" disabled={running} onClick={() => void runCurrent()}>
        ▶ Run
      </button>
      <div className="dropdown" ref={rootRef}>
        <button className="qbtn" aria-expanded={histOpen} onClick={() => setHistOpen((o) => !o)}>History</button>
        {histOpen && (
          <div className="dropdown-panel">
            {history.length === 0 && <div className="muted">No queries yet</div>}
            {history.map((h) => (
              <div className="hist-item" key={h.id} onClick={() => { setHistOpen(false); void loadHistory(h.id); }}>
                <span className="hist-sql">{h.sql}</span>
                <span className="count">{h.pageCount.toLocaleString()}p</span>
              </div>
            ))}
          </div>
        )}
      </div>
      <button className="qbtn" title="Format SQL" onClick={() => setSql(formatSql(sql))}>Format</button>
      <button className="qbtn" title="Explain" onClick={() => void doExplain()}>Explain</button>
    </div>
  );
}

export function QueryEditor() {
  const sql = useQuery((s) => s.sql);
  const setSql = useQuery((s) => s.setSql);

  const onMount: OnMount = (editor, monaco) => {
    editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter, () => {
      void useQuery.getState().runCurrent();
    });
  };

  return (
    <div id="query-editor">
      <ControlBar />
      <div className="editor-host">
        <Editor
          language="sql"
          theme="vs-dark"
          value={sql}
          onChange={(v) => setSql(v ?? "")}
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
