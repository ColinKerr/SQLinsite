import { useViz } from "../state/store.ts";
import { CanvasHost } from "./CanvasHost.tsx";
import { PageTopBar } from "./PageTopBar.tsx";

// The Pages/Tables view content (to the right of the shared Navigation Panel): the
// Page Top Bar (zoom/profile/session) over the canvas + minimap.
export function CanvasStage() {
  return (
    <div className="view-area">
      <PageTopBar />
      <div className="view-body">
        <CanvasHost store={useViz} publish />
      </div>
    </div>
  );
}
