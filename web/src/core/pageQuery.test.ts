import { describe, expect, it } from "vitest";
import { pageRowsSql, qi } from "./pageQuery.ts";

describe("pageRowsSql", () => {
  it("selects exact rowid runs (gaps excluded), single rowids as equality", () => {
    expect(pageRowsSql("T", [[2, 7], [9, 9], [16, 21]])).toBe(
      'SELECT * FROM "T" WHERE (rowid BETWEEN 2 AND 7) OR rowid = 9 OR (rowid BETWEEN 16 AND 21);',
    );
  });

  it("quotes table identifiers, escaping embedded quotes", () => {
    expect(qi('a"b')).toBe('"a""b"');
    expect(pageRowsSql('my"tbl', [[1, 1]])).toBe('SELECT * FROM "my""tbl" WHERE rowid = 1;');
  });
});
