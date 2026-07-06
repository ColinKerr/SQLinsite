import { useEffect, useRef } from "react";
import { Profile } from "../core/profile.ts";
import { createVizStore, useViz, type VizStore } from "../state/store.ts";
import { useQuery } from "../state/queryStore.ts";
import { CanvasHost } from "./CanvasHost.tsx";

// Renders the map's Pages/Tables grid overlaid with the *last query run's*
// profile, using a private store instance so it never disturbs the main view.
export function QueryCanvas({ sub }: { sub: "pages" | "tables" }) {
  const run = useQuery((s) => s.run);
  const meta = useViz((s) => s.meta);
  const allRuns = useViz((s) => s.allRuns);
  const storeRef = useRef<VizStore>();
  if (!storeRef.current) storeRef.current = createVizStore();
  const store = storeRef.current;

  useEffect(() => {
    if (!meta) return;
    const profile = new Profile({ pages: run?.profile.pages ?? [] });
    // Force hasProfile so the overlay is always active for the run.
    store.getState().initFromMeta({ ...meta, hasProfile: true }, allRuns, profile);
    store.getState().setView(sub);
  }, [run, meta, allRuns, sub, store]);

  if (!run) return <div className="results-msg muted">Run a query to see its page profile.</div>;
  return <div className="query-canvas"><CanvasHost store={store} /></div>;
}
