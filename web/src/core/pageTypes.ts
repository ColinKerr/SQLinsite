// Human-readable descriptions for page types (tree popovers) and the byte
// regions used by the page-detail schematic + legend.

export const PAGE_TYPE_DESC: Record<string, string> = {
  "table-leaf": "Table b-tree leaf — holds rows (rowid + record) for a table.",
  "table-interior": "Table b-tree interior — routes to child pages by rowid.",
  "index-leaf": "Index b-tree leaf — holds index keys.",
  "index-interior": "Index b-tree interior — routes to child pages by key.",
  "overflow": "Overflow page — continuation of a large cell's payload.",
  "freelist-trunk": "Freelist trunk — points to the next trunk and a list of free leaf pages.",
  "freelist-leaf": "Freelist leaf — an unused page held on the freelist.",
  "pointer-map": "Pointer-map page — parent pointers used by auto-vacuum.",
  "lock-byte": "Lock-byte page — reserved region, no data.",
  "unallocated": "Unallocated page — not part of any structure.",
};

// Color per schematic region kind (distinct from the object palette).
export const REGION_COLOR: Record<string, string> = {
  "db-header": "#b07aa1",
  "page-header": "#4e79a7",
  "cellptr-array": "#76b7b2",
  "cell": "#59a14f",
  "overflow-header": "#4e79a7",
  "payload": "#59a14f",
  "freelist-header": "#4e79a7",
  "freelist-array": "#76b7b2",
  "free": "#33373f",
  "reserved": "#565b66",
  "unknown": "#7a7f8a",
};

export const REGION_LABEL: Record<string, string> = {
  "db-header": "Database header (100 bytes)",
  "page-header": "Page header",
  "cellptr-array": "Cell pointer array",
  "cell": "Cell (record)",
  "overflow-header": "Overflow next-page pointer",
  "payload": "Overflow payload",
  "freelist-header": "Freelist trunk header",
  "freelist-array": "Free leaf page numbers",
  "free": "Free / unallocated",
  "reserved": "Reserved bytes",
  "unknown": "Page bytes",
};

export function regionColor(kind: string): string {
  return REGION_COLOR[kind] ?? REGION_COLOR["unknown"];
}
export function regionLabel(kind: string): string {
  return REGION_LABEL[kind] ?? kind;
}
export function pageTypeDesc(type: string | null | undefined): string {
  return (type && PAGE_TYPE_DESC[type]) || "Page";
}
