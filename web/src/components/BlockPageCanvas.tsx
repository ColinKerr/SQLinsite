import { useEffect, useRef, useState } from "react";
import { BlockPageController } from "../viz/blockPageController.ts";
import { useViz } from "../state/store.ts";
import type { BlockColorMode, PageRow } from "../core/types.ts";

// The Block Detail View's page-level canvas: a mini Pages view (minimap, zoom,
// hover popup) bounded to the selected block's pages and colored by the active
// Block color mode. Driven by BlockPageController; the overlay (profile/metric)
// is mirrored from the shared store so it shades exactly like the other views.
export function BlockPageCanvas({ pages, startPage, mode, sharedWithParent }: {
  pages: PageRow[]; startPage: number; mode: BlockColorMode; sharedWithParent: boolean;
}) {
  const stageRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const miniRef = useRef<HTMLCanvasElement>(null);
  const popupRef = useRef<HTMLDivElement>(null);
  const ctrlRef = useRef<BlockPageController>();
  const [pct, setPct] = useState(30);

  const profile = useViz((s) => s.profile);
  const metric = useViz((s) => s.metric);
  const hasProfile = useViz((s) => s.hasProfile);
  const objById = useViz((s) => s.objById);

  useEffect(() => {
    const c = new BlockPageController(
      canvasRef.current!, miniRef.current!, popupRef.current!, stageRef.current!, setPct,
    );
    c.mount();
    ctrlRef.current = c;
    return () => { c.unmount(); ctrlRef.current = undefined; };
  }, []);

  useEffect(() => {
    ctrlRef.current?.setData({ pages, startPage, mode, sharedWithParent, profile, metric, hasProfile, objById });
  }, [pages, startPage, mode, sharedWithParent, profile, metric, hasProfile, objById]);

  return (
    <div className="block-pagecanvas">
      <div className="bar-group block-pc-zoom">
        <span title="Zoom level">{pct}%</span>
        <button title="Zoom out" onClick={() => ctrlRef.current?.zoomBy(-2)}>&minus;</button>
        <button title="Zoom in" onClick={() => ctrlRef.current?.zoomBy(2)}>+</button>
        <button title="Fit the block's pages" onClick={() => ctrlRef.current?.fit()}>Fit</button>
      </div>
      <div className="block-pc-body">
        <div className="block-pc-stage" ref={stageRef}>
          <canvas ref={canvasRef} />
          <div className="detail-popup" ref={popupRef} hidden />
        </div>
        <canvas className="block-pc-minimap" ref={miniRef} title="Click to scroll" />
      </div>
    </div>
  );
}
