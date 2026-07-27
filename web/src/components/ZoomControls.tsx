import { MAX_BLOCK_PX } from "../core/constants.ts";
import { useController } from "../state/ControllerContext.tsx";
import { useViz } from "../state/store.ts";

export function ZoomControls() {
  const blockPx = useViz((s) => s.blockPx);
  const controller = useController();
  const pct = Math.round((100 * blockPx) / MAX_BLOCK_PX) + "%";
  return (
    <div className="bar-group" id="zoom-controls">
      <span id="zoom-pct" title="Zoom level (current / max block size)">{pct}</span>
      <button id="zoom-out" title="Zoom out" onClick={() => controller?.zoomBy(-2)}>&minus;</button>
      <button id="zoom-in" title="Zoom in" onClick={() => controller?.zoomBy(2)}>+</button>
      <button id="zoom-fit" title="Fit the whole view" onClick={() => controller?.fit()}>Fit</button>
    </div>
  );
}
