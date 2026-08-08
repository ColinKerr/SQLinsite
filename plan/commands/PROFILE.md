# Details for the `sqlinsite profile` command

## Statements File Format

```JSON
{
    "TestRun": {
        "Name": "Test Name",
        "Sessions": [
            {
                "SessionName": "Session One",
                "Statements": [
                    "SELECT * FROM Banana",
                    "SELECT * FROM Apple"
                ]
            },
            {
                "SessionName": "Session Two",
                "Statements": [
                    "SELECT * FROM Juice"
                ]
            }
        ]
    }
}
```

The test file is reopened fresh for each session but is kept open within one session.
Statements within a session are executed in array order; `Statement Index` is the
0-based position of a statement in its session's `Statements` array.

Because the connection is reopened per session, statements in the same session share
a warm page cache, while a fresh session starts cold. To measure a statement against a
cold page cache, give it its own session.

## Output File Format

A SQLite db with two tables: a raw `accesses` log — one row
per page access, preserving order and timing — and a single-row `meta` header. The
raw log is kept whole (not pre-aggregated) so the db is useful for timing analysis;
`sqlinsite visualize --profile-file` aggregates it into per-page read/write counts on
import.

### `accesses` table (one row per page access)

- `sessionName` — Name of the session from the statements file.
- `statementIndex` — 0-based index of the statement running when this page was accessed.
- `timeStart` — Monotonic clock reading (nanoseconds) when the page request entered the VFS.
- `timeEnd` — Monotonic clock reading (nanoseconds) when the underlying VFS returned.
- `pageNumber` — 1-based SQLite page number, derived as `offset / page_size + 1` (page 1 is the database header). Matches SQLite's own numbering and the `pageNumber` field emitted by `sqlinsite map`. Only the main database file is logged.
- `access` — `Read` or `Write`, indicating whether the page was accessed via `xRead` or `xWrite`.

Rows are inserted in the order page accesses occur. With `--timing raw` (default)
`timeStart` / `timeEnd` share an arbitrary monotonic epoch and are meaningful as
deltas, not wall-clock times; with `--timing relative` they are rebased to the run start.

The SQL text is **not** stored per access — it would bloat every row and the
statements file already maps `sessionName` + `statementIndex` back to the exact
statement. Treat the statements JSON as the run's manifest.

### `meta` table (single row, run header)

- `formatVersion` — Profile-db schema version, so `visualize` can refuse an incompatible file.
- `testFile` — Path to the db that was profiled (`--db-file`); lets `visualize` catch a profile overlaid onto a different db.
- `pageSize` — Page size discovered from the test db header (confirms page-number alignment with the map).
- `timing` — `raw` or `relative` (how to interpret `timeStart` / `timeEnd`).
- `name` — `TestRun.Name` from the statements file.
- `createdAt` — UTC ISO-8601 timestamp of the run.

`formatVersion`, `testFile`, and `pageSize` are the correctness-relevant fields
`visualize` reads; the rest are provenance. Example row as JSON:

```json
{
  "formatVersion": 1,
  "testFile": "/Users/me/src/imodels/1CP02_4k.bim",
  "pageSize": 4096,
  "timing": "raw",
  "name": "Nightly load test",
  "createdAt": "2026-08-03T14:12:07Z"
}
```

## Run summary

Unless `--quiet` is given, a summary is written to stderr after a run: per-session
row counts (with read/write split) and a grand total. It does not affect the output db.

```
SQLinsite summary:
  session "ReadSession": 3 rows (3 read, 0 write)
  session "WriteSession": 6 rows (4 read, 2 write)
  total: 9 rows (7 read, 2 write) across 2 session(s)
```
