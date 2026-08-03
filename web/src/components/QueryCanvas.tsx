import { useEffect, useRef } from "react";
import { createVizStore, useViz, type VizStore } from "../state/store.ts";
import { useQuery } from "../state/queryStore.ts";
import { CanvasHost } from "./CanvasHost.tsx";

// Renders the map's Pages/Tables grid overlaid with the shared profile overlay,
// using a private store instance so its view/scroll never disturbs the main view.
// The overlay is the unified selection (see store): after a run, that run's source
// is auto-selected, so this canvas shows the last run — and the same overlay is
// visible in the Pages/Tables views too.
export function QueryCanvas({ sub }: { sub: "pages" | "tables" }) {
  const run = useQuery((s) => s.run);
  const meta = useViz((s) => s.meta);
  const minimap = useViz((s) => s.minimap);
  const profile = useViz((s) => s.profile);
  const metric = useViz((s) => s.metric);
  const hasProfile = useViz((s) => s.hasProfile);
  const storeRef = useRef<VizStore>();
  if (!storeRef.current) storeRef.current = createVizStore();
  const store = storeRef.current;

  // Mirror the main store's overlay (profile/metric/hasProfile) and the requested
  // sub-view into the private store, so this canvas shades exactly like the others.
  useEffect(() => {
    if (!meta) return;
    store.getState().initFromMeta({ ...meta, hasProfile }, minimap, profile);
    store.setState({ metric, hasProfile });
    store.getState().setView(sub);
  }, [meta, minimap, profile, metric, hasProfile, sub, store]);

  if (!run) return <div className="results-msg muted">Run a query to see its page profile.</div>;
  return <div className="query-canvas"><CanvasHost store={store} /></div>;
}
