import { useMemo } from "react";
import { useViz } from "../state/store.ts";
import { useController } from "../state/ControllerContext.tsx";
import { useRegisterNodeActivation } from "../state/treeSelectionStore.ts";
import { makeCanvasActivate } from "./nodeActivation.ts";
import { CanvasHost } from "./CanvasHost.tsx";
import { PageTopBar } from "./PageTopBar.tsx";

// The Pages/Tables view content (to the right of the shared Navigation Panel): the
// Page Top Bar (zoom/profile/session) over the canvas + minimap.
export function CanvasStage() {
  const controller = useController();
  const view = useViz((s) => s.view);
  const setSelectedObject = useViz((s) => s.setSelectedObject);

  // Activating a node scrolls the canvas to it.
  useRegisterNodeActivation(useMemo(
    () => makeCanvasActivate(view, (p) => controller?.goToPage(p), setSelectedObject),
    [view, controller, setSelectedObject],
  ));

  return (
    <div className="view-area">
      <PageTopBar />
      <div className="view-body">
        <CanvasHost store={useViz} publish />
      </div>
    </div>
  );
}
