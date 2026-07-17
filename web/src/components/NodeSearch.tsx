import { useEffect, useRef, useState } from "react";
import { fetchTreeSearch } from "../core/api.ts";
import { searchNode, type TreeNode } from "../core/treeModel.ts";
import { useTree } from "../state/treeStore.ts";
import { TreeNodeContent } from "./TreeNodeContent.tsx";

const MIN_DIGITS = 1; // dropdown appears once this many digits are entered
const LIMIT = 20;

// The Node Search bar (fixed at the top of the b-tree tree): finds pages by page
// number. After 3+ digits a dropdown lists matches — rendered with the same node
// control as the tree — with the top match selected by default. Arrow keys move
// the selection; Enter or a click jumps to (reveals) that node and closes.
export function NodeSearch({ pageSize }: { pageSize: number }) {
  const revealPage = useTree((s) => s.revealPage);
  const [query, setQuery] = useState("");
  const [matches, setMatches] = useState<TreeNode[]>([]);
  const [open, setOpen] = useState(false);
  const [active, setActive] = useState(0);
  const boxRef = useRef<HTMLDivElement>(null);

  // Fetch matches (debounced) once the query holds enough digits; else clear.
  useEffect(() => {
    const digits = query.replace(/\D/g, "");
    if (digits.length < MIN_DIGITS) { setMatches([]); setOpen(false); return; }
    let cancelled = false;
    const t = setTimeout(async () => {
      const r = await fetchTreeSearch(digits, LIMIT);
      if (cancelled) return;
      const nodes = (r?.matches ?? []).map(searchNode);
      setMatches(nodes);
      setActive(0);            // highest (best) match selected by default
      setOpen(nodes.length > 0);
    }, 150);
    return () => { cancelled = true; clearTimeout(t); };
  }, [query]);

  // Close the dropdown on an outside click.
  useEffect(() => {
    if (!open) return;
    const onDoc = (e: MouseEvent) => {
      if (boxRef.current && !boxRef.current.contains(e.target as Node)) setOpen(false);
    };
    document.addEventListener("mousedown", onDoc);
    return () => document.removeEventListener("mousedown", onDoc);
  }, [open]);

  const jump = (node: TreeNode) => {
    if (node.page != null) void revealPage(node.page);
    setOpen(false);
  };

  const onKeyDown = (e: React.KeyboardEvent) => {
    if (!open || matches.length === 0) return;
    if (e.key === "ArrowDown") { e.preventDefault(); setActive((a) => Math.min(a + 1, matches.length - 1)); }
    else if (e.key === "ArrowUp") { e.preventDefault(); setActive((a) => Math.max(a - 1, 0)); }
    else if (e.key === "Enter") { e.preventDefault(); jump(matches[active]); }
    else if (e.key === "Escape") { setOpen(false); }
  };

  return (
    <div className="btree-search" ref={boxRef}>
      <input className="btree-search-input" type="text" inputMode="numeric"
             placeholder="Find page by number…" value={query} aria-label="Find page by number"
             onChange={(e) => setQuery(e.target.value)}
             onKeyDown={onKeyDown}
             onFocus={() => { if (matches.length) setOpen(true); }} />
      {open && (
        <div className="btree-search-menu" role="listbox">
          {matches.map((node, i) => (
            <div key={node.key} role="option" aria-selected={i === active}
                 className={"tn-row btree-search-item" + (i === active ? " active" : "")}
                 onMouseEnter={() => setActive(i)}
                 // mousedown (not click) so it fires before the input blurs.
                 onMouseDown={(e) => { e.preventDefault(); jump(node); }}>
              <TreeNodeContent node={node} pageSize={pageSize} />
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
