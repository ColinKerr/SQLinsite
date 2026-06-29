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
  an optional profile overlay.

See [plan/CLI.md](./plan/CLI.md) for all parameters and
[examples/](./examples/) for a worked run.

## Prerequisites

- A C++23 compiler (Apple clang 17+ / clang 17+ / GCC 13+ / MSVC 19.35+).
- CMake 3.24+ and a generator (Ninja or Make).
- Network access on first configure: CMake `FetchContent` pulls nlohmann/json,
  doctest, and cpp-httplib. SQLite and D3-free front-end assets are vendored.

## Build & test

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Quick start

```
# profile
sqlinsite profile --test-file app.db --statements queries.json --out-file trace.csv

# map the database structure
sqlinsite map --test-file app.db --out-file app.map.sqlite

# visualize (open the printed URL)
sqlinsite visualize serve --map-file app.map.sqlite --profile-file trace.csv
```

## Documentation

- [plan/ARCHITECTURE.md](./plan/ARCHITECTURE.md) — design and source layout.
- [plan/CLI.md](./plan/CLI.md) — commands and options.
- [plan/commands/](./plan/commands/) — per-command details (profile, map, visualize).
- [plan/TESTING.md](./plan/TESTING.md) — testing approach.
- [plan/STYLE.md](./plan/STYLE.md) — coding style.
- [plan/NEXT_STEPS.md](./plan/NEXT_STEPS.md) — remaining work and future ideas.
