# Analysis View

The goal of this view is to turn the raw map (and, when present, the CBS manifest and loaded profiles) into **actionable insights** about how efficiently the database uses its storage and its cloud blocks: free space, fragmentation / fill factor, and — the headline for Cloud Backed SQLite — **how many blocks a workload actually pulls**.

Where the Pages, Tables, and Block views *show* the layout, this view *summarizes* it and allows the user to query the map, and if present the profile results and manifest.  It includes a dashboard of derived metrics, each linkable back into the other views and a query editor + results table.

## Availability & inputs

- **Always available** from the map alone: storage-efficiency and fragmentation metrics (the map already classifies freelist/overflow pages and records free bytes per page).
- **Block metrics** appear when a `--manifest-file` is loaded (see BLOCK_VIEW.md) — and the manifest matches the map (`manifestMatch`).
- **Session metrics** appear when a profile source is present (a loaded `--profile-file` and/or interactive Query-view runs; see PAGES_AND_TABLES_VIEWS.md -> Profile overlay).
- **Querying the primary db** (no-prefix base of Query Based Metrics) requires `--db-file`; without it the base is empty and only `map.`/`profile.`/`manifest.` queries work. Predefined metrics never rely on the base, so they are unaffected.

## Navigation Panel

Navigation is the shared **B-Tree Tree** panel on the **left** (see B_TREE_TREE.md).  Selecting a node **scopes** the analysis to that object (a table, its indexes, or a structural group); with nothing selected the analysis is whole-file. The content area to the right is the dashboard. Every metric that names pages/blocks/objects links to the corresponding view (a "reclaimable blocks" figure opens the Block view colored by used-vs-free; a hot object opens the Tables view; etc.).

## Content View

The user can pick between two sub views.  `Predefined Metrics` and `Query Metrics`

### Predefined Metrics

Shows metrics defined ahead of time in this document.

**Implementation.** Each predefined metric is a **canned SQL query over the unified analysis connection** (the same primary + `map` + `profile` + `manifest` connection as Query Based Metrics). They use only the `map.` / `profile.` / `manifest.` prefixes, so storage/block metrics work even without `--db-file`; session metrics need a profile source, block metrics need a manifest (see Availability). Queries re-run when the view
is entered and when the tree **scope** or the selected profile **sources** change (scope injects a `WHERE objectId = …` / source injects the `sel` list). Each metric's SQL is openable in the Query Metrics sub-view for inspection/adaptation.

**Presentation (full charts).** The dashboard is a scrollable set of metric cards: a headline number/gauge plus a chart. Chart types are called out per metric below — free-fraction **histograms**, a block **heatmap** (blocks laid out as in the Block view, colored by the metric), per-object **bar tables**, and a cross-session **overlap matrix**. Clicking a chart element drills into the corresponding view with that scope or source applied.

#### 1. Storage efficiency

- **Free space.** Freelist page count and percentage (`freelistPageCount / pageCount`), and the reclaimable bytes (`freePages * pageSize`) and reclaimable **blocks** (`floor(freePages / pagesPerBlock)` — whole 4 MiB objects that are pure slack). A freshly compacted checkpoint has ~0% free; an incrementally-built briefcase can be a quarter empty (observed: **26.9% free**, ~3 GB of freelist, in one test checkpoint vs 0% in a compacted copy of the same changeset).
- **Fill factor (fragmentation).** The map stores `freeBytes` per page; the average fill factor is `1 - Σ freeBytes / (usedPages * usableSize)`. Low fill factor means the same rows are spread across more pages after many in-place edits — physical bloat beyond the freelist. Report per object and whole-file.
- **Pages per row / overflow ratio.** From `page_row_runs` + `objects`: pages per 1,000 rows, and the share of pages that are overflow — high values flag wide rows or poor packing.
- **Compaction projection.** Estimated size after `VACUUM`: removes the freelist (`pageCount - freePages` pages) and, with defrag, reclaims low-fill slack — expressed in pages, bytes, **and blocks** (the CBS-relevant unit). "This db would shrink from N to M blocks (−X GB of cloud objects)."

*Charts:* a free-vs-used gauge (free %), a per-object fill-factor bar table, and a current-vs-projected blocks bar (compaction).

#### 2. Block analysis (with a manifest)

