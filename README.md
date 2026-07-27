# SQLinsite

A CLI tool to help you understand SQLite page access. It can profile which pages
your SQL statements read and write, map a database file's on-disk structure, and
visualize both in the browser.

## Commands

- `sqlinsite profile` — run statements through a wrapping SQLite VFS and record
  every page read/write to a CSV.
- `sqlinsite map` — parse a database file's structure into an indexed SQLite
  map (pages, objects, cells, pointers, runs).
- `sqlinsite visualize serve` — serve an interactive canvas view of a map, with
  an optional profile overlay. Pass `--db-file` to also enable a live **Query**
  view: run SQL against the mapped database and see the pages each query touches.

See [plan/CLI.md](./plan/CLI.md) for all parameters and
[examples/](./examples/) for a worked run.

## Prerequisites

- A C++23 compiler (Apple clang 17+ / clang 17+ / GCC 13+ / MSVC 19.35+).
- CMake 3.24+ and a generator (Ninja or Make).
- Network access on first configure: CMake `FetchContent` pulls nlohmann/json,
  doctest, and cpp-httplib. SQLite is vendored.
- **Node 20+ and npm.** The `visualize` front-end (a React + TypeScript app in
  `web/`) is built as part of the normal build; its output goes to `web_build/`
  (gitignored — never committed) and is embedded into the binary.

## Build & test

The normal build builds **both** the front-end and the native backend:

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Build just one side:

```
cmake --build build --target frontend   # Vite build → web_build/ (npm)
cmake --build build --target backend     # native binary (rebuilds the front-end only if stale)
```

Front-end unit tests (Vitest): `cd web && npm test`.

## Quick start

```
# profile
sqlinsite profile --test-file app.db --statements queries.json --out-file trace.csv

# map the database structure
sqlinsite map --test-file app.db --out-file app.map.sqlite

# visualize (open the printed URL)
sqlinsite visualize serve --map-file app.map.sqlite --profile-file trace.csv

# visualize + live Query view (read-only db access)
sqlinsite visualize serve --map-file app.map.sqlite --db-file app.db
```

## Documentation

- [plan/ARCHITECTURE.md](./plan/ARCHITECTURE.md) — design and source layout.
- [plan/CLI.md](./plan/CLI.md) — commands and options.
- [plan/commands/](./plan/commands/) — per-command details (profile, map, visualize).
- [plan/TESTING.md](./plan/TESTING.md) — testing approach.
- [plan/STYLE.md](./plan/STYLE.md) — coding style.
- [plan/NEXT_STEPS.md](./plan/NEXT_STEPS.md) — remaining work and future ideas.
