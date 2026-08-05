# Block View

The goal of this view is to visualize how the SQLite file's **pages** map onto the fixed-size **blocks** that Cloud Backed SQLite (CBS) stores in cloud object storage, so a user can see which cloud block holds any page (and which pages/objects share a block). It must scale to dbs with billions of pages — because a block holds ~1024–8192 pages, the block count stays modest (a few thousand for a multi-GB db).

This view uses the CBS `manifest.bcv` format (see the research notes and `https://sqlite.org/cloudsqlite/doc/trunk/www/index.wiki`). The `.bim`/`.bcv` mapping is a pure, offline, contiguous, offset-based one — no cloud round-trip is needed.

## Availability & inputs

The Block button is shown only when a manifest is loaded, gated the same way the Query/Tree buttons are gated on `--db-file` (see the ViewPicker). Inputs:

- `--manifest-file <manifest.bcv>` — the CBS manifest for the container.
- `--manifest-db-name <name>` — which named database in the manifest the `--map-file` corresponds to (a manifest can describe several; e.g. `BASELINE.bim` plus one checkpoint per changeset). Optional.

**Default database selection (auto-match by block count).** When `--manifest-db-name` is omitted, the server picks the **non-deleted, non-`BASELINE` database whose block count equals `ceil(map.pageCount / pagesPerBlock)`** — the one that physically matches the mapped file. If exactly one matches it is used silently; if several match, the highest db id (newest checkpoint) wins. If an explicit name is given it is used as-is.

**Mismatch handling.** If the selected database's block count does **not** equal `ceil(map.pageCount / pagesPerBlock)` (a manifest paired with the wrong map, or an
uncommitted/stale file), the Block view is **disabled** and `/api/meta` reports the reason so the front-end can show a clear "this manifest does not match the mapped database" warning rather than drawing a misaligned mapping.

`/api/meta` gains `hasManifest`, `blockSize`, `pagesPerBlock`, `blockCount`, the selected `manifestDbName`, and `manifestMatch` (bool + reason). The mapping is cheap arithmetic + one block-id array, so it is computed **serve-side from the manifest** (an optional side-input like the profile), not baked into the map.

## Page → block mapping

For 1-based page `P` (`pageSize` from the map's `meta`, `szBlk` from the manifest):

```
blockIndex  = (P-1) * pageSize / szBlk          // integer division; contiguous
blockId     = fullBlockArray[manifestDb][blockIndex]
blockObject = hex(blockId) + ".bcv"
inBlockOff  = (P-1) * pageSize % szBlk
pagesPerBlock = szBlk / pageSize                // 1024 @ 4 KiB, 8192 @ 512 B
```

Notes that the view must honor:

- **Every page lies wholly in one block** (`szBlk` is a power of two ≥ `pageSize`).
- **Block count comes from the page count, not the file size.** The `.bim` is padded up to a whole number of blocks, so `fileSize/pageSize` overcounts; use the map's `meta.pageCount` (the real SQLite pages). The **final block is partial** — it holds `pageCount - (blockCount-1)*pagesPerBlock` real pages plus padding.
- **Child databases are delta-encoded.** Reconstruct the selected db's full ordered block array by taking its parent's array and applying its `(blockIndex, blockId)` overrides, up the parent chain. A block whose id equals the parent's at the same index is **shared/unchanged**; the rest are **new/changed in this version**.
- Block ids at 16 bytes are container-scoped (not global content hashes), so sharing is only within a container (parent → child), never across containers.

## Navigation Panel

Navigation is the shared **B-Tree Tree** panel on the **left** (see B_TREE_TREE.md), the same control and state as every other view. The block visualization occupies the content area to its right.

### Linking Behavior

The Block view **registers** its node-activation handler with the shared tree (clicking a node **never switches the content view**; it scrolls/selects within the blocks shown):

- **Page node** — scroll to and select the block that contains that page; the block's detail is shown and the page's slot within the block is highlighted.
- **Table / index grouping node** — scroll to and highlight the block range the object spans; blocks are outlined to show how the object is distributed across blocks (a compact object -> one block; a scattered object -> many).
- Activating a block (in the content area) that owns a single object selects that object in the tree (mirrors the Tables-view behavior).

## Blocks visualization (content area)

The Block view is a **new canvas view type** (`view: "blocks"`) in the shared `CanvasController`, reusing its render loop, scroll/zoom, minimap, hover popups, and
the Page Top Bar — the same machinery as Pages/Tables. It draws blocks in **block order** (a wrapped one-dimensional grid), and is a single **zoomable surface**:

- **Zoomed out — one cell per block.** Each block is one filled cell (color per the active mode below). This is the natural overview: block counts are small (a few
  thousand for a multi-GB db), so most files fit on screen.
- **Zoomed in — page slots inside each block.** As `blockPx` grows past a threshold, each block cell subdivides into its `pagesPerBlock` page slots (a mini page grid), drawn with the page-level coloring (object/type) and a **block separator/gutter** between blocks, so a page node from the tree can be highlighted in its exact slot.
- **LOD / scale.** Zoomed out uses per-block cells; a very large db (millions of blocks) coalesces contiguous same-color blocks into runs, exactly like the Pages view's runs LOD. Endpoints are windowed by block index (see `/api/blocks`).
- The **final block** is drawn partially filled to reflect its real-page count vs padding (`realPages < pagesPerBlock`).
- **Minimap** (the shared scroll-bar minimap) shows the whole block sequence colored by the active mode, fixed like the Tables minimap.

### Color-mode control

The color mode (below) is chosen in a **Block-view control in the Page Top Bar** (shown only in this view), alongside the shared, global **Overlay/source selector** (see PAGES_AND_TABLES_VIEWS.md → Profile overlay) that the profile color mode reads from.

### Color modes (selectable, like the Overlay control)

- **By object** — the object that owns the most pages in the block (the block palette reuses the object palette; blocks that mix objects use the dominant color, matching the Pages minimap).
- **Used vs free** — the fraction of the block's pages that are freelist/unallocated vs used (from the map's page types); a block that is mostly freelist is reclaimable slack. See ANALYSIS_VIEW.md.
- **Changed vs shared** (needs a child manifest db) — blocks shared unchanged with the parent checkpoint vs blocks new/changed in this version, so a user sees how much a changeset rewrote.
- **Profile overlay** — when a profile source is selected, shade each block by whether (and how much of) it was accessed (see PAGES_AND_TABLES_VIEWS.md → Profile overlay); this is the block "working set" of the selection (ANALYSIS_VIEW.md).

