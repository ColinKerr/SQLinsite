import { useViz } from "../state/store.ts";
import { ZoomControls } from "./ZoomControls.tsx";
import { formatBytes, formatCount } from "../core/format.ts";
import type { BlockColorMode } from "../core/types.ts";

const MODES: { id: BlockColorMode; label: string }[] = [
  { id: "object", label: "Object" },
  { id: "free", label: "Used / free" },
  { id: "shared", label: "Changed / shared" },
  { id: "profile", label: "Profile" },
];

// The Block view's fixed top bar: color-mode selector, zoom controls, and db stats
// (see BLOCK_VIEW.md). Shown only inside the Block view layout.
export function BlockControls() {
  const mode = useViz((s) => s.blockColorMode);
  const setMode = useViz((s) => s.setBlockColorMode);
  const hasProfile = useViz((s) => s.hasProfile);
  const blockCount = useViz((s) => s.blockCount);
  const pagesPerBlock = useViz((s) => s.pagesPerBlock);
  const dbName = useViz((s) => (s.meta?.manifestDbName as string) ?? "");
  const blockSize = useViz((s) => (s.meta?.blockSize as number) ?? 0);

  return (
    <div className="block-controls">
      <div className="bar-group" id="block-color">
        <span className="ctl-label">Color</span>
        {MODES.map((m) => {
          if (m.id === "profile" && !hasProfile) return null;
          return (
            <button key={m.id} className={"tab" + (mode === m.id ? " active" : "")}
                    onClick={() => setMode(m.id)}>
              {m.label}
            </button>
          );
        })}
      </div>
      <div className="bar-spacer" />
      <ZoomControls />
      <div className="bar-group block-stats" title={dbName}>
        <span>{formatCount(blockCount)} blocks</span>
        <span>· {pagesPerBlock} pages/block</span>
        <span>· {formatBytes(blockSize)} each</span>
      </div>
    </div>
  );
}
