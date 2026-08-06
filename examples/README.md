# Example

A minimal end-to-end run of `sqlinsite profile`.

## 1. Create a test database

```sh
sqlite3 fruit.db "CREATE TABLE Fruit(id INTEGER PRIMARY KEY, name TEXT);
                  INSERT INTO Fruit(name) VALUES ('apple'),('banana'),('cherry');"
```

## 2. Profile it

```sh
sqlinsite profile \
  --db-file fruit.db \
  --statements examples/statements.json \
  --out-file fruit-trace.sqlite \
  --timing relative
```

A summary is printed to stderr:

```
SQLinsite summary:
  session "WarmCacheReads": 3 rows (3 read, 0 write)
  session "ColdSelect": 2 rows (2 read, 0 write)
  session "Write": 4 rows (2 read, 2 write)
  total: 9 rows (7 read, 2 write) across 3 session(s)
```

## 3. Read the trace

`fruit-trace.sqlite` is a SQLite database — query it with `sqlite3`. The raw
per-access log is in `accesses` (abridged; timings vary per machine):

```sh
sqlite3 fruit-trace.sqlite \
  "SELECT sessionName, statementIndex, timeStart, timeEnd, pageNumber, access
   FROM accesses ORDER BY timeStart LIMIT 5;"
```
```
WarmCacheReads|0|121000|122416|1|Read
WarmCacheReads|0|175333|176333|1|Read
WarmCacheReads|0|179375|180416|2|Read
Write|0|537958|539625|1|Write
Write|0|540083|541333|2|Write
```

- Each row is one page access at the VFS layer for the **main database file**.
- `statementIndex` is the 0-based position of the statement within its session;
  cross-reference it against `examples/statements.json` to see the SQL.
- `pageNumber` is `byte_offset / page_size + 1`; page 1 is the database header.
  It matches SQLite's numbering and `sqlinsite map`'s `pageNumber`.
- `timeStart`/`timeEnd` are nanoseconds. With `--timing relative` they are
  rebased to the start of the run; without it they are raw monotonic ticks.
  Use `timeEnd - timeStart` for the per-access cost.
- The `meta` table carries the run header (`SELECT * FROM meta;`): `formatVersion`,
  `testFile`, `pageSize`, `timing`, `name`, `createdAt`.
- Pass this file to `sqlinsite visualize serve --profile-file fruit-trace.sqlite`
  to overlay the reads/writes on the map.

Notes:

- Statements in the same session share a warm page cache. To profile a statement
  against a cold cache, put it in its own session.
- Writes run **in place** — the example's `INSERT` permanently adds a row to
  `fruit.db`.
