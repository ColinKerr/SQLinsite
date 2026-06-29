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

A CSV file with a header row followed by one row per page access. Columns:

- `Session Name` - Name of the session from the statements file.
- `Statement Index` - 0-based index of the statement running when this page was accessed.
- `Time Start` - Monotonic clock reading (nanoseconds) when the page request entered the VFS.
- `Time End` - Monotonic clock reading (nanoseconds) when the underlying VFS returned.
- `Page Number` - 1-based SQLite page number, derived as `offset / page_size + 1` (page 1 is the database header). Matches SQLite's own numbering and the `pageNumber` field emitted by `sqlinsite map`. Only the main database file is logged.
- `Read or Write` - `Read` or `Write`, indicating whether the page was accessed via `xRead` or `xWrite`.

Rows are emitted in the order page accesses occur. With `--timing raw` (default)
`Time Start` / `Time End` share an arbitrary monotonic epoch and are meaningful as
deltas, not wall-clock times; with `--timing relative` they are rebased to the run start.

The SQL text is **not** included in the output rows — it would bloat
every row and the statements file already maps `Session Name` + `Statement Index`
back to the exact statement. Treat the statements JSON as the run's manifest.

## Run summary

Unless `--quiet` is given, a summary is written to stderr after a run: per-session
row counts (with read/write split) and a grand total. It does not affect the CSV.

```
SQLinsite summary:
  session "ReadSession": 3 rows (3 read, 0 write)
  session "WriteSession": 6 rows (4 read, 2 write)
  total: 9 rows (7 read, 2 write) across 2 session(s)
```
