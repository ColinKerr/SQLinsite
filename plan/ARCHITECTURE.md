# Architecture

## Tech

- **Language:** C++23 (single native CLI binary).
- **SQLite:** compiled from the official amalgamation (`sqlite3.c` / `sqlite3.h`) **version 3.53.3** (released 2026-06-26), vendored into the repo so the VFS links against a known version. Vendored under `third_party/sqlite/` with the download URL and SHA-256 recorded alongside it. Built with `SQLITE_ENABLE_COLUMN_METADATA`, `SQLITE_THREADSAFE=1`, and (for `map`) `SQLITE_ENABLE_DBPAGE_VTAB`.
- **JSON:** nlohmann/json (FetchContent) for the statements file and the `visualize` API responses.
- **HTTP server (`visualize serve`):** cpp-httplib v0.48.0 (FetchContent, header-only).
- **Front-end (`visualize`):** plain JavaScript drawing on `<canvas>` (no framework). Pages are drawn directly to the canvas — no DOM node per page — so the view scales to billions of pages.
- **Build:** CMake.

## Commands

The binary exposes several subcommands, each its own module set:

- **`profile`** — runs statements through the wrapping VFS, emitting a per-page-access CSV. Detailed below; see [commands/PROFILE.md](./commands/PROFILE.md).
- **`map`** — parses the SQLite file format into an **indexed SQLite file** describing every page. Uses the `sqlite_dbpage` vtab for raw page bytes; does not use the VFS shim. See [commands/MAP.md](./commands/MAP.md).
- **`visualize serve`** — local web server that queries the map SQLite file by page-number range and renders it on a canvas with zoom and level-of-detail (+ optional profile overlay). See [commands/VISUALIZE.md](./commands/VISUALIZE.md).

`profile` and `map` both produce data keyed by SQLite's 1-based **page number**, which is the join `visualize` relies on. The map is a queryable, indexed SQLite database (not a JSON blob) so the visualizer fetches only the pages in the current viewport.

## Overview

SQLinsite registers a custom SQLite VFS that wraps an existing VFS (the
platform default) and records every page-level read and write. The CLI opens
the test database through this wrapping VFS, runs the configured statements,
and streams one CSV row per page access to the output file.

```
statements.json ──► profile command ──► open DB via "sqlinsite" VFS
                                              │
                                              ▼
                                     wrapping VFS shim
                              (xRead / xWrite intercepted)
                                              │
                                              ▼
                                       real (default) VFS
                                              │
                                              ▼
                                   CSV rows ──► --out-file
```

## The wrapping VFS

The shim is a `sqlite3_vfs` whose methods delegate to the underlying default
VFS, with read/write paths instrumented.

- **Registration:** build a `sqlite3_vfs` named `sqlinsite` that copies the
  function pointers of `sqlite3_vfs_find(NULL)` (the default), overriding
  `xOpen`. Register it non-default and select it explicitly when opening the
  database.
- **xOpen:** allocate a wrapper `sqlite3_file` that stores (a) the real file
  handle returned by the underlying VFS and (b) the open flags. Its
  `pMethods` point to wrapper IO methods. Only files opened with
  `SQLITE_OPEN_MAIN_DB` are flagged for logging; journal, WAL, and temp files
  delegate transparently and are **not** logged (see File scope).
- **xRead / xWrite:** for a logged file, capture the start time, call the
  underlying method, capture the end time, derive the page number, and emit a
  CSV row. All other VFS methods (xClose, xSync, xFileSize, locking, etc.)
  delegate straight through.

## Profiling context

Page access is logged at the VFS layer, which has no knowledge of which
statement is running. A process-global profiling context bridges that gap:

```
struct ProfilingContext {
    std::string sessionName;
    int statementIndex;   // 0-based index within the session
    int pageSize;         // discovered from the DB header
    CsvWriter* out;
};
```

The profile loop updates `sessionName` and `statementIndex` before executing
each statement, and the VFS IO methods read the context when emitting rows.

## Page number derivation

The VFS operates on byte offsets, not page numbers. For a main-DB access at
byte `offset`:

```
pageNumber = offset / pageSize + 1
```

This is **1-based**, matching SQLite's own page numbering (and the `pageNumber`
field from `sqlinsite map`), so profile traces and maps join without an
off-by-one. `pageSize` is read from bytes 16–17 of the database header
(big-endian; a value of 1 means 65536) on the first read of the main DB, or
obtained via `PRAGMA page_size` before measurement begins. The 100-byte header
read at offset 0 maps to page 1.

