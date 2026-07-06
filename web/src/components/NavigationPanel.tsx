import type { ReactNode } from "react";
import { colorForObject, GLYPH, STRUCTURAL } from "../core/palette.ts";
import type { ObjectInfo } from "../core/types.ts";
import { useController } from "../state/ControllerContext.tsx";
import { useViz } from "../state/store.ts";

function ObjectRow({ o, child, onClick }: { o: ObjectInfo; child?: boolean; onClick: (id: number) => void }) {
  return (
    <div
      className={"legend-row clickable" + (child ? " child" : "")}
      title={`Go to ${o.name}`}
      onClick={() => onClick(o.id)}
    >
      <span className="swatch" style={{ background: colorForObject(o.id) }} />
      <span className="name">{o.name}</span>
      <span className="count">{o.pageCount.toLocaleString()}</span>
    </div>
  );
}

export function NavigationPanel() {
  const meta = useViz((s) => s.meta);
  const objects = useViz((s) => s.objects);
  const legendWidth = useViz((s) => s.legendWidth);
  const controller = useController();
  if (!meta) return null;

  const counts = new Map((meta.typeCounts || []).map((t) => [t.pageType, t.count] as const));

  // Tables are roots; their indexes (matched by tableName) nest as children.
  const indexesByTable = new Map<string, ObjectInfo[]>();
  for (const o of objects) {
    if (o.type === "table") continue;
    const key = o.tableName || "";
    (indexesByTable.get(key) ?? indexesByTable.set(key, []).get(key)!).push(o);
  }
  const claimed = new Set<number>();
  const go = (id: number) => void controller?.navigateToObject(id);

  const rows: ReactNode[] = [];
  for (const o of objects) {
    if (o.type !== "table") continue;
    rows.push(<ObjectRow key={`t${o.id}`} o={o} onClick={go} />);
    for (const idx of indexesByTable.get(o.name) || []) {
      rows.push(<ObjectRow key={`i${idx.id}`} o={idx} child onClick={go} />);
      claimed.add(idx.id);
    }
  }
  for (const o of objects) {
    if (o.type !== "table" && !claimed.has(o.id)) {
      rows.push(<ObjectRow key={`o${o.id}`} o={o} onClick={go} />);
    }
  }

  return (
    <aside id="legend" style={{ width: legendWidth }}>
      <h3>Page types</h3>
      {Object.keys(GLYPH).map((type) => {
        const n = counts.get(type) || 0;
        if (n === 0) return null;
        return (
          <div className="legend-row" key={type}>
            <span className="swatch" style={{ background: STRUCTURAL[type] || "#30343d" }}>
              {GLYPH[type]}
            </span>
            <span className="name">{type}</span>
            <span className="count">{n.toLocaleString()}</span>
          </div>
        );
      })}
      <h3>Tables &amp; indexes</h3>
      {rows}
    </aside>
  );
}
