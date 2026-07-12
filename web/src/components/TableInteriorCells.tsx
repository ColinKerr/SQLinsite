import type { RefObject } from "react";
import type { PageContent } from "../core/types.ts";
import { PageCard } from "./PageDetail.tsx";

// The "Table Interior Cell control": table-interior pages hold no record data —
// each divider cell is just a rowid + a child pointer — so they render as a table
// of Cell (record) / Row Count / Bytes / Page / Row Ids rows (plus a final row for
// the header's rightmost pointer). Rows link to the Horizontal Schematic: hover
// highlights the matching cell block and a schematic click highlights the row.
export function TableInteriorCells({ content, hover, setHover, rowRefs, onNav, pointerType }: {
  content: PageContent;
  hover: number | null;
  setHover: (i: number | null) => void;
  rowRefs: RefObject<(HTMLElement | null)[]>;
  onNav: (p: number) => void;
  pointerType: (toPage: number) => string | undefined;
}) {
  const regionOf = (cellIndex: number) =>
    content.regions.findIndex((r) => r.cellIndex === cellIndex);
  const bytes = (offset: number, len: number) => `${offset}–${offset + len} (${len}B)`;
  // Rowids aren't contiguous (deletions leave gaps): render the runs as a mix of
  // ranges "s–e" and single ids "s", e.g. "1–4, 6, 11–60".
  const rowRanges = (ranges?: [number, number][]) =>
    ranges && ranges.length ? ranges.map(([s, e]) => (s === e ? `${s}` : `${s}–${e}`)).join(", ") : "—";

  const rightmost = content.header["rightmostPointer"] as number | undefined;
  const ph = content.regions.find((r) => r.kind === "page-header");
  const phIndex = content.regions.findIndex((r) => r.kind === "page-header");

  // A row bound to schematic region `ri`: hover/pin highlight + scroll target.
  const rowProps = (ri: number, extra = "") => ({
    ref: (el: HTMLTableRowElement | null) => { if (ri >= 0) rowRefs.current![ri] = el; },
    className: extra + (hover === ri ? " hi" : ""),
    onMouseEnter: () => setHover(ri),
    onMouseLeave: () => setHover(null),
  });

  return (
    <table className="pd-intcell">
      {content.rowidCapped && (
        <caption className="int-capped">
          Row ids/counts truncated — this page's subtree is too large to enumerate fully.
        </caption>
      )}
      <thead>
        <tr><th>Cell (record)</th><th>Row Count</th><th>Bytes</th><th>Page</th><th>Row Ids</th></tr>
      </thead>
      <tbody>
        {content.cells.map((c) => {
          const ri = regionOf(c.cellIndex);
          return (
            <tr key={c.cellIndex} {...rowProps(ri, "int-row")}>
              <td className="muted">{c.cellIndex}</td>
              <td>{c.rowidCount ?? "—"}</td>
              <td className="muted">{bytes(c.offset, c.size)}</td>
              <td>{c.leftChild != null &&
                <PageCard page={c.leftChild} pageType={pointerType(c.leftChild)} onClick={onNav} />}</td>
              <td className="int-rowids">{rowRanges(c.rowidRanges)}</td>
            </tr>
          );
        })}
        {rightmost != null && rightmost > 0 && (
          <tr {...rowProps(phIndex, "int-rightmost")}>
            <td className="muted">rightmost</td>
            <td>{content.rightmostRowids?.count ?? "—"}</td>
            <td className="muted">{ph ? bytes(ph.offset + ph.length - 4, 4) : "—"}</td>
            <td><PageCard page={rightmost} pageType={pointerType(rightmost)} onClick={onNav} /></td>
            <td className="int-rowids">{rowRanges(content.rightmostRowids?.ranges)}</td>
          </tr>
        )}
      </tbody>
    </table>
  );
}
