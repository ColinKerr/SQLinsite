import type {
  Meta, ObjectPagesResponse, PageContent, PageDetail, PagesResponse, ProfilePagesResponse,
  RunsResponse, TreeChild, TreeObjectResponse, TreePagesResponse, TreePathResponse, TreeRoot,
  TreeSearchResponse,
} from "./types.ts";

export async function getJson<T>(url: string): Promise<T | null> {
  const r = await fetch(url);
  if (!r.ok) return null;
  return (await r.json()) as T;
}

// `&sel=` fragment for the current session/query selection; empty when all
// leaves are on (the server treats "no sel" as "all"), "&sel=-1" when none.
export function selParam(selLeaves: Set<number>, leafCount: number, hasProfile: boolean): string {
  if (!hasProfile) return "";
  if (selLeaves.size === 0) return "&sel=-1";
  if (selLeaves.size === leafCount) return "";
  return "&sel=" + [...selLeaves].join(",");
}

export const fetchMeta = () => getJson<Meta>("/api/meta");
export const fetchPages = (from: number, to: number) =>
  getJson<PagesResponse>(`/api/pages?from=${from}&to=${to}`);
export const fetchRuns = (from: number, to: number) =>
  getJson<RunsResponse>(`/api/runs?from=${from}&to=${to}`);
export const fetchObjectPages = (objectId: number, from: number, to: number) =>
  getJson<ObjectPagesResponse>(`/api/object/pages?objectId=${objectId}&from=${from}&to=${to}`);
export const fetchPage = (n: number) => getJson<PageDetail>(`/api/page/${n}`);
export const fetchProfilePages = (from: number, to: number, sel: string) =>
  getJson<ProfilePagesResponse>(`/api/profile/pages?from=${from}&to=${to}${sel}`);

// Page Tree view.
export const fetchTreeRoots = () => getJson<{ roots: TreeRoot[] }>("/api/tree/roots");
export const fetchTreeChildren = (page: number) =>
  getJson<{ children: TreeChild[] }>(`/api/tree/children?page=${page}`);
export const fetchTreeFreelist = (after: number, limit: number) =>
  getJson<TreePagesResponse>(`/api/tree/freelist?after=${after}&limit=${limit}`);
export const fetchTreeOther = (after: number, limit: number) =>
  getJson<TreePagesResponse>(`/api/tree/other?after=${after}&limit=${limit}`);
export const fetchTreePath = (page: number) =>
  getJson<TreePathResponse>(`/api/tree/path?page=${page}`);
export const fetchTreeObject = (objectId: number) =>
  getJson<TreeObjectResponse>(`/api/tree/object?id=${objectId}`);
export const fetchTreeSearch = (q: string, limit: number) =>
  getJson<TreeSearchResponse>(`/api/tree/search?q=${encodeURIComponent(q)}&limit=${limit}`);
export const fetchPageContent = (n: number) => getJson<PageContent>(`/api/page/${n}/content`);
