// Formats a byte count as KB / MB / GB, choosing the largest unit that leaves at
// least one whole-number digit, with at most two decimal places and trailing
// zeros trimmed, e.g. 1536 → "1.5 KB", 1048576 → "1 MB", 1.234e6 → "1.18 MB".
// Sub-sector byte sizes (cell/blob/region bytes within a page) use
// formatSectorBytes instead.
export function formatBytes(bytes: number): string {
  const units: [string, number][] = [
    ["GB", 1024 ** 3],
    ["MB", 1024 ** 2],
    ["KB", 1024],
  ];
  const [unit, size] = units.find(([, s]) => bytes >= s) ?? ["KB", 1024];
  const value = bytes / size;
  // Never show more than two digits to the right of the decimal point.
  let text = value.toFixed(2);
  // Trim trailing zeros (and a dangling dot), but only past a decimal point so
  // integers like "1000" are left intact.
  if (text.includes(".")) text = text.replace(/0+$/, "").replace(/\.$/, "");
  return `${text} ${unit}`;
}

// Formats a page-sector byte size — the bytes used by a sector within a page
// (cell, unallocated space, blob column, region), always shown unconverted with a
// bare "B" suffix, e.g. 42 → "42B". SQLite caps page size at 64 KiB, so these
// never exceed 65536.
export function formatSectorBytes(bytes: number): string {
  return `${bytes}B`;
}

// Formats a generic count (rows, pages, indexes, …) with thousands separators and
// no decimal point, e.g. 4599739 → "4,599,739".
export function formatCount(n: number): string {
  return new Intl.NumberFormat("en-US").format(Math.round(n));
}
