# Testing

## Framework

- doctest, wired into CMake as a `ctest` target (`doctest_discover_tests`).
- `ctest` runs the full suite. CI builds the binary and runs `ctest`.

## Strategy

- The suite must stay green (`ctest`) at all times; add tests alongside the code
  they cover.
- Fixtures are generated at test time with the SQLite C API (known schema and
  page size) so expected page numbers, types, and rowids are deterministic.
- Pure logic (varint/record decoders, page-number math, CSV/profile aggregation,
  CLI parsing) is unit-tested directly; commands are covered end to end.
- The visualize **server and API** are tested (a server is started on an
  ephemeral port and driven with an HTTP client); the **canvas pixels** are not
  browser-tested — verify rendering by running `visualize serve` and looking.

## Test fixtures

- Generate small SQLite databases at build/test time with a known schema and
  page size so expected page indices are deterministic.
- Keep one fixture with a single table small enough to fit in a few pages, and
  one larger fixture that forces multi-page reads (B-tree spanning several pages).

## Unit tests

- **Statements file parsing** — valid files produce the expected sessions and
  statements; malformed JSON and missing required fields fail with a clear error.
- **Page number derivation** — `offset / pageSize + 1` (1-based) for representative offsets,
  including the offset-0 header read mapping to page 0 and the page-size = 1
  (65536) header encoding.
- **CSV writer** — header row plus correct column order and escaping.

## VFS integration tests

- Run `profile` against a fixture with a `SELECT` session and assert the output
  CSV contains `Read` rows for the expected pages, in order.
- Run a session with an `INSERT`/`UPDATE` and assert `Write` rows appear and the
  in-place database reflects the change afterward.
- Assert journal/WAL/temp files produce **no** rows (main DB only).
- Assert `Statement Index` and `Session Name` columns match the input across a
  multi-session, multi-statement run.

## Map tests

- Build fixtures (table + index + overflow row, an `auto_vacuum` DB with
  pointer-map pages, a post-delete DB with freelist pages), run `map`, then open
  the output with SQLite and assert: `pages` count equals `meta.pageCount`;
  object→page assignment via `pages.objectId`; rowid ranges; `cells` with decoded
  keys for known pages; every `pointers.toPage` is valid; `runs` tile every page
  with no gaps/overlaps; `type_counts` and `objects.pageCount` agree with `pages`.

## Visualize tests

- Build a small map, start the server on an ephemeral port, and assert the query
  API (`/api/meta`, `/api/pages`, `/api/runs`, `/api/page/:n`,
  `/api/object/pages`, `/api/profile/*`) returns correct JSON; the per-block
  range cap returns 413 and a missing page 404; static assets serve with correct
  content types.
- Profile aggregation (CSV → per-page reads/writes) is unit-tested directly.

## Manual smoke test

```
sqlinsite profile --test-file sample.db --statements sample.json --out-file out.csv
```

Inspect `out.csv` for plausible page access patterns.
