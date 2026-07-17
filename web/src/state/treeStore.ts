import { create } from "zustand";
import {
  fetchPageContent, fetchTreeChildren, fetchTreeFreelist, fetchTreeObject, fetchTreeOther,
  fetchTreePath, fetchTreeRoots,
} from "../core/api.ts";
import type { PageContent, TreeObjectOverview } from "../core/types.ts";
import {
  btreeChildNode, childNode, indexesGroupNode, moreNode, rootNode, type TreeNode,
} from "../core/treeModel.ts";

const WINDOW = 1000; // page window for the freelist / "all other pages" nodes

export interface TreeState {
  roots: TreeNode[] | null;
  childrenByKey: Record<string, TreeNode[]>;
  expanded: Set<string>;
  loading: Set<string>;
  selectedPage: number | null;
  selectedKey: string | null;   // selected non-page node (table/indexes) for highlight
  content: PageContent | null;
  overview: TreeObjectOverview | null;  // table/index overview for the selected grouping node
  overviewMode: "table" | "index" | null; // which overview view to render
  contentLoading: boolean;

  loadRoots(): Promise<void>;
  toggle(node: TreeNode): Promise<void>;
  loadMore(node: TreeNode): Promise<void>; // a "more" loader row
  selectPage(page: number): Promise<void>;
  selectTable(objectId: number, key: string): Promise<void>;   // table node → Table Overview
  selectIndexes(objectId: number, key: string): Promise<void>; // Indexes node → Index Overview
  highlightNode(node: TreeNode): void; // set the shared selection highlight, no detail fetch
  revealPage(page: number): Promise<void>; // select + expand the tree down to it
}

// Selects a grouping node and loads its object overview into the right pane,
// tagged with the view mode (Table vs Index Overview). Ignores a late response if
// the selection moved on.
async function loadOverview(
  set: (partial: Partial<TreeState>) => void,
  get: () => TreeState,
  objectId: number, key: string, mode: "table" | "index",
): Promise<void> {
  set({ selectedKey: key, selectedPage: null, content: null, overview: null,
        overviewMode: mode, contentLoading: true });
  const r = await fetchTreeObject(objectId);
  if (get().selectedKey === key) set({ overview: r?.overview ?? null, contentLoading: false });
}

// Fetches a node's children (page b-tree children, or a window of the freelist /
// other virtual roots), appending a "load more" row when a window is full.
async function fetchChildren(node: TreeNode, after = 0): Promise<TreeNode[]> {
  if (node.kind === "page" && node.page != null) {
    const r = await fetchTreeChildren(node.page);
    return (r?.children ?? []).map((c) => childNode(node.key, c));
  }
  // Grouping nodes build children from the inlined roots payload — no fetch.
  if (node.kind === "table") {
    const kids: TreeNode[] = [];
    if (node.tableBtree) kids.push(btreeChildNode(node.key, node.tableBtree, "table"));
    if ((node.indexes?.length ?? 0) > 0)
      kids.push(indexesGroupNode(node.key, node.objectId, node.indexes!));
    return kids;
  }
  if (node.kind === "indexes") {
    return (node.indexes ?? []).map((b) => btreeChildNode(node.key, b, "index"));
  }
  if (node.kind === "freelist") {
    const r = await fetchTreeFreelist(after, WINDOW);
    const kids = (r?.pages ?? []).map((c) => childNode(node.key, c, "freelist-trunk"));
    if (kids.length === WINDOW) kids.push(moreNode(node.key, "freelist", kids[kids.length - 1].page!));
    return kids;
  }
  if (node.kind === "other") {
    const r = await fetchTreeOther(after, WINDOW);
    const kids = (r?.pages ?? []).map((c) => childNode(node.key, c));
    if (kids.length === WINDOW) kids.push(moreNode(node.key, "other", kids[kids.length - 1].page!));
    return kids;
  }
  return [];
}

