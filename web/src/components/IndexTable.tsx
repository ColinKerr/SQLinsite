import type { TreeObjectIndex } from "../core/types.ts";
import { formatBytes } from "../core/format.ts";
import { PageCard } from "./PageDetail.tsx";

// The indexes table shared by the Table Overview and Index Overview. Columns:
// Name / Pages / Size / Root, plus a Statement column (the CREATE INDEX SQL) when
// `showStatement` is set. Size is pages × pageSize.
export function IndexTable({ indexes, pageSize, showStatement, onNav }: {
  indexes: TreeObjectIndex[];
  pageSize: number;
  showStatement?: boolean;
  onNav: (p: number) => void;
}) {
  if (indexes.length === 0) return <div className="muted">No indexes.</div>;
  return (
    <table className="pd-coltable">
      <thead>
        <tr>
          <th>name</th><th>pages</th><th>size</th><th>root</th>
          {showStatement && <th>statement</th>}
        </tr>
      </thead>
      <tbody>
        {indexes.map((ix) => (
          <tr key={ix.name}>
            <td>{ix.name}</td>
            <td className="muted">{ix.pageCount ?? "—"}</td>
            <td className="muted">
              {ix.pageCount != null && pageSize > 0 ? formatBytes(ix.pageCount * pageSize) : "—"}
            </td>
            <td>{ix.rootPage != null ? <PageCard page={ix.rootPage} onClick={onNav} /> : "—"}</td>
            {showStatement && <td><code className="to-stmt">{ix.sql ?? "—"}</code></td>}
          </tr>
        ))}
      </tbody>
    </table>
  );
}
