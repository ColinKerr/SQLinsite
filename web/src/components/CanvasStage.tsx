import { useEffect } from "react";
import { useViz } from "../state/store.ts";
import { CanvasHost } from "./CanvasHost.tsx";
import { PanelResizer } from "./PanelResizer.tsx";
import { NavigationPanel } from "./NavigationPanel.tsx";
import { PageTopBar } from "./PageTopBar.tsx";

// Main Pages/Tables view: the Page Top Bar (zoom/profile/session) over the canvas
// + minimap (driven by the app store) and the Navigation panel.
export function CanvasStage() {
  const setLegendWidth = useViz((s) => s.setLegendWidth);
  useEffect(() => {
    const saved = localStorage.getItem("legendWidth");
    if (saved) setLegendWidth(parseInt(saved, 10));
  }, [setLegendWidth]);

  return (
    <main>
      <div className="view-area">
        <PageTopBar />
        <div className="view-body">
          <CanvasHost store={useViz} publish />
        </div>
      </div>
      <PanelResizer />
      <NavigationPanel />
    </main>
  );
}
