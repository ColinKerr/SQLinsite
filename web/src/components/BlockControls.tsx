import { useViz } from "../state/store.ts";
import type { BlockColorMode } from "../core/types.ts";

const MODES: { id: BlockColorMode; label: string }[] = [
  { id: "object", label: "Object" },
  { id: "free", label: "Used / free" },
  { id: "shared", label: "Changed / shared" },
  { id: "profile", label: "Profile" },
];

// Block-view color-mode selector (shown only in the Block view). Sets how each
// block cell is colored; see BLOCK_VIEW.md → Color modes.
export function BlockControls() {
  const view = useViz((s) => s.view);
  const mode = useViz((s) => s.blockColorMode);
  const setMode = useViz((s) => s.setBlockColorMode);
  const hasProfile = useViz((s) => s.hasProfile);
  if (view !== "blocks") return null;
  return (
    <div className="bar-group" id="block-controls">
      <span className="ctl-label">Color</span>
      {MODES.map((m) => {
        if (m.id === "profile" && !hasProfile) return null;
        return (
          <button
            key={m.id}
            className={"tab" + (mode === m.id ? " active" : "")}
            onClick={() => setMode(m.id)}
          >
            {m.label}
          </button>
        );
      })}
    </div>
  );
}
