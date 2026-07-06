import { useEffect, useRef } from "react";
import { CanvasController } from "../viz/controller.ts";
import { useControllerCtx } from "../state/ControllerContext.tsx";
import type { VizStore } from "../state/store.ts";

// Renders the stage/canvas/minimap/popup DOM and drives it with a CanvasController
// bound to `store`. Reused by the main Pages/Tables view (app store) and by the
// Query view's result canvas (a per-run store). When `publish` is set, the
// controller is exposed via context so the top bar's zoom controls can drive it.
export function CanvasHost({ store, publish }: { store: VizStore; publish?: boolean }) {
  const stageRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const minimapRef = useRef<HTMLCanvasElement>(null);
  const popupRef = useRef<HTMLDivElement>(null);
  const { setController } = useControllerCtx();

  useEffect(() => {
    const controller = new CanvasController(
      canvasRef.current!, minimapRef.current!, popupRef.current!, stageRef.current!, store,
    );
    controller.mount();
    if (publish) setController(controller);
    return () => { controller.unmount(); if (publish) setController(null); };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [store]);

  return (
    <>
      <div id="stage" ref={stageRef}>
        <canvas id="canvas" ref={canvasRef} />
        <div id="popup" ref={popupRef} hidden />
      </div>
      <canvas id="minimap" ref={minimapRef} title="Click to scroll" />
    </>
  );
}
