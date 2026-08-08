# `sqlinsite map` performance review

Goal: make `sqlinsite map` build faster **without** slowing `visualize serve`.
Tracked dimensions: **build wall time**, **peak RSS**, **generated map size**.

## How this was measured

- **Harness** (`perf/`, enhanced for this task): added `--map-only` (build the map
  and record time / peak RSS / **map file size**, skipping serve+browser) plus map
  metrics in the baseline/regression comparison (`map build ms`, `map peak RSS MB`,
  `map size MB`). Run e.g. `npm run perf -- --db <file> --map-only`.
- **Phase timing**: `SQLINSITE_MAP_TIMING=1 sqlinsite map …` prints per-phase wall
  time to stderr (env-gated, zero cost otherwise). This is the reliable signal —
  end-to-end wall clock on this machine is noisy under memory pressure.
- **Files**: synthetic (`perf/generate.ts`) at several scales + real `.bim` files.

## Recommendations

**1. Eliminate `dbstat` — the single biggest lever (~43% of real-file build).**
Attribute pages to objects from the pointer graph the parse pass already produces,
instead of a separate dbstat walk. Design: sequential parse builds a candidate
child/overflow edge graph; then traverse from each schema root (`objects.rootPage`)
to assign object + true page type (a page's role is unambiguous once reached via the
tree, which resolves the overflow-vs-btree ambiguity that dbstat exists to solve).
Expected **~30–40% further build-time reduction** (removes ~9 s on 1.8M-page files;
proportionally more on the 8.6M-page 35 GB files). **Risk: high** — touches
correctness-critical classification (WITHOUT ROWID tables, auto-vacuum pointer-map
pages, freelist, corrupt pages); needs careful implementation and dedicated tests.

**2. Shrink the map (helps both size and write time).** `pointers` + its two
indexes are 42% of the file. Options, each requiring a matching serve-side change:
(a) store `kind` as a small INT code instead of TEXT; (b) drop `overflow`-kind rows
(the next-overflow link is derivable and the chain is already in the graph) after
confirming the ancestors/`/content` queries don't need them.

**3. Avoid the subtree-count UPDATE pass** (5–13% of build). It rewrites every
`pages` row a second time. Fold `subtreePageCount` into the initial INSERT (compute
the child/overflow adjacency in a light pre-pass) or write it to a side table.
Lower priority and lower risk than #1.
