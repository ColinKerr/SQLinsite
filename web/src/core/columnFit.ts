// Auto-fit widths for the results table: size each column to the widest of its
// header and cell values in the first page of data. Pure (text measurement is
// injected) so it is unit-testable without a DOM.

export const MIN_COL = 48;
export const MAX_COL = 480;
// Extra room per cell: horizontal padding + space for the page tint / affordance.
const DEFAULT_PAD = 20;

export function formatCellText(v: unknown): string {
  if (v === null || v === undefined) return "NULL";
  return String(v);
}

export interface FitOptions {
  min?: number;
  max?: number;
  pad?: number;
}

// Returns a pixel width per column (index-aligned to `columns`), each clamped to
// [min, max]. `rows` should be the first loaded window; `measure` returns the
// rendered pixel width of a string in the table font.
export function fitColumnWidths(
  columns: { name: string }[],
  rows: unknown[][],
  measure: (text: string) => number,
  opts: FitOptions = {},
): number[] {
  const min = opts.min ?? MIN_COL;
  const max = opts.max ?? MAX_COL;
  const pad = opts.pad ?? DEFAULT_PAD;

  return columns.map((col, c) => {
    let widest = measure(col.name);
    for (const row of rows) {
      const w = measure(formatCellText(row[c]));
      if (w > widest) widest = w;
    }
    return Math.round(Math.min(max, Math.max(min, widest + pad)));
  });
}
