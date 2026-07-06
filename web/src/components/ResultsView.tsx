import { useQuery, type ResultsTab } from "../state/queryStore.ts";
import { ResultsTable } from "./ResultsTable.tsx";
import { ExplainResults } from "./ExplainResults.tsx";
import { QueryCanvas } from "./QueryCanvas.tsx";

const TABS: { id: ResultsTab; label: string }[] = [
  { id: "table", label: "Results" },
  { id: "pages", label: "Pages" },
  { id: "tables", label: "Tables" },
];

export function ResultsView() {
  const tab = useQuery((s) => s.resultsTab);
  const setTab = useQuery((s) => s.setResultsTab);
  return (
    <div id="results-view">
      <div className="results-tabs">
        {TABS.map((t) => (
          <button key={t.id} className={"rtab" + (tab === t.id ? " active" : "")}
                  onClick={() => setTab(t.id)}>
            {t.label}
          </button>
        ))}
        {/* Explain is a hidden tab: it only appears (and activates) via the Explain button. */}
        {tab === "explain" && <button className="rtab active">Explain</button>}
      </div>
      <div className="results-body">
        {tab === "table" && <ResultsTable />}
        {tab === "pages" && <QueryCanvas sub="pages" />}
        {tab === "tables" && <QueryCanvas sub="tables" />}
        {tab === "explain" && <ExplainResults />}
      </div>
    </div>
  );
}
