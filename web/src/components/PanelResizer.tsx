import { useEffect, useRef } from "react";
import { useViz } from "../state/store.ts";

// The draggable divider between the view and the right-hand panel. Width is
// shared (legendWidth) and persisted, so it's consistent across views.
export function PanelResizer() {
  const ref = useRef<HTMLDivElement>(null);
  const setLegendWidth = useViz((s) => s.setLegendWidth);
  useEffect(() => {
    const resizer = ref.current!;
    let dragging = false;
    const onDown = (e: MouseEvent) => { dragging = true; e.preventDefault(); };
    const onMove = (e: MouseEvent) => {
      if (!dragging) return;
      const w = Math.max(140, Math.min(window.innerWidth * 0.6, window.innerWidth - e.clientX - 3));
      setLegendWidth(w);
    };
    const onUp = () => {
      if (!dragging) return;
      dragging = false;
      localStorage.setItem("legendWidth", String(useViz.getState().legendWidth));
    };
    resizer.addEventListener("mousedown", onDown);
    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
    return () => {
      resizer.removeEventListener("mousedown", onDown);
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
    };
  }, [setLegendWidth]);
  return <div id="resizer" ref={ref} title="Drag to resize" />;
}
