// Lightweight SQL pretty-printer: collapse whitespace and put major clauses on
// their own lines. Not a full formatter, but enough for the Format button.
const CLAUSES = [
  "SELECT", "FROM", "WHERE", "GROUP BY", "HAVING", "ORDER BY", "LIMIT",
  "LEFT JOIN", "INNER JOIN", "CROSS JOIN", "JOIN", "UNION ALL", "UNION",
  "VALUES", "SET",
];

export function formatSql(sql: string): string {
  let s = sql.replace(/\s+/g, " ").trim();
  for (const kw of CLAUSES) {
    const re = new RegExp("\\s*\\b" + kw.replace(/ /g, "\\s+") + "\\b\\s*", "gi");
    s = s.replace(re, "\n" + kw + " ");
  }
  return s.replace(/^\n/, "").replace(/[ \t]+$/gm, "").trim();
}
