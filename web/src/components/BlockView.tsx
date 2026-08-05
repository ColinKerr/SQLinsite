import { useMemo, useRef, useState } from "react";
import { useViz } from "../state/store.ts";
import { useController } from "../state/ControllerContext.tsx";
import { useRegisterNodeActivation } from "../state/treeSelectionStore.ts";
import { makeCanvasActivate } from "./nodeActivation.ts";
import { CanvasHost } from "./CanvasHost.tsx";
import { BlockControls } from "./BlockControls.tsx";
import { BlockDetailView } from "./BlockDetailView.tsx";

// The Block view content (right of the shared B-Tree Tree): a fixed controls bar,
// the block canvas (2/3), an adjustable divider, and the Block Detail View (1/3).
export function BlockView() {
  const controller = useController();
  const setSelectedObject = useViz((s) => s.setSelectedObject);

  // Tree node activation links into the block canvas (a page node scrolls to its
  // block; see CanvasController.selectPageInView), same as the Pages/Tables views.
  useRegisterNodeActivation(useMemo(
    () => makeCanvasActivate((p, objId, pt) => controller?.selectPageInView(p, objId, pt), setSelectedObject),
    [controller, setSelectedObject],
  ));

  const [detailPct, setDetailPct] = useState(33);
  const areaRef = useRef<HTMLDivElement>(null);
  const startDrag = (e: React.MouseEvent) => {
    e.preventDefault();
    const onMove = (ev: MouseEvent) => {
      const rect = areaRef.current?.getBoundingClientRect();
      if (!rect) return;
      const pct = ((rect.bottom - ev.clientY) / rect.height) * 100;
      setDetailPct(Math.max(15, Math.min(70, pct)));
    };
    const onUp = () => {
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
    };
    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  };

  return (
    <div className="view-area" id="block-layout">
      <BlockControls />
      <div className="block-split" ref={areaRef}>
        <div className="view-body" style={{ height: `${100 - detailPct}%` }}>
          <CanvasHost store={useViz} publish />
        </div>
        <div className="block-divider" title="Drag to resize" onMouseDown={startDrag} />
        <div className="block-detail-pane" style={{ height: `${detailPct}%` }}>
          <BlockDetailView />
        </div>
      </div>
    </div>
  );
}
