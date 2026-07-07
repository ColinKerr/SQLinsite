import { create } from "zustand";
import {
  fetchPageContent, fetchTreeChildren, fetchTreeFreelist, fetchTreeOther, fetchTreePath,
  fetchTreeRoots,
} from "../core/api.ts";
import type { PageContent } from "../core/types.ts";
import { childNode, moreNode, rootNode, type TreeNode } from "../core/treeModel.ts";

const WINDOW = 1000; // page window for the freelist / "all other pages" nodes

export interface TreeState {
  roots: TreeNode[] | null;
  childrenByKey: Record<string, TreeNode[]>;
  expanded: Set<string>;
  loading: Set<string>;
  selectedPage: number | null;
  content: PageContent | null;
  contentLoading: boolean;

  loadRoots(): Promise<void>;
  toggle(node: TreeNode): Promise<void>;
  loadMore(node: TreeNode): Promise<void>; // a "more" loader row
  selectPage(page: number): Promise<void>;
  revealPage(page: number): Promise<void>; // select + expand the tree down to it
}

// Fetches a node's children (page b-tree children, or a window of the freelist /
// other virtual roots), appending a "load more" row when a window is full.
async function fetchChildren(node: TreeNode, after = 0): Promise<TreeNode[]> {
  if (node.kind === "page" && node.page != null) {
    const r = await fetchTreeChildren(node.page);
    return (r?.children ?? []).map((c) => childNode(node.key, c));
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
  content: null,
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
    set({ selectedPage: page, contentLoading: true });
    const content = await fetchPageContent(page);
    // Ignore if the selection changed while fetching.
    if (get().selectedPage === page) set({ content, contentLoading: false });
  },

  // Select `page` and expand the tree down to its node (following the ancestor
  // path from a b-tree root). Freelist/other targets just select (path degrades).
  async revealPage(page) {
    void get().selectPage(page);
    const r = await fetchTreePath(page);
    const path = r?.path ?? [];
    const root = path.length ? get().roots?.find((n) => n.kind === "page" && n.page === path[0].page) : undefined;
    if (!root) return;
    const expanded = new Set(get().expanded);
    let key = root.key;
    let node: TreeNode = root;
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
