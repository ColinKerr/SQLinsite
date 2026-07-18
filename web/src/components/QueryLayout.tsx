import { useEffect, useMemo, useRef, useState } from "react";
import { useQuery } from "../state/queryStore.ts";
import { useViz } from "../state/store.ts";
import { useRegisterNodeActivation } from "../state/treeSelectionStore.ts";
import { makeQueryActivate } from "./nodeActivation.ts";
import { QueryEditor } from "./QueryEditor.tsx";
import { ResultsView } from "./ResultsView.tsx";
import { CellDetails } from "./CellDetails.tsx";

// The live Query view content (to the right of the shared Navigation Panel): the
// "query and data viewer" — editor over results, split by a horizontal divider —
// plus the Cell Details panel that appears when a results cell is clicked.
export function QueryLayout() {
  const [editorPct, setEditorPct] = useState(33);
  const viewerRef = useRef<HTMLDivElement>(null);
  const refreshHistory = useQuery((s) => s.refreshHistory);
  const runObjectQuery = useQuery((s) => s.runObjectQuery);
  const runPageQuery = useQuery((s) => s.runPageQuery);
  const objById = useViz((s) => s.objById);

  useEffect(() => { void refreshHistory(); }, [refreshHistory]);

  // Activating a node fills the results view with that node's rows.
  useRegisterNodeActivation(useMemo(
    () => makeQueryActivate({ objById, runObjectQuery, runPageQuery }),
    [objById, runObjectQuery, runPageQuery]));

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
    <div id="query-viewer" ref={viewerRef}>
      <div className="qv-pane" style={{ height: `${editorPct}%` }}>
        <QueryEditor />
      </div>
      <div id="qsplit" title="Drag to resize" onMouseDown={startDrag} />
      <div className="qv-pane" style={{ height: `${100 - editorPct}%` }}>
        <ResultsView />
      </div>
      <CellDetails />
    </div>
  );
}
