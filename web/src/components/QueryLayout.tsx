import { useEffect, useRef, useState } from "react";
import { useQuery } from "../state/queryStore.ts";
import { QueryEditor } from "./QueryEditor.tsx";
import { ResultsView } from "./ResultsView.tsx";
import { SchemaPanel } from "./SchemaPanel.tsx";
import { PanelResizer } from "./PanelResizer.tsx";

// The live Query view: the "query and data viewer" (editor over results, split by
// a horizontal divider) plus the schema panel on the right.
export function QueryLayout() {
  const [editorPct, setEditorPct] = useState(33);
  const viewerRef = useRef<HTMLDivElement>(null);
  const refreshHistory = useQuery((s) => s.refreshHistory);

  useEffect(() => { void refreshHistory(); }, [refreshHistory]);

  const startDrag = (e: React.MouseEvent) => {
    e.preventDefault();
    const onMove = (ev: MouseEvent) => {
      const rect = viewerRef.current?.getBoundingClientRect();
      if (!rect) return;
      const pct = ((ev.clientY - rect.top) / rect.height) * 100;
      setEditorPct(Math.max(15, Math.min(80, pct)));
    };
    const onUp = () => {
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
    };
    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  };

  return (
    <main>
      <div id="query-viewer" ref={viewerRef}>
        <div className="qv-pane" style={{ height: `${editorPct}%` }}>
          <QueryEditor />
        </div>
        <div id="qsplit" title="Drag to resize" onMouseDown={startDrag} />
        <div className="qv-pane" style={{ height: `${100 - editorPct}%` }}>
          <ResultsView />
        </div>
      </div>
      <PanelResizer />
      <SchemaPanel />
    </main>
  );
}
