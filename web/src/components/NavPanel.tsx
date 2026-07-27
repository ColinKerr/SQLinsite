import { useEffect } from "react";
import { useViz } from "../state/store.ts";
import { useTree } from "../state/treeStore.ts";
import { BTreeTree } from "./BTreeTree.tsx";

// The shared left-hand Navigation Panel: a resizable column holding the one
// B-Tree Tree instance used by every view. Kept mounted across view switches so
// the tree (selection, expansion, scroll, search) is visually unchanged.
export function NavPanel() {
  const legendWidth = useViz((s) => s.legendWidth);
  const setLegendWidth = useViz((s) => s.setLegendWidth);
  const roots = useTree((s) => s.roots);
  const loadRoots = useTree((s) => s.loadRoots);
  useEffect(() => { if (!roots) void loadRoots(); }, [roots, loadRoots]);
  useEffect(() => {
    const saved = localStorage.getItem("legendWidth");
    if (saved) setLegendWidth(parseInt(saved, 10));
  }, [setLegendWidth]);

  return (
    <aside id="nav-panel" style={{ width: legendWidth }}>
      <BTreeTree />
    </aside>
  );
}
