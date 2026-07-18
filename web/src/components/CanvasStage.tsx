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
  const setSelectedObject = useViz((s) => s.setSelectedObject);

  // Activating a node links it to the canvas without switching the content view: a
  // page node scrolls to and selects that page's block in the current view; a table
  // grouping node scrolls to the object (first leaf in the Pages view, its band in
  // Tables); an index grouping node does nothing.
  useRegisterNodeActivation(useMemo(
    () => makeCanvasActivate((p, objId, pt) => controller?.selectPageInView(p, objId, pt), setSelectedObject),
    [controller, setSelectedObject],
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
