import { useEffect, useRef, useState } from "react";
import { useTree } from "../state/treeStore.ts";
import { BTreeTree } from "./BTreeTree.tsx";
import { PageDetail } from "./PageDetail.tsx";
import { TableOverview } from "./TableOverview.tsx";
import { IndexOverview } from "./IndexOverview.tsx";

// The Page Tree view: the virtualized b-tree tree on the left and the page-detail
// view on the right, separated by a draggable divider.
export function PageTreeView() {
  const roots = useTree((s) => s.roots);
  const loadRoots = useTree((s) => s.loadRoots);
  const overviewMode = useTree((s) => s.overviewMode);
  useEffect(() => { if (!roots) void loadRoots(); }, [roots, loadRoots]);

  // The tree is a fixed-ish narrow column (like the Navigation Panel), resizable
  // via the divider; the detail pane fills the rest.
  const [treeWidth, setTreeWidth] = useState(520);
  const ref = useRef<HTMLDivElement>(null);
  const startDrag = (e: React.MouseEvent) => {
    e.preventDefault();
    const onMove = (ev: MouseEvent) => {
      const rect = ref.current?.getBoundingClientRect();
      if (!rect) return;
      setTreeWidth(Math.max(180, Math.min(820, ev.clientX - rect.left)));
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
      <div id="tree-split" ref={ref}>
        <div className="tv-pane" style={{ flex: `0 0 ${treeWidth}px` }}><BTreeTree /></div>
        <div id="tv-divider" title="Drag to resize" onMouseDown={startDrag} />
        <div className="tv-pane" style={{ flex: "1 1 auto" }}>
          {overviewMode === "index" ? <IndexOverview />
            : overviewMode === "table" ? <TableOverview />
            : <PageDetail />}
        </div>
      </div>
    </main>
  );
}
