import { MINIMAP_BUCKETS } from "./constants.ts";
import type {
  Meta, MinimapResponse, ObjectPagesResponse, ObjectRunsResponse, PageContent, PageDetail,
  PagesResponse, ProfilePagesResponse, ProfileSourcesResponse, RunsResponse, StructuralGroupsResponse,
  TreeChild, TreeObjectResponse, TreePagesResponse, TreePathResponse, TreeRoot, TreeSearchResponse,
} from "./types.ts";

export async function getJson<T>(url: string): Promise<T | null> {
  const r = await fetch(url);
  if (!r.ok) return null;
  return (await r.json()) as T;
}

// `&sel=` fragment for the current profile-source selection; empty when all
// sources are on (the server treats "no sel" as "all"), "&sel=-1" when none.
export function selParam(selSources: Set<number>, sourceCount: number, hasProfile: boolean): string {
  if (!hasProfile) return "";
  if (selSources.size === 0) return "&sel=-1";
  if (selSources.size === sourceCount) return "";
  return "&sel=" + [...selSources].join(",");
}

export const fetchMeta = () => getJson<Meta>("/api/meta");
export const fetchPages = (from: number, to: number) =>
  getJson<PagesResponse>(`/api/pages?from=${from}&to=${to}`);
export const fetchRuns = (from: number, to: number) =>
  getJson<RunsResponse>(`/api/runs?from=${from}&to=${to}`);
// Whole-file object-colored overview for the minimap (tiny; no run map needed).
export const fetchMinimap = (buckets = MINIMAP_BUCKETS) =>
  getJson<MinimapResponse>(`/api/minimap?buckets=${buckets}`);
export const fetchObjectPages = (objectId: number, from: number, to: number) =>
  getJson<ObjectPagesResponse>(`/api/object/pages?objectId=${objectId}&from=${from}&to=${to}`);
export const fetchObjectRuns = (objectId: number, from: number, to: number) =>
  getJson<ObjectRunsResponse>(`/api/object/runs?objectId=${objectId}&from=${from}&to=${to}`);
export const fetchObjectPageOrdinal = (objectId: number, page: number) =>
  getJson<{ ordinal: number }>(`/api/object/page-ordinal?objectId=${objectId}&page=${page}`);
export const fetchStructuralGroups = () =>
  getJson<StructuralGroupsResponse>("/api/tables/structural-groups");
export const fetchStructuralPages = (key: string, from: number, to: number) =>
  getJson<ObjectPagesResponse>(`/api/tables/structural-pages?key=${key}&from=${from}&to=${to}`);
export const fetchStructuralPageOrdinal = (key: string, page: number) =>
  getJson<{ ordinal: number }>(`/api/tables/structural-page-ordinal?key=${key}&page=${page}`);
export const fetchPage = (n: number) => getJson<PageDetail>(`/api/page/${n}`);
export const fetchProfilePages = (from: number, to: number, sel: string) =>
  getJson<ProfilePagesResponse>(`/api/profile/pages?from=${from}&to=${to}${sel}`);
// Every profile source (loaded 'input' + interactive 'query') for the overlay tree.
export const fetchProfileSources = () =>
  getJson<ProfileSourcesResponse>("/api/profile/sources");

// Page Tree view.
export const fetchTreeRoots = () => getJson<{ roots: TreeRoot[] }>("/api/tree/roots");
export const fetchTreeChildren = (page: number) =>
  getJson<{ children: TreeChild[] }>(`/api/tree/children?page=${page}`);
export const fetchTreeFreelist = (after: number, limit: number) =>
  getJson<TreePagesResponse>(`/api/tree/freelist?after=${after}&limit=${limit}`);
export const fetchTreePointerMap = (after: number, limit: number) =>
  getJson<TreePagesResponse>(`/api/tree/pointermap?after=${after}&limit=${limit}`);
export const fetchTreeOther = (after: number, limit: number) =>
  getJson<TreePagesResponse>(`/api/tree/other?after=${after}&limit=${limit}`);
export const fetchTreePath = (page: number) =>
  getJson<TreePathResponse>(`/api/tree/path?page=${page}`);
export const fetchTreeObject = (objectId: number) =>
  getJson<TreeObjectResponse>(`/api/tree/object?id=${objectId}`);
export const fetchTreeSearch = (q: string, limit: number) =>
  getJson<TreeSearchResponse>(`/api/tree/search?q=${encodeURIComponent(q)}&limit=${limit}`);
export const fetchPageContent = (n: number) => getJson<PageContent>(`/api/page/${n}/content`);
