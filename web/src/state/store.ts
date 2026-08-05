import { create, createStore, type StateCreator, type StoreApi } from "zustand";
import { fetchProfilePages, fetchProfileSources, fetchStructuralGroups, selParam } from "../core/api.ts";
import { MAX_BLOCK_PX, MIN_BLOCK_PX } from "../core/constants.ts";
import { Profile } from "../core/profile.ts";
import type { BlockColorMode, Meta, Metric, MinimapBucket, ObjectInfo, ProfileSource, StructuralGroup, View } from "../core/types.ts";

// Shared UI state. Hot render-loop state (scroll, caches, selected, canvas size)
// lives inside CanvasController, not here, so canvas panning never re-renders React.
export interface VizState {
  meta: Meta | null;
  objById: Map<number, ObjectInfo>;
  objects: ObjectInfo[];
  structuralGroups: StructuralGroup[]; // Tables-view bands for non-object pages
  structuralGroupsLoaded: boolean; // lazily fetched on first Tables-view entry
  pageCount: number;
  hasProfile: boolean;
  // Unified profile sources (loaded 'input' + interactive 'query'), from
  // /api/profile/sources; the overlay is the union of the selected ones.
  sources: ProfileSource[];
  minimap: MinimapBucket[]; // whole-file object-colored overview (Pages minimap)

  // CBS manifest (Block view); present + matching enables the Block button.
  hasManifest: boolean;
  manifestMatch: boolean;
  pagesPerBlock: number;
  blockCount: number;
  blockColorMode: BlockColorMode;

  view: View;
  blockPx: number;
  metric: Metric;
  selSources: Set<number>; // sourceIds whose page accesses feed the overlay
  sourceCount: number;
  profile: Profile;
  legendWidth: number;
  selectedObject: number | null; // Navigation-panel object id (for history nav)

  initFromMeta(meta: Meta, minimap: MinimapBucket[], profile: Profile,
               structuralGroups?: StructuralGroup[]): void;
  ensureStructuralGroups(): Promise<void>;
  setView(v: View): void;
  setBlockPx(px: number): void;
  setMetric(m: Metric): void;
  setBlockColorMode(m: BlockColorMode): void;
  setSources(next: Set<number>): void;
  setLegendWidth(w: number): void;
  setSelectedObject(id: number | null): void;
  reloadProfile(): Promise<void>;
  // Loads the source manifest and refreshes the overlay. `select` chooses the
  // default selection (defaults to all sources).
  loadSources(select?: (sources: ProfileSource[]) => Set<number>): Promise<void>;
  // Called after a Query-view run: picks up the new 'query' source, selects only
  // it (so the run lights up in every view), and refreshes the overlay.
  onQueryRun(queryId: number): Promise<void>;
}

const creator: StateCreator<VizState> = (set, get) => ({
  meta: null,
  objById: new Map(),
  objects: [],
  structuralGroups: [],
  structuralGroupsLoaded: false,
  pageCount: 0,
  hasProfile: false,
  sources: [],
  minimap: [],

  hasManifest: false,
  manifestMatch: false,
  pagesPerBlock: 0,
  blockCount: 0,
  blockColorMode: "object",

  view: "pages",
  blockPx: 12,
  metric: "none",
  selSources: new Set(),
  sourceCount: 0,
  profile: Profile.empty(),
  legendWidth: 240,
  selectedObject: null,

  initFromMeta(meta, minimap, profile, structuralGroups = []) {
    const objById = new Map(meta.objects.map((o) => [o.id, o] as const));
    set({
      meta,
      objById,
      objects: meta.objects,
      structuralGroups,
      structuralGroupsLoaded: structuralGroups.length > 0,
      pageCount: meta.meta.pageCount,
      hasProfile: !!meta.hasProfile,
      minimap,
      hasManifest: !!meta.hasManifest,
      manifestMatch: !!meta.manifestMatch,
      pagesPerBlock: meta.pagesPerBlock ?? 0,
      blockCount: meta.blockCount ?? 0,
      metric: meta.hasProfile ? "total" : "none",
      profile,
    });
  },

  // Switching to Tables lazily loads its structural-page bands (a heavy map query
  // on large files) so it never blocks the initial load of the other views.
  ensureStructuralGroups: async () => {
    if (get().structuralGroupsLoaded) return;
    set({ structuralGroupsLoaded: true }); // guard against concurrent double-fetch
    const res = await fetchStructuralGroups();
    set({ structuralGroups: res?.groups ?? [] });
  },
  setView: (v) => {
    set({ view: v });
    if (v === "tables") void get().ensureStructuralGroups();
  },
  setBlockPx: (px) => set({ blockPx: Math.max(MIN_BLOCK_PX, Math.min(MAX_BLOCK_PX, px)) }),
  setMetric: (m) => set({ metric: m }),
  setBlockColorMode: (m) => set({ blockColorMode: m }),
  setSources: (next) => set({ selSources: new Set(next) }),
  setLegendWidth: (w) => set({ legendWidth: w }),
  setSelectedObject: (id) => set({ selectedObject: id }),

  async reloadProfile() {
    const s = get();
    if (!s.hasProfile) { set({ profile: Profile.empty() }); return; }
    const sel = selParam(s.selSources, s.sourceCount, s.hasProfile);
    const data = await fetchProfilePages(1, s.pageCount, sel);
    set({ profile: new Profile(data || { pages: [] }) });
  },

  async loadSources(select = (srcs) => new Set(srcs.map((s) => s.sourceId))) {
    const res = await fetchProfileSources();
    const sources = res?.sources ?? [];
    const hasProfile = sources.length > 0;
    set({
      sources,
      selSources: select(sources),
      sourceCount: sources.length,
      hasProfile,
      metric: hasProfile && get().metric === "none" ? "total" : get().metric,
    });
    await get().reloadProfile();
  },

  async onQueryRun(queryId) {
    // Select only the just-run query so it lights up in every view; the user can
    // re-check loaded sessions or other runs in the Overlay control.
    await get().loadSources((srcs) => {
      const run = srcs.find((s) => s.kind === "query" && s.sessionId === queryId);
      return new Set(run ? [run.sourceId] : srcs.map((s) => s.sourceId));
    });
  },
});

// App-global store (Pages/Tables views + top bar).
export const useViz = create<VizState>(creator);

// A fresh, independent store instance — used by the Query view's result canvas,
// seeded with a query run's profile so it can reuse the canvas renderer.
export type VizStore = StoreApi<VizState>;
export const createVizStore = (): VizStore => createStore<VizState>(creator);
