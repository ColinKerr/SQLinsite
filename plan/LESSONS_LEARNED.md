# Lessons Learned

Catalogs issues and problems that caused a change in architecture or implementation that are helpful for future design and implementation decisions.

## `SQLinsite visualize serve`

### Performance

Original implementation stored the whole map as JSON and drew one DOM node per page with D3. That was too slow. So the updated design stores the map in a SQLite file, the server answers **page-range** queries, and the browser draws blocks directly on the canvas, dropping D3.

## `sqlinsite map`

### Map generation performance (2026-07)

Generating the map was too slow and the map too large (on an 8 GB / ~2 M-page real iModel: ~11 min, ~4 GB map, ~8 GB RAM). A pass of optimizations took it to **~26 s, 575 MB map, ~0.3 GB RAM** — same output, verified byte-equivalent. The lessons, in the order they mattered:

- **Measure on representative (large, cold) data, not small warm-cache benchmarks.** The early wins looked like *31× faster* on a 2 M-row synthetic file, but on the real 8 GB file wall time barely moved — the bottleneck had shifted to phases those wins didn't touch. Small-file profiling is CPU-bound; large-file behavior is dominated by **memory pressure and I/O**. Always profile the phase breakdown on a file that exceeds RAM before declaring victory.

- **Don't load the whole input into memory.** The builder read every page into a `vector<vector<Byte>>` up front (8 GB → 8 GB RSS), which thrashed swap on a 16 GB machine and made *both* the old and new code ~equally slow. Switching to **on-demand page reads** (`sqlite_dbpage WHERE pgno=?`, connection kept open, OS/SQLite caches hold the hot set) cut RSS ~23× and wall time ~25×. This single change dwarfed every algorithmic one on the large file. Cost: a small slowdown on tiny warm files (extra per-page fetch) — a good trade.

- **Store the minimum; derive the rest at query time.** The map persisted decoded index keys (`keyJson`) and a row per *every* cell (54 M rows on that file) — 94 % of map size and most of build time — yet the runtime read almost none of it. Dropping index keys and all table-leaf/index cells (keeping only table-interior cells) and decoding on demand from the source db shrank the map ~7× with no feature loss. Requiring the source db at visualize time for the deepest inspection was an acceptable price.

- **Prefer a C++ pass over a whole-file recursive CTE for tree roll-ups.** `page_row_runs` and `subtreePageCount` were each computed with `WITH RECURSIVE … UNION` over the whole file. On millions of pages these materialize huge temp b-trees on disk and dominate build time. Computing them **in C++ from edges collected during the single parse pass** (leaf runs merged up the tree; an iterative — not recursive — bottom-up sum, so long overflow chains can't blow the stack) removed a ~3.5 s (and, under memory pressure, much larger) phase and needs no extra data structures on disk.

- **Reuse SQLite's own machinery when it's cheaper than hand-rolling.** Object→page assignment was a C++ b-tree traversal that parsed every page just to follow pointers. The `DBSTAT` vtab walks all b-trees in C and names each page's owner in one query; combined with reading the page's header byte for the exact type, it removed a full second parse of every page (and is correct for `WITHOUT ROWID` tables). Enabling one compile flag beat re-implementing the walk.

- **Bump the format version on every incompatible schema change and gate on it.** Each of these changes altered the map schema; the `meta.formatVersion` vs `expectedFormatVersion` check (already in place) meant an old binary never silently queried a dropped column on a new map. Cheap insurance that made the schema churn safe.