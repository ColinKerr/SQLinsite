// Shown in the results area while a query is running (from pressing Run until the
// results return): a spinner plus, when a cancel handler is supplied, a Cancel
// button that stops the in-flight query. Analysis meta-queries omit Cancel.
export function RunningIndicator({ onCancel, label = "Running query…" }: {
  onCancel?: () => void;
  label?: string;
}) {
  return (
    <div className="results-running">
      <div className="spinner" aria-hidden="true" />
      <div className="results-running-text">{label}</div>
      {onCancel && <button className="qbtn results-cancel" onClick={onCancel}>Cancel</button>}
    </div>
  );
}
