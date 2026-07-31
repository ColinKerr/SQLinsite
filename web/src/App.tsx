import { useEffect, useState } from "react";
import { fetchMeta, fetchMinimap, fetchProfilePages } from "./core/api.ts";
import { formatVersionError } from "./core/version.ts";
import { perfMark, perfReady } from "./core/perf.ts";
import { Profile } from "./core/profile.ts";
import { ControllerProvider } from "./state/ControllerContext.tsx";
import { useViz } from "./state/store.ts";
import { initHistory, useHistory } from "./state/historyStore.ts";
import { TopBar } from "./components/TopBar.tsx";
import { NavPanel } from "./components/NavPanel.tsx";
import { PanelResizer } from "./components/PanelResizer.tsx";
import { CanvasStage } from "./components/CanvasStage.tsx";
import { QueryLayout } from "./components/QueryLayout.tsx";
import { PageTreeView } from "./components/PageTreeView.tsx";

export default function App() {
  const [ready, setReady] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const initFromMeta = useViz((s) => s.initFromMeta);
  const view = useViz((s) => s.view);

  useEffect(() => {
    (async () => {
      const meta = await fetchMeta();
      if (!meta) { setError("Failed to load /api/meta"); return; }
      perfMark("meta-loaded");
      // Refuse a map whose format version this build can't read (older or newer).
      const verr = formatVersionError(meta.meta.formatVersion, meta.expectedFormatVersion);
      if (verr) { setError(verr); return; }
      const pageCount = meta.meta.pageCount;
      // Fetch the independent boot data in parallel: the downsampled object-colored
      // minimap (a few KB, not the whole-file run map) and, if present, the profile
      // overlay. The Tables view's structural-page bands are loaded lazily on first
      // entry (see store), so that heavy query no longer blocks initial load.
      const [minimap, profileData] = await Promise.all([
        fetchMinimap(),
        meta.hasProfile ? fetchProfilePages(1, pageCount, "") : Promise.resolve(null),
      ]);
      perfMark("minimap-loaded");
      const profile = meta.hasProfile ? new Profile(profileData ?? { pages: [] }) : Profile.empty();
      initFromMeta(meta, minimap?.buckets ?? [], profile);
      // History navigation: subscribe to nav changes, seed the stack from
      // localStorage, then restore the position from the URL (or the last one).
      initHistory();
      useHistory.getState().hydrate();
      useHistory.getState().restoreFromUrl();
      setReady(true);
    })();
  }, [initFromMeta]);

  // Signal the perf harness once the first view has actually painted.
  useEffect(() => {
    if (ready) requestAnimationFrame(() => perfReady());
  }, [ready]);

  if (error) return <div style={{ padding: 16, color: "#e15759" }}>{error}</div>;
  if (!ready) return <div style={{ padding: 16, color: "#9aa0aa" }}>Loading…</div>;

  return (
    <ControllerProvider>
      <TopBar />
      <main>
        {/* One shared B-Tree Tree on the left for every view; the resizer and the
            view-specific content follow it. */}
        <NavPanel />
        <PanelResizer />
        <div className="view-main">
          {view === "query" ? <QueryLayout />
            : view === "tree" ? <PageTreeView />
            : <CanvasStage />}
        </div>
      </main>
    </ControllerProvider>
  );
}
