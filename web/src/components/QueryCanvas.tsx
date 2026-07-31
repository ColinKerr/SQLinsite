import { useEffect, useRef } from "react";
import { Profile } from "../core/profile.ts";
import { fetchQueryProfile } from "../core/queryApi.ts";
import type { ProfilePage } from "../core/types.ts";
import { createVizStore, useViz, type VizStore } from "../state/store.ts";
import { useQuery } from "../state/queryStore.ts";
import { CanvasHost } from "./CanvasHost.tsx";

// Renders the map's Pages/Tables grid overlaid with the *last query run's*
// profile, using a private store instance so it never disturbs the main view.
export function QueryCanvas({ sub }: { sub: "pages" | "tables" }) {
  const run = useQuery((s) => s.run);
  const meta = useViz((s) => s.meta);
  const minimap = useViz((s) => s.minimap);
  const storeRef = useRef<VizStore>();
  if (!storeRef.current) storeRef.current = createVizStore();
  const store = storeRef.current;

  useEffect(() => {
    if (!meta) return;
    let cancelled = false;
    // Force hasProfile so the overlay is always active for the run.
    const apply = (pages: ProfilePage[]) => {
      if (cancelled) return;
      store.getState().initFromMeta({ ...meta, hasProfile: true }, minimap, new Profile({ pages }));
      store.getState().setView(sub);
    };
    const inline = run?.profile.pages ?? [];
    if (run && inline.length === 0 && run.profileDeferred) {
      // The run response deferred a large profile — show the grid now and fetch it.
      apply([]);
      void fetchQueryProfile(run.queryId).then((p) => apply(p?.pages ?? []));
    } else {
      apply(inline);
    }
    return () => { cancelled = true; };
  }, [run, meta, minimap, sub, store]);

  if (!run) return <div className="results-msg muted">Run a query to see its page profile.</div>;
  return <div className="query-canvas"><CanvasHost store={store} /></div>;
}