### Block detail (hover popup / selection)

Hovering or selecting a block shows:

- Block index and **object name** `hex(blockId).bcv`.
- Page range `[startPage .. endPage]` and real pages vs padding (for the last block).
- Objects present in the block and their page counts (the block's object mix).
- Used vs free (freelist) page counts.
- **Shared with parent** flag + parent checkpoint name (child manifest dbs).
- When a profile is selected: pages of this block that were read/written, so the block is shown as fully / partially / not accessed.

## Endpoints

All page ranges are inclusive and 1-based.

| Method/Path | Returns |
|---|---|
| `GET /api/blocks?from&to` | rows for blocks in the block-index window `[from, to]`: `{blockIndex, blockId, startPage, endPage, realPages, dominantObjectId, usedPages, freePages, sharedWithParent}`. Coalesced into runs of same-color adjacent blocks when zoomed out, mirroring `/api/runs`. |
| `GET /api/block/:i` | full detail for block `i`: page range, object mix (`objectId → pageCount`), used/free, `blockId`/object name, shared-with-parent + parent name, and profile read/write totals for the selected sources |
| `GET /api/page/:n` | (existing) gains a `block` field: `{blockIndex, blockId, inBlockOffset}` so the Page Detail and every hover can name a page's cloud block |

## Backend / data source

**Manifest representation — a temp SQLite db (`ManifestDb`).** A small `manifest.bcv` reader (v4 header + per-db headers + full/delta block arrays, resolving the parent chain) parses the manifest **once at serve start** into a temp SQLite database (mirroring `ProfileDb`), attached to the read connections as `manifest`:

```sql
CREATE TABLE meta(formatVersion, blockSize, blockIdSize, nDb, nDelete,
                  maxDbId, selectedDbId);
CREATE TABLE databases(id INTEGER PRIMARY KEY, parent, version, name,
                       blockCount, entryCount, deleted, isSelected);
-- Fully-resolved ordered block arrays for every non-deleted db (small: a few
-- thousand rows each). blockId is stored as lowercase hex; the object name is
-- blockId || '.bcv'. sharedWithParent = the id equals the parent's at this index.
CREATE TABLE blocks(dbId INTEGER, blockIndex INTEGER, blockId TEXT,
                    sharedWithParent INTEGER, PRIMARY KEY(dbId, blockIndex));
```

Exposing the manifest as SQL means the Block view, the Page Detail block field, and the Analysis view (see ANALYSIS_VIEW.md) all read it the same way, with no cloud access and no new map-build step.

**Per-block aggregation.** Because `pagesPerBlock` is constant, a page's block index is `(pageNumber-1)/pagesPerBlock`, so `/api/blocks`/`/api/block/:i` are one grouped join of the map's `pages` against `manifest.blocks` for the selected db:

```sql
SELECT b.blockIndex, b.blockId, b.sharedWithParent,
       count(*)                             AS pages,
       sum(p.pageType IN ('freelist-trunk','freelist-leaf','unallocated')) AS freePages
  FROM manifest.blocks b
  JOIN pages p ON (p.pageNumber - 1) / :pagesPerBlock = b.blockIndex
 WHERE b.dbId = :selectedDbId AND b.blockIndex BETWEEN :from AND :to
 GROUP BY b.blockIndex
```

`dominantObjectId` is the `objectId` with the most pages in the block (a windowed `GROUP BY blockIndex, objectId` picking the max; ties → lowest id). Results are cached per selected db (the manifest and map are static). The profile read/write totals per block come from the same page→block grouping over `prof.page_access` filtered by the selected sources.
