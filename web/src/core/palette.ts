export const PALETTE = [
  "#4e79a7", "#f28e2b", "#59a14f", "#e15759", "#b07aa1",
  "#76b7b2", "#edc948", "#ff9da7", "#9c755f", "#bab0ac",
];

export const STRUCTURAL: Record<string, string> = {
  "overflow": "#7a7f8a",
  "freelist-trunk": "#565b66",
  "freelist-leaf": "#454a54",
  "pointer-map": "#8a7fae",
  "lock-byte": "#9a6b6b",
  "unallocated": "#33373f",
};

export const GLYPH: Record<string, string> = {
  "table-leaf": "T", "table-interior": "T↑", "index-leaf": "I", "index-interior": "I↑",
  "overflow": "O", "freelist-trunk": "F", "freelist-leaf": "f", "pointer-map": "P",
  "lock-byte": "L", "unallocated": "·",
};

export function colorForObject(id: number): string {
  return PALETTE[((id % PALETTE.length) + PALETTE.length) % PALETTE.length];
}

// Subtle stable tint for a data page number, used to shade results-table cells by
// the page that stores their bytes. Keyed on the page number so adjacent pages are
// visually distinct; low alpha keeps cell text readable on the dark theme.
export function colorForPageNumber(page: number): string {
  const base = PALETTE[((page % PALETTE.length) + PALETTE.length) % PALETTE.length];
  const r = parseInt(base.slice(1, 3), 16);
  const g = parseInt(base.slice(3, 5), 16);
  const b = parseInt(base.slice(5, 7), 16);
  return `rgba(${r}, ${g}, ${b}, 0.28)`;
}

export function colorForPage(objectId: number | null | undefined, pageType: string): string {
  return objectId === null || objectId === undefined
    ? STRUCTURAL[pageType] || "#33373f"
    : colorForObject(objectId);
}