export const useTree = create<TreeState>((set, get) => ({
  roots: null,
  childrenByKey: {},
  expanded: new Set(),
  loading: new Set(),
  selectedPage: null,
  selectedKey: null,
  content: null,
  overview: null,
  overviewMode: null,
  contentLoading: false,

  async loadRoots() {
    const r = await fetchTreeRoots();
    set({ roots: (r?.roots ?? []).map(rootNode) });
  },

  async toggle(node) {
    if (!node.hasChildren) { void get().selectPage(node.page!); return; }
    const expanded = new Set(get().expanded);
    if (expanded.has(node.key)) {
      expanded.delete(node.key);
      set({ expanded });
      return;
    }
    expanded.add(node.key);
    set({ expanded });
    if (!get().childrenByKey[node.key] && !get().loading.has(node.key)) {
      const loading = new Set(get().loading); loading.add(node.key); set({ loading });
      const kids = await fetchChildren(node);
      set((s) => ({
        childrenByKey: { ...s.childrenByKey, [node.key]: kids },
        loading: new Set([...s.loading].filter((k) => k !== node.key)),
      }));
    }
  },

  async loadMore(node) {
    const parent = node.loaderParent!;
    const kids = await fetchChildren(
      get().roots!.find((r) => r.key === parent) ??
        { key: parent, kind: node.loaderKind!, label: "", page: null, pageType: null, hasChildren: true },
      node.after,
    );
    set((s) => {
      const existing = (s.childrenByKey[parent] ?? []).filter((k) => k.kind !== "more");
      return { childrenByKey: { ...s.childrenByKey, [parent]: [...existing, ...kids] } };
    });
  },

  async selectPage(page) {
    set({ selectedPage: page, selectedKey: null, overview: null, overviewMode: null,
          contentLoading: true });
    const content = await fetchPageContent(page);
    // Ignore if the selection changed while fetching.
    if (get().selectedPage === page) set({ content, contentLoading: false });
  },

  // Both overview modes fetch the same table overview (its `indexes` include SQL);
  // `overviewMode` picks Table Overview vs Index Overview in the right pane.
  async selectTable(objectId, key) {
    await loadOverview(set, get, objectId, key, "table");
  },
  async selectIndexes(objectId, key) {
    await loadOverview(set, get, objectId, key, "index");
  },

  // Move the shared selection highlight to `node` without fetching any detail
  // (used by non-Page-Tree views, which drive their own content off the click).
  highlightNode(node) {
    if (node.page != null) set({ selectedPage: node.page, selectedKey: null, overviewMode: null });
    else set({ selectedKey: node.key, selectedPage: null, overviewMode: null });
  },

  // Select `page` and expand the tree down to its node. The path from the map is
  // the page ancestry from a b-tree root down; we prefix it with the synthetic
  // grouping chain (table [→ Indexes] → b-tree page node) that now sits above
  // every b-tree root. Freelist/other targets just select (path degrades).
  async revealPage(page) {
    void get().selectPage(page);
    // The tree view may not have mounted yet (e.g. revealed from the Query view),
    // so ensure the roots exist before walking the ancestor path.
    if (!get().roots) await get().loadRoots();
    const r = await fetchTreePath(page);
    const path = r?.path ?? [];
    if (!path.length) return;

    // Map path[0] (a b-tree root page) to its owning grouping chain, root-first,
    // ending at the b-tree page node.
    const rootPage = path[0].page;
    let chain: TreeNode[] = [];
    for (const t of get().roots ?? []) {
      if (t.kind !== "table") continue;
      if (t.tableBtree?.page === rootPage) {
        chain = [t, btreeChildNode(t.key, t.tableBtree, "table")];
        break;
      }
      const ix = (t.indexes ?? []).find((b) => b.page === rootPage);
      if (ix) {
        const grp = indexesGroupNode(t.key, t.objectId, t.indexes!);
        chain = [t, grp, btreeChildNode(grp.key, ix, "index")];
        break;
      }
    }
    if (!chain.length) return;

    const expanded = new Set(get().expanded);
    // Expand + cache the synthetic parents (all chain nodes but the b-tree page).
    for (let i = 0; i < chain.length - 1; i++) {
      const parent = chain[i];
      expanded.add(parent.key);
      if (!get().childrenByKey[parent.key]) {
        const kids = await fetchChildren(parent);
        const k = parent.key;
        set((s) => ({ childrenByKey: { ...s.childrenByKey, [k]: kids } }));
      }
    }

    // Descend the page path from the b-tree page node (the chain's last entry).
    let node: TreeNode = chain[chain.length - 1];
    let key = node.key;
    for (let i = 1; i < path.length; i++) {
      let kids = get().childrenByKey[key];
      if (!kids) {
        kids = await fetchChildren(node);
        const loadedKey = key;
        set((s) => ({ childrenByKey: { ...s.childrenByKey, [loadedKey]: kids! } }));
      }
      expanded.add(key);
      const next = kids.find((k) => k.key === `${key}>${path[i].edgeKind}:${path[i].page}`);
      if (!next) break;
      key = next.key;
      node = next;
    }
    set({ expanded });
  },
}));
