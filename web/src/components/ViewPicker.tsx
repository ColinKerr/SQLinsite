import { useViz } from "../state/store.ts";

export function ViewPicker() {
  const view = useViz((s) => s.view);
  const setView = useViz((s) => s.setView);
  const hasDb = useViz((s) => s.meta?.hasDb ?? false);
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
    </nav>
  );
}
