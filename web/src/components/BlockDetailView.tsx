import { useEffect, useState } from "react";
import { fetchBlock, fetchPages, selParam } from "../core/api.ts";
import { useViz } from "../state/store.ts";
import { formatCount } from "../core/format.ts";
import { BlockPageCanvas } from "./BlockPageCanvas.tsx";
import type { BlockColorMode, BlockDetail, PageRow } from "../core/types.ts";

// Legend for the active color mode (nothing for Object, per BLOCK_VIEW.md).
function ColorKey({ mode }: { mode: BlockColorMode }) {
  if (mode === "object") return null;
  if (mode === "shared") {
    return (
      <div className="block-key">
        <span><i className="sw" style={{ background: "#e0a030" }} /> changed / new</span>
        <span><i className="sw" style={{ background: "#3b7dd8" }} /> shared with parent</span>
      </div>
    );
  }
  const grad = mode === "free"
    ? "linear-gradient(90deg, rgb(80,200,90), rgb(230,50,90))"
    : "linear-gradient(90deg, #2a2d35, #6ea8fe)";
  const lo = mode === "free" ? "used" : "not accessed";
  const hi = mode === "free" ? "free" : "most accessed";
  return (
    <div className="block-key">
      <span>{lo}</span>
      <i className="ramp" style={{ background: grad }} />
      <span>{hi}</span>
    </div>
  );
}

// The Block Detail View: a color-key row, a stats row, and a page-level canvas of
// the block's pages (colored by the active Block color mode). Empty until a block
// is selected.
export function BlockDetailView() {
  const selectedBlock = useViz((s) => s.selectedBlock);
  const mode = useViz((s) => s.blockColorMode);
  const hasProfile = useViz((s) => s.hasProfile);
  const metric = useViz((s) => s.metric);
  const selSources = useViz((s) => s.selSources);
  const sourceCount = useViz((s) => s.sourceCount);
  const [detail, setDetail] = useState<BlockDetail | null>(null);
  const [pages, setPages] = useState<PageRow[]>([]);

  useEffect(() => {
    if (selectedBlock == null) { setDetail(null); setPages([]); return; }
    let cancelled = false;
    const sel = selParam(selSources, sourceCount, hasProfile && metric !== "none");
    void fetchBlock(selectedBlock, sel).then((d) => {
      if (cancelled || !d) return;
      setDetail(d);
      void fetchPages(d.startPage, d.endPage).then((r) => {
        if (!cancelled) setPages(r?.pages ?? []);
      });
    });
    return () => { cancelled = true; };
  }, [selectedBlock, hasProfile, metric, selSources, sourceCount]);

  if (selectedBlock == null || !detail) {
    return <div className="results-msg muted">Click a block to see its detail.</div>;
  }
  const mainObj = detail.objectMix.find((o) => o.objectId != null);
  return (
    <div className="block-detail">
      <div className="block-key-row"><ColorKey mode={mode} /></div>
      <div className="block-stats">
        <b>Block {detail.blockIndex}</b>
        <span>{detail.objectName}</span>
        <span>pages {detail.startPage}–{detail.endPage} ({formatCount(detail.realPages)})</span>
        <span>used {formatCount(detail.usedPages)} · free {formatCount(detail.freePages)}</span>
        <span>{mainObj ? mainObj.name : "—"}</span>
        <span>{detail.sharedWithParent ? `shared w/ ${detail.parentName ?? "parent"}` : "new / changed"}</span>
        {detail.profile && <span>r/w {detail.profile.reads}/{detail.profile.writes}</span>}
      </div>
      <BlockPageCanvas pages={pages} startPage={detail.startPage} mode={mode}
                       sharedWithParent={detail.sharedWithParent} />
    </div>
  );
}
