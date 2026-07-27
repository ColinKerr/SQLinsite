import { useEffect, useRef, useState } from "react";
import { useViz } from "../state/store.ts";
import type { Metric } from "../core/types.ts";

function metricFrom(reads: boolean, writes: boolean): Metric {
  return reads && writes ? "total" : reads ? "reads" : writes ? "writes" : "none";
}

// Parent session checkbox: checked when all its leaves are selected, indeterminate
// when only some are. React has no `indeterminate` prop, so set it via ref.
function SessionCheckbox({ checked, indeterminate, onChange, label }: {
  checked: boolean; indeterminate: boolean; onChange: () => void; label: string;
}) {
  const ref = useRef<HTMLInputElement>(null);
  useEffect(() => { if (ref.current) ref.current.indeterminate = indeterminate; }, [indeterminate]);
  return (
    <label>
      <input ref={ref} type="checkbox" className="ses" checked={checked} onChange={onChange} />
      <span className="name">{label}</span>
    </label>
  );
}

export function ProfileControls() {
  const hasProfile = useViz((s) => s.hasProfile);
  const metric = useViz((s) => s.metric);
  const setMetric = useViz((s) => s.setMetric);
  const sessions = useViz((s) => s.sessions);
  const selLeaves = useViz((s) => s.selLeaves);
  const setLeaves = useViz((s) => s.setLeaves);
  const reloadProfile = useViz((s) => s.reloadProfile);

  const [open, setOpen] = useState(false);
  const rootRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!open) return;
    const onDoc = (e: MouseEvent) => {
      if (rootRef.current && !rootRef.current.contains(e.target as Node)) setOpen(false);
    };
    document.addEventListener("click", onDoc);
    return () => document.removeEventListener("click", onDoc);
  }, [open]);

  if (!hasProfile) return null;

  const reads = metric === "reads" || metric === "total";
  const writes = metric === "writes" || metric === "total";

  const applyLeaves = (next: Set<number>) => {
    setLeaves(next);
    void reloadProfile();
  };
  const toggleLeaf = (id: number) => {
    const next = new Set(selLeaves);
    if (next.has(id)) next.delete(id); else next.add(id);
    applyLeaves(next);
  };
  const toggleSession = (ids: number[]) => {
    const allOn = ids.every((id) => selLeaves.has(id));
    const next = new Set(selLeaves);
    for (const id of ids) { if (allOn) next.delete(id); else next.add(id); }
    applyLeaves(next);
  };

  return (
    <div className="bar-group" id="profile-controls" ref={rootRef}>
      <div className="dropdown">
        <button
          id="profile-toggle"
          className="dropdown-btn"
          aria-expanded={open}
          onClick={() => setOpen((o) => !o)}
        >
          Overlay &#9662;
        </button>
        {open && (
          <div id="profile-panel" className="dropdown-panel">
            <div className="dd-section">
              <div className="dd-title">Show</div>
              <label>
                <input type="checkbox" id="metric-reads" checked={reads}
                  onChange={(e) => setMetric(metricFrom(e.target.checked, writes))} /> reads
              </label>
              <label>
                <input type="checkbox" id="metric-writes" checked={writes}
                  onChange={(e) => setMetric(metricFrom(reads, e.target.checked))} /> writes
              </label>
            </div>
            <div className="dd-section">
              <div className="dd-title">Sessions &amp; queries</div>
              <div id="sessions">
                {sessions.map((s) => {
                  const ids = s.leaves.map((l) => l.leafId);
                  const on = ids.filter((id) => selLeaves.has(id)).length;
                  return (
                    <div className="session" key={s.session}>
                      <SessionCheckbox
                        checked={on === ids.length}
                        indeterminate={on > 0 && on < ids.length}
                        onChange={() => toggleSession(ids)}
                        label={s.session}
                      />
                      <div className="stmts">
                        {s.leaves.map((l) => (
                          <label key={l.leafId}>
                            <input type="checkbox" className="leaf"
                              checked={selLeaves.has(l.leafId)}
                              onChange={() => toggleLeaf(l.leafId)} />
                            stmt {l.statementIndex}
                          </label>
                        ))}
                      </div>
                    </div>
                  );
                })}
              </div>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
