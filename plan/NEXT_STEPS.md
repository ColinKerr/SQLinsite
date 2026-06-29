# Next Steps

The three commands (`profile`, `map`, `visualize serve`) build, run, and are
covered by the `ctest` suite. This is the remaining work and the ideas parked for
later.

## Packaging, docs, CI

- [ ] **CI:** GitHub Actions matrix (Linux/macOS, clang/gcc) running
      `cmake --build` + `ctest`. The build is CMake-driven with `FetchContent`
      for nlohmann/json, doctest, and cpp-httplib; SQLite and the front-end
      assets are vendored, so CI only needs a compiler + CMake and network for
      the FetchContent deps on first configure.
- [ ] **Install target:** `cmake --install` placing the `sqlinsite` binary.
- [ ] **.clang-format:** add the config and format the codebase.

## Future / out of scope for now

- **profile:** capture journal/WAL/temp file access behind a flag (currently
  main DB only); profile against a temporary copy of the DB as an opt-in
  alternative to in-place execution; configurable PRAGMAs / setup statements run
  outside measurement.
- **map:** WAL-frame mapping (currently the committed main DB only).
- **visualize:** session/statement filtering in the overlay; a `visualize static`
  subcommand emitting a self-contained bundle; Tables-view object-ordinal run
  aggregation so its zoomed-out level-of-detail matches the Pages view.
