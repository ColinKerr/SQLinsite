import { useViz } from "../state/store.ts";
import { formatCount } from "../core/format.ts";

// Session information → Map Information: total pages, and the number of distinct
// pages accessed by the current profile selection (unique pages, not a sum of
// accesses).
export function SessionInfo() {
  const pageCount = useViz((s) => s.pageCount);
  const hasProfile = useViz((s) => s.hasProfile);
  const profile = useViz((s) => s.profile);
  const metric = useViz((s) => s.metric);

  let text = `${formatCount(pageCount)} pages`;
  if (hasProfile) text += ` · ${formatCount(profile.identifiedPages(metric))} accessed`;

  return (
    <div className="bar-group" id="session-info">
      <span id="summary" title="Map pages · distinct pages accessed by the current profile selection">
        {text}
      </span>
    </div>
  );
}