- **Used vs free per block.** Histogram / list of blocks by free fraction; count of **reclaimable blocks** (≥ threshold free) — whole cloud objects storing mostly slack.
- **Object locality.** For each object, the number of distinct blocks it spans and its density within them. A compact object lives in one block; a scattered object forces a reader to pull many blocks to see it — a cloud-cost signal.
- **Changed vs shared (checkpoint delta).** For a child (delta-encoded) manifest db: how many blocks are shared unchanged with the parent vs new/changed in this version, i.e. the **incremental download size** of this checkpoint (observed: a checkpoint that reused only 22.8% of its parent's blocks — 77% rewritten). Contrast with the full size to show the value of incremental fetch.

*Charts:* a **block free-fraction histogram**, a **block heatmap** (blocks laid out as in the Block view, colored by free fraction) with reclaimable blocks highlighted, a per-object distinct-blocks bar (locality), and a changed-vs-shared stacked bar.

#### 3. Session / profile analysis — "how many blocks does a session load?"

Because CBS fetches **whole blocks**, the cost of a workload is not the pages it touched but the **distinct blocks** those pages fall in. This section makes that explicit, per profile source (a loaded session/statement or an interactive query run; selectable like the Overlay control):

- **Block working set.** For the selection: pages accessed, **distinct blocks touched**, and the **download bytes** those blocks represent (`blocksTouched * szBlk`). This is the real cloud-fetch cost of the workload.
- **Locality ratio** = `pagesAccessed / (blocksTouched * pagesPerBlock)` — how densely the session used the blocks it forced down. A contiguous scan of 1,024 pages pulls **1** block (ratio ≈ 1.0); 500 scattered single-page hits can pull up to **500** blocks (~2 GB at 4 MiB) for a ratio near 0.001. Low locality = expensive random access; the headline insight for optimizing CBS workloads.
- **Read vs write blocks.** Distinct blocks read vs written by the selection (writes imply new block uploads on commit).
- **Cross-session overlap.** Blocks touched by more than one session (warm-cache / prefetch benefit) vs blocks unique to a session; a matrix or Venn-style summary over the selected sources. Answers "if I run A then B, how many blocks does B still need?"
- **Hot vs cold blocks.** Most-accessed blocks (candidates for prefetch/pinning) and never-touched blocks (candidates the workload never needs).

*Charts:* a working-set summary (pages, blocks, download bytes) with a **block heatmap** of touched blocks, a locality-ratio gauge, a read/write split bar, a **cross-session overlap matrix**, and a hot-block heatmap.

Each metric drills into the Block view with that session's overlay applied, so the distinct blocks light up spatially.

#### Derived metrics (reference)

| metric | formula |
|---|---|
| free % | `freePages / pageCount` |
| reclaimable blocks | `floor(freePages / pagesPerBlock)` |
| fill factor | `1 - Σ freeBytes / (usedPages * usableSize)` |
| block working set | `count(distinct blockIndex over accessed pages)` |
| session download bytes | `blocksTouched * szBlk` |
| locality ratio | `pagesAccessed / (blocksTouched * pagesPerBlock)` |
| checkpoint delta blocks | `blocksTotal - blocksSharedWithParent` |


#### Data sources

All computed serve-side from data already present — no new build step and no cloud access:

- **Map:** `meta` (page size/count, freelist count), `pages` (`pageType` for free/overflow, `freeBytes` for fill factor), `objects` + `page_row_runs` (rows, pages per object), `runs` (spans).
- **Manifest:** the block-id array + `szBlk` (BLOCK_VIEW.md) for the page→block map.
- **Profile:** `prof.page_access` filtered by the selected sources (the same store the overlay uses) mapped through page→block for the working-set metrics.

### Query Based Metrics

Allows the user to query the primary database, the map database, the profile database and the manifest as a database.  All attached into **one plain, read-only SQLite connection** — the same "unified analysis connection" the Predefined Metrics are computed over (see below).

- Primary database is the **base** so there is no prefix. It is the `--db-file` (opened read-only). When no `--db-file` is given the base is an empty in-memory db and only prefixed queries work.
- The map database (`--map-file`) is attached read-only with the **`map`** prefix.
- The profile database (the unified `ProfileDb`: `sources`, `page_access`) is attached with the **`profile`** prefix.
- The manifest is exposed as the temp `ManifestDb` (`meta`, `databases`, `blocks`; see BLOCK_VIEW.md) attached with the **`manifest`** prefix.

**Connection model.** A plain SQLite connection (no `sqlinsite` profiling VFS), all attachments read-only. Analysis queries are *meta* queries over the map/profile/manifest — they do **not** profile the primary db and do **not** create new profile sources (unlike the Query view). The connection is reused across runs (no cold-cache requirement). Because it is read-only it cannot modify the primary `.bim`.

This view is setup like the Query view with a query editor on top and a results view on the bottom.  It differs in that the results view only shows the table of results with **no mapping back to the source pages** (no `rowid`→page columns) and there are **no sub tabs** for pages/tables views. Served by `POST /api/analysis/query` (mirrors `/api/query/run` but on the unified connection, no profiling, no history side effects).

### Query Editor

Reuses query editor control from Query View

### Results Table

Reuses results table from Query View