import { useEffect } from "react";
import { useViz } from "../state/store.ts";
import { CanvasHost } from "./CanvasHost.tsx";
import { PanelResizer } from "./PanelResizer.tsx";
import { NavigationPanel } from "./NavigationPanel.tsx";

// Main Pages/Tables view: canvas + minimap (driven by the app store) and the
// Navigation panel.
export function CanvasStage() {
  const setLegendWidth = useViz((s) => s.setLegendWidth);
  useEffect(() => {
    const saved = localStorage.getItem("legendWidth");
    if (saved) setLegendWidth(parseInt(saved, 10));
  }, [setLegendWidth]);

  return (
    <main>
      <CanvasHost store={useViz} publish />
      <PanelResizer />
      <NavigationPanel />
    </main>
  );
}
