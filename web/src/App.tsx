import { useEffect, useState } from "react";
import { fetchMeta, fetchProfilePages, fetchRuns } from "./core/api.ts";
import { Profile } from "./core/profile.ts";
import { ControllerProvider } from "./state/ControllerContext.tsx";
import { useViz } from "./state/store.ts";
import { TopBar } from "./components/TopBar.tsx";
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
      const pageCount = meta.meta.pageCount;
      const runs = await fetchRuns(1, pageCount); // whole-file run map for the minimap
      let profile = Profile.empty();
      if (meta.hasProfile) {
        const data = await fetchProfilePages(1, pageCount, ""); // all leaves
        profile = new Profile(data ?? { pages: [] });
      }
      initFromMeta(meta, runs?.runs ?? [], profile);
      setReady(true);
    })();
  }, [initFromMeta]);

  if (error) return <div style={{ padding: 16, color: "#e15759" }}>{error}</div>;
  if (!ready) return <div style={{ padding: 16, color: "#9aa0aa" }}>Loading…</div>;

  return (
    <ControllerProvider>
      <TopBar />
      {view === "query" ? <QueryLayout />
        : view === "tree" ? <PageTreeView />
        : <CanvasStage />}
    </ControllerProvider>
  );
}
