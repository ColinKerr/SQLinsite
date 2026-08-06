import { formatCount } from "../core/format.ts";
import { useQuery } from "../state/queryStore.ts";
import { SqlEditor } from "./SqlEditor.tsx";

// The Query view's editor: the shared SqlEditor wired to the query store, with the
// server-side run history (labeled by page count) and Explain.
export function QueryEditor() {
  const sql = useQuery((s) => s.sql);
  const setSql = useQuery((s) => s.setSql);
  const runCurrent = useQuery((s) => s.runCurrent);
  const running = useQuery((s) => s.running);
  const doExplain = useQuery((s) => s.doExplain);
  const history = useQuery((s) => s.history);
  const loadHistory = useQuery((s) => s.loadHistory);

  return (
    <SqlEditor
      sql={sql}
      onChange={setSql}
      onRun={() => void runCurrent()}
      running={running}
      onExplain={() => void doExplain()}
      history={history.map((h) => ({ id: h.id, sql: h.sql, meta: `${formatCount(h.pageCount)}p` }))}
      onSelectHistory={(id) => void loadHistory(id)}
    />
  );
}
