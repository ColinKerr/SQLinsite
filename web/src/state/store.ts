import { create, createStore, type StateCreator, type StoreApi } from "zustand";
import { fetchProfilePages, selParam } from "../core/api.ts";
import { MAX_BLOCK_PX, MIN_BLOCK_PX } from "../core/constants.ts";
import { Profile } from "../core/profile.ts";
import type { Meta, Metric, ObjectInfo, Run, SessionInfo, View } from "../core/types.ts";

// Shared UI state. Hot render-loop state (scroll, caches, selected, canvas size)
// lives inside CanvasController, not here, so canvas panning never re-renders React.
export interface VizState {
  meta: Meta | null;
  objById: Map<number, ObjectInfo>;
  objects: ObjectInfo[];
  pageCount: number;
  hasProfile: boolean;
  sessions: SessionInfo[];
  allRuns: Run[];

  view: View;
  blockPx: number;
  metric: Metric;
  selLeaves: Set<number>;
  leafCount: number;
  profile: Profile;
  legendWidth: number;
  pendingPage: number | null; // page to scroll to once the Pages canvas mounts

  initFromMeta(meta: Meta, allRuns: Run[], profile: Profile): void;
  setView(v: View): void;
  setBlockPx(px: number): void;
  setMetric(m: Metric): void;
  setLeaves(next: Set<number>): void;
  setLegendWidth(w: number): void;
  reloadProfile(): Promise<void>;
  requestPage(n: number): void;     // switch to Pages view and scroll to page n
  clearPendingPage(): void;
}

const creator: StateCreator<VizState> = (set, get) => ({
  meta: null,
  objById: new Map(),
  objects: [],
  pageCount: 0,
  hasProfile: false,
  sessions: [],
  allRuns: [],

  view: "pages",
  blockPx: 12,
  metric: "none",
  selLeaves: new Set(),
  leafCount: 0,
  profile: Profile.empty(),
  legendWidth: 240,
  pendingPage: null,

  initFromMeta(meta, allRuns, profile) {
    const objById = new Map(meta.objects.map((o) => [o.id, o] as const));
    const selLeaves = new Set<number>();
    for (const s of meta.sessions || []) for (const l of s.leaves) selLeaves.add(l.leafId);
    set({
      meta,
      objById,
      objects: meta.objects,
      pageCount: meta.meta.pageCount,
      hasProfile: !!meta.hasProfile,
      sessions: meta.sessions || [],
      allRuns,
      selLeaves,
      leafCount: selLeaves.size,
      metric: meta.hasProfile ? "total" : "none",
      profile,
    });
  },

  setView: (v) => set({ view: v }),
  setBlockPx: (px) => set({ blockPx: Math.max(MIN_BLOCK_PX, Math.min(MAX_BLOCK_PX, px)) }),
  setMetric: (m) => set({ metric: m }),
  setLeaves: (next) => set({ selLeaves: new Set(next) }),
  setLegendWidth: (w) => set({ legendWidth: w }),
  requestPage: (n) => set({ view: "pages", pendingPage: n }),
  clearPendingPage: () => set({ pendingPage: null }),

  async reloadProfile() {
    const s = get();
    if (!s.hasProfile) return;
    const sel = selParam(s.selLeaves, s.leafCount, s.hasProfile);
    const data = await fetchProfilePages(1, s.pageCount, sel);
    set({ profile: new Profile(data || { pages: [] }) });
  },
});

// App-global store (Pages/Tables views + top bar).
export const useViz = create<VizState>(creator);

// A fresh, independent store instance — used by the Query view's result canvas,
// seeded with a query run's profile so it can reuse the canvas renderer.
export type VizStore = StoreApi<VizState>;
export const createVizStore = (): VizStore => createStore<VizState>(creator);
