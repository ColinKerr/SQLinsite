import { useEffect, useRef, useState } from "react";
import { useHistory } from "../state/historyStore.ts";

// A Back/Forward split button: the main portion navigates one step; the caret
// opens a dropdown listing the entries in that direction (nearest first).
function HistoryButton({ dir }: { dir: "back" | "forward" }) {
  const entries = useHistory((s) => s.entries);
  const index = useHistory((s) => s.index);
  const back = useHistory((s) => s.back);
  const forward = useHistory((s) => s.forward);
  const jumpTo = useHistory((s) => s.jumpTo);
  const [open, setOpen] = useState(false);
  const ref = useRef<HTMLDivElement>(null);

  // The entry indices in this direction, nearest to current first.
  const targets = dir === "back"
    ? Array.from({ length: index }, (_, k) => index - 1 - k)
    : Array.from({ length: entries.length - 1 - index }, (_, k) => index + 1 + k);
  const enabled = targets.length > 0;

  useEffect(() => {
    if (!open) return;
    const onDoc = (e: MouseEvent) => { if (!ref.current?.contains(e.target as Node)) setOpen(false); };
    const onKey = (e: KeyboardEvent) => { if (e.key === "Escape") setOpen(false); };
    document.addEventListener("mousedown", onDoc);
    document.addEventListener("keydown", onKey);
    return () => { document.removeEventListener("mousedown", onDoc); document.removeEventListener("keydown", onKey); };
  }, [open]);

  const step = () => { if (dir === "back") back(); else forward(); };

  return (
    <div className="split-btn" ref={ref}>
      <button className="split-main" onClick={step} disabled={!enabled}
              title={dir === "back" ? "Back" : "Forward"} aria-label={dir}>
        {dir === "back" ? "‹" : "›"}
      </button>
      <button className="split-caret" onClick={() => enabled && setOpen((o) => !o)} disabled={!enabled}
              title={`${dir === "back" ? "Back" : "Forward"} history`} aria-label={`${dir} history`}>
        ▾
      </button>
      {open && enabled && (
        <div className="hist-menu">
          {targets.map((i) => (
            <button key={i} className="hist-item" onClick={() => { jumpTo(i); setOpen(false); }}>
              {entries[i].label}
            </button>
          ))}
        </div>
      )}
    </div>
  );
}

export function HistoryNav() {
  return (
    <div className="bar-group hist-nav">
      <HistoryButton dir="back" />
      <HistoryButton dir="forward" />
    </div>
  );
}
