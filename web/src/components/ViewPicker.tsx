import { useViz } from "../state/store.ts";

export function ViewPicker() {
  const view = useViz((s) => s.view);
  const setView = useViz((s) => s.setView);
  const hasDb = useViz((s) => s.meta?.hasDb ?? false);
  const hasBlocks = useViz((s) => s.hasManifest && s.manifestMatch);
  return (
    <nav className="bar-group" id="view-picker">
      <button className={"tab" + (view === "pages" ? " active" : "")} onClick={() => setView("pages")}>
        Pages
      </button>
      <button className={"tab" + (view === "tables" ? " active" : "")} onClick={() => setView("tables")}>
        Tables
      </button>
      {hasDb && (
        <button className={"tab" + (view === "query" ? " active" : "")} onClick={() => setView("query")}>
          Query
        </button>
      )}
      {hasDb && (
        <button className={"tab" + (view === "tree" ? " active" : "")} onClick={() => setView("tree")}>
          Tree
        </button>
      )}
      {hasBlocks && (
        <button className={"tab" + (view === "blocks" ? " active" : "")} onClick={() => setView("blocks")}>
          Blocks
        </button>
      )}
      <button className={"tab" + (view === "analysis" ? " active" : "")} onClick={() => setView("analysis")}>
        Analysis
      </button>
    </nav>
  );
}