## Timing

`Time Start` / `Time End` are samples from a monotonic high-resolution clock
(`std::chrono::steady_clock`, reported in nanoseconds). They bracket the call
into the underlying VFS, so the delta approximates the cost of servicing that
page request.

## File scope

Only the main database file is logged. Rollback-journal, WAL, and temp files
pass through the shim untouched. This keeps `Page Number` well-defined.

## Sessions and statement execution

- The test database is opened **fresh per session** (new connection through
  the `sqlinsite` VFS) and kept open for all statements in that session.
- Page-cache warming across statements is intentional and controlled by
  session grouping: statements sharing a session share a warm page cache. To
  profile a statement against a **cold** cache, put it in a session of its own,
  so the fresh connection forces every page to be read from the VFS.
- Statements run **in place** against `--test-file`; write statements
  permanently modify the original database.
- Each statement is prepared, stepped to completion (rows are consumed so all
  pages are touched), and finalized before the next statement runs.

## Source layout

```
CMakeLists.txt            top-level build (vendored SQLite, FetchContent deps, asset embedding)
cmake/embed_assets.cmake  generates a C++ byte-array source embedding visualize assets
third_party/sqlite/       vendored SQLite 3.53.3 amalgamation (URL + SHA-256 recorded)
assets/                   visualize front-end (index.html, app.js, style.css) — embedded
bench/overhead.cpp        sqlinsite_bench: VFS overhead benchmark (not a ctest)
src/                      include root; internal includes are written relative to it
  main.cpp                CLI entry point
  common/
    cli.*                 argument parsing / dispatch → ParsedCli (pure, unit-tested)
  profile/
    profile_command.*     session orchestration + statement execution
    vfs_shim.*            wrapping VFS and instrumented IO methods
    profiling_context.hpp process-global context bridging the profile loop ↔ VFS
    statements_file.*     statements JSON parsing/validation
    csv_writer.*          buffered CSV output
    page_index.hpp        offset → 1-based page number (pure, tested)
  map/
    db_file.*             read a DB's pages/header via the sqlite_dbpage vtab
    sqlite_format.*       varint / record / b-tree decoders (pure, tested)
    page_parser.*         decode one page → PageInfo (cells, pointers, free bytes)
    map_model.hpp         PageInfo / Pointer / cell structs
    map_builder.*         classify pages, walk b-trees, stream rows to the writer
    map_writer.*          create + populate the map SQLite database
    map_command.*         map CLI entry
  visualize/
    map_db.*              read-only map queries (the visualize API) + profile table
    profile_reader.*      aggregate a profile CSV per page number (pure, tested)
    visualize_command.*   cpp-httplib server: static assets + query API
    embedded_assets.hpp   accessor for the CMake-embedded front-end assets
tests/                    doctest suite wired into ctest
```

Each command lives in its own `src/` subdirectory; shared code is in
`src/common/`. The commands are independent: `map` and `visualize` do not use the
VFS shim, and `profile` does not use the file-format parser. They meet only at the
1-based page number, which `visualize` uses to join a map to a profile CSV. A
command's directory exposes its surface through its `*_command.hpp` (options +
`run*` entry point); `common/cli` and `main` depend on those headers to dispatch.

## Overhead

The instrumentation adds, per logged page access: two `steady_clock` reads, a
context lookup, the page-index division, and a CSV row write into a 1 MiB
buffer. The hot path performs no heap allocation — the session name is streamed
directly and only quoted/escaped when it contains special characters.

`bench/overhead.cpp` (built as the `sqlinsite_bench` target) quantifies this by
running an identical cold-cache, full-scan read workload through the default VFS
and through the shim, then dividing the wall-time difference by the number of
logged accesses.

Measured on Apple clang (`-O2`, 200k-row table, ~167k page accesses,
warm OS cache): roughly **150–225 ns per page access**, about **6–9%** wall-time
overhead on this read-dominated workload. The cost is dominated by the two clock
reads and the buffered formatting; absolute percentages fall for workloads that
do more per page (joins, expressions) and rise for trivial scans. These numbers
are indicative, not a guarantee — re-run `sqlinsite_bench` on the target host.
