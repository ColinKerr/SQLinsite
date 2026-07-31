import { pageTypeDesc } from "../core/pageTypes.ts";
import { colorForPage, colorForPageNumber, GLYPH } from "../core/palette.ts";

// A small graphical page representation matching a b-tree tree node: type glyph +
// color + page number. It is the shared control for rendering page-id pointers
// (rightmostPointer, owned by, root, left child, overflow, …). Clicking invokes
// `onClick` — each view defines the link behavior (typically revealing the page
// in the b-tree tree).
export function PageCard({ page, pageType, label, colorByNumber, onClick }:
                  { page: number; pageType?: string; label?: string; colorByNumber?: boolean;
                    onClick: (p: number) => void }) {
  // `colorByNumber` gives each page a distinct color (used for overflow value
  // segments so the control matches its coloured slice of the value).
  const glyphBg = colorByNumber ? colorForPageNumber(page) : colorForPage(null, pageType ?? "");
  return (
    <button className="pgcard" onClick={() => onClick(page)} title={pageType ? pageTypeDesc(pageType) : `page ${page}`}>
      <span className="pgcard-glyph" style={{ background: glyphBg }}>
        {pageType ? (GLYPH[pageType] ?? "·") : "·"}
      </span>
      <span className="pgcard-num">{label ?? `p${page}`}</span>
    </button>
  );
}
