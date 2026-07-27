import { useQuery } from "../state/queryStore.ts";

// Shown in the results area while a query is running (from pressing Run until the
// results return): a spinner plus a Cancel button that stops the in-flight query.
export function RunningIndicator() {
  const cancelRun = useQuery((s) => s.cancelRun);
  return (
    <div className="results-running">
      <div className="spinner" aria-hidden="true" />
      <div className="results-running-text">Running query…</div>
      <button className="qbtn results-cancel" onClick={() => cancelRun()}>Cancel</button>
    </div>
  );
}
