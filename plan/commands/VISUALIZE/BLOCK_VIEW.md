# Block View

The goal of this view is to visualize how the SQLite file's **pages** map onto the fixed-size **blocks** that Cloud Backed SQLite (CBS) stores in cloud object storage, so a user can see which cloud block holds any page (and which pages/objects share a block). It must scale to dbs with billions of pages — because a block holds ~1024–8192 pages, the block count stays modest (a few thousand for a multi-GB db).

This view requires a CBS `manifest.bcv` for the opened db and mapping between the two is done at the page level. See `https://sqlite.org/cloudsqlite/doc/trunk/www/index.wiki` for details of the manifest format.

The view is made up of three parts

- Block Controls - This fixed top bar controls colorization, zoom and stats about the current db.
- Block View - A canvas control showing each block represented as a square, colored based on the option selected in BlockControls
- Block Detail View - Shows a color key for all colorizations besides 'Object' along the top row, the second row is statistics for the selected block and, under that, another canvas control with the block's page-level details.

The Block Controls top bar is fixed at the top of the content area.  The Block View is below the top bar and above the Block Detail View, 2/3rds Block View, 1/3rd Block Detail View.  The split between the Block View and the Block Details View is adjustable.

## Availability & inputs

The Block button is shown only when a manifest is loaded, gated the same way the Query/Tree buttons are gated on `--db-file` (see the ViewPicker). Inputs:

- `--manifest-file <manifest.bcv>` — the CBS manifest for the container.
- `--manifest-db-name <name>` — which named database in the manifest the `--map-file` corresponds to (a manifest can describe several; e.g. `BASELINE.bim` plus one checkpoint per changeset). Optional.

**Default database selection (auto-match by block count).** When `--manifest-db-name` is omitted, the server picks the **non-deleted, non-`BASELINE` database whose block count equals `ceil(map.pageCount / pagesPerBlock)`** — the one that physically matches the mapped file. If exactly one matches it is used silently; if several match, the highest db id (newest checkpoint) wins. If an explicit name is given it is used as-is.

**Mismatch handling.** If the selected database's block count does **not** equal `ceil(map.pageCount / pagesPerBlock)` (a manifest paired with the wrong map, or an
uncommitted/stale file), the Block view is **disabled** and `/api/meta` reports the reason so the front-end can show a clear "this manifest does not match the mapped database" warning rather than drawing a misaligned mapping.

`/api/meta` gains `hasManifest`, `blockSize`, `pagesPerBlock`, `blockCount`, the selected `manifestDbName`, and `manifestMatch` (bool + reason). The mapping is cheap arithmetic + one block-id array, so it is computed **serve-side from the manifest** (an optional side-input like the profile), not baked into the map.

## Page -> block mapping

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

## Block View (canvas)

The Block View canvas is a **canvas view type** (`view: "blocks"`) in the shared `CanvasController`, reusing its render loop, scroll/zoom, and minimap. It draws blocks in **block order** (a wrapped one-dimensional grid) as a zoomable surface; it sits inside the three-part Block layout (below the Block Controls bar, above the Block Detail View), not in the Pages/Tables Page Top Bar.

- **One cell per block.** Each block is one filled square (color per the active mode below). Block counts are small (a few thousand for a multi-GB db), so most files fit on screen; zoom controls the square size.
- **LOD / scale.** A very large db (millions of blocks) coalesces contiguous same-color blocks into runs, like the Pages view's runs LOD. Endpoints are windowed by block index (see `/api/blocks`).
- The **final block** is partial (`realPages < pagesPerBlock`).
- **Minimap** shows the whole block sequence colored by the active mode.
- Page-level detail is **not** drawn on this canvas (it lives in the Block Detail View); clicking a block selects it and populates the detail view.

### Block Controls (color-mode + zoom + stats)

The fixed top bar holds the **color-mode selector** (below), the **zoom controls**, and **db stats** (selected manifest db, block count, pages/block, block size). The profile color mode reads from the shared, global **Overlay/source selector** (see PAGES_AND_TABLES_VIEWS.md → Profile overlay).

### Color modes (selectable, like the Overlay control)

- **By object** — the object that owns the most pages in the block (the block palette reuses the object palette; blocks that mix objects use the dominant color, matching the Pages minimap).
- **Used vs free** — the fraction of the block's pages that are freelist/unallocated vs used (from the map's page types); a block that is mostly freelist is reclaimable slack. See ANALYSIS_VIEW.md.
- **Changed vs shared** (needs a child manifest db) — blocks shared unchanged with the parent checkpoint vs blocks new/changed in this version, so a user sees how much a changeset rewrote.
- **Profile overlay** — when a profile source is selected, shade each block by whether (and how much of) it was accessed (see PAGES_AND_TABLES_VIEWS.md → Profile overlay); this is the block "working set" of the selection (ANALYSIS_VIEW.md).

### Block Detail View

Selecting a block (clicking it in the Block View canvas, or activating a page node in
the tree) populates the Block Detail View pane. It shows, top to bottom:

- A **color key row** (its own row, above the stats) for the active color mode
  (omitted for the Object mode).
- A **stats row** for the block: block index, **object name** `hex(blockId).bcv`, page
  range `[startPage .. endPage]` + real pages, used vs free (freelist) page counts, the
  block's main object, the **shared-with-parent** flag + parent checkpoint name (child
  manifest dbs), and — when a profile is selected — its read/write totals.
- A **page-level canvas**: the block's pages (`/api/pages` for the block's page range)
  shown like the Pages view — a compact, self-contained mini-canvas (`BlockPageController`)
  with its own **minimap**, **zoom** (−/+/Fit and ctrl-wheel), and **hover popup** (page
  type, owning object, free bytes, cells, read/write totals). It is colored by the color
  mode chosen in the Block Controls: **object** (owner/type, also the base under the
  profile overlay), **used/free** (binary per-page tint), **shared/changed** (the block's
  uniform status), or the **profile** access overlay. It reuses the Pages view's palette /
  overlay / layout helpers rather than the shared `CanvasController`, so the detail pane
  never disturbs the main block canvas.

(Hovering a block also shows a quick summary popup; the pane is the persistent detail.)

## Endpoints

All page ranges are inclusive and 1-based.

| Method/Path | Returns |
|---|---|
| `GET /api/blocks?from&to` | rows for blocks in the block-index window `[from, to]`: `{blockIndex, blockId, startPage, endPage, realPages, dominantObjectId, usedPages, freePages, sharedWithParent}`. Coalesced into runs of same-color adjacent blocks when zoomed out, mirroring `/api/runs`. |
| `GET /api/block/:i` | full detail for block `i`: page range, object mix (`objectId → pageCount` with names), used/free, `blockId`/`objectName`, `sharedWithParent` + `parentName`, and profile read/write totals for the selected sources |
| `GET /api/page/:n` | (existing) gains a `block` field: `{blockIndex, blockId, inBlockOffset}` so the Page Detail and every hover can name a page's cloud block |

## Backend / data source

**Manifest representation — a temp SQLite db (`ManifestDb`).** A small `manifest.bcv` reader (v4 header + per-db headers + full/delta block arrays, resolving the parent chain) parses the manifest **once at serve start** into a temp SQLite database (mirroring `ProfileDb`), attached to the read connections as `manifest`:

```sql
CREATE TABLE meta(formatVersion, blockSize, blockIdSize, nDb, nDelete, maxDbId,
                  selectedDbId, pagesPerBlock, blockCount, manifestDbName,
                  manifestMatch, matchReason);
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
