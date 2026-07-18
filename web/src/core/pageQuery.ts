// SQL identifier quoting.
export const qi = (name: string) => `"${name.replace(/"/g, '""')}"`;

// Builds `SELECT * FROM "T" WHERE (rowid BETWEEN a AND b) OR (rowid = c) OR …`
// from a batch of rowid runs, so it selects exactly those rows (rowids aren't
// contiguous — deletions leave gaps — so a plain BETWEEN over the whole range
// would sweep in rows that don't belong to the page). A single-rowid run uses
// `rowid = x`.
export function pageRowsSql(table: string, runs: [number, number][]): string {
  const terms = runs.map(([lo, hi]) => (lo === hi ? `rowid = ${lo}` : `(rowid BETWEEN ${lo} AND ${hi})`));
  return `SELECT * FROM ${qi(table)} WHERE ${terms.join(" OR ")};`;
}
