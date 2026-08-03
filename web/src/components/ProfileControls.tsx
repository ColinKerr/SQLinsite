import { useEffect, useRef, useState } from "react";
import { useViz } from "../state/store.ts";
import type { Metric, ProfileSource } from "../core/types.ts";

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

// Groups the flat source list into the tree the control renders: one node per
// input session (its statements), then a "Queries" node listing interactive runs.
interface Group { label: string; items: { id: number; label: string }[] }
function groupSources(sources: ProfileSource[]): Group[] {
  const groups: Group[] = [];
  let session: Group | null = null;
  let queries: Group | null = null;
  for (const s of sources) {
    if (s.kind === "input") {
      if (!session || session.label !== s.sessionName) {
        session = { label: s.sessionName, items: [] };
        groups.push(session);
      }
      session.items.push({ id: s.sourceId, label: `stmt ${s.sessionId}` });
    } else {
      if (!queries) { queries = { label: "Queries", items: [] }; groups.push(queries); }
      const sql = s.sessionName.replace(/\s+/g, " ").trim();
      queries.items.push({ id: s.sourceId, label: sql.length > 40 ? sql.slice(0, 40) + "…" : sql });
    }
  }
  return groups;
}

export function ProfileControls() {
  const hasProfile = useViz((s) => s.hasProfile);
  const metric = useViz((s) => s.metric);
  const setMetric = useViz((s) => s.setMetric);
  const sources = useViz((s) => s.sources);
  const selSources = useViz((s) => s.selSources);
  const setSources = useViz((s) => s.setSources);
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

  const apply = (next: Set<number>) => {
    setSources(next);
    void reloadProfile();
  };
  const toggleSource = (id: number) => {
    const next = new Set(selSources);
    if (next.has(id)) next.delete(id); else next.add(id);
    apply(next);
  };
  const toggleGroup = (ids: number[]) => {
    const allOn = ids.every((id) => selSources.has(id));
    const next = new Set(selSources);
    for (const id of ids) { if (allOn) next.delete(id); else next.add(id); }
    apply(next);
  };
  const groups = groupSources(sources);

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
                {groups.map((g) => {
                  const ids = g.items.map((i) => i.id);
                  const on = ids.filter((id) => selSources.has(id)).length;
                  return (
                    <div className="session" key={g.label}>
                      <SessionCheckbox
                        checked={on === ids.length}
                        indeterminate={on > 0 && on < ids.length}
                        onChange={() => toggleGroup(ids)}
                        label={g.label}
                      />
                      <div className="stmts">
                        {g.items.map((i) => (
                          <label key={i.id} title={i.label}>
                            <input type="checkbox" className="leaf"
                              checked={selSources.has(i.id)}
                              onChange={() => toggleSource(i.id)} />
                            {i.label}
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
