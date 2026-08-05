# Details for `sqlinsite visualize` command

`visualize` is a parent command with child commands. v1 ships one:

```
sqlinsite visualize serve --map-file <map.sqlite> [--profile-file <run.sqlite>] [--port <n>]
```

`serve` starts a local web server that hosts an interactive, **canvas-based** view
of a `sqlinsite map` result, optionally overlaid with a `sqlinsite profile` database.
It runs in the foreground and prints its URL; stop it with Ctrl-C.

- `--map-file` (required) — the **SQLite** map from `sqlinsite map`.
- `--profile-file` (optional) — the profile **SQLite database** from `sqlinsite
  profile`; imported as loaded ('input') sources of the read/write overlay.
- `--db-file` (optional) - The SQLite file mapped by the `map-file`.  File will be opened read-only. Enables the Query view and is the no-prefix base of the Analysis Query Metrics.
- `--manifest-file` (optional) — a Cloud Backed SQLite `manifest.bcv`; enables the **Block** view and the block metrics of the **Analysis** view (see BLOCK_VIEW.md, ANALYSIS_VIEW.md).
- `--manifest-db-name` (optional) — which named database in the manifest the map corresponds to (defaults to the block-count match; mismatch disables the Block view).
- `--port` (optional, default `8080`; `0` picks a free port).



## Server architecture

- **HTTP server:** cpp-httplib v0.48.0 (FetchContent, header-only). No TLS.
- **Data source:** the server opens `--map-file` **read-only** with SQLite and
  answers queries against it directly — it never loads the full map into memory.
  Prepared statements + the map's indexes keep each request O(log n + result).
- **Profile:** a single on-disk **temp SQLite database** (`ProfileDb`, WAL) holds
  every profile *source* in two tables — `sources(sourceId, kind, sessionName,
  sessionId)` and `page_access(sourceId, pageNumber, reads, writes)`. A
  `--profile-file` is imported as `kind='input'` sources; each interactive
  Query-view run is added as a `kind='query'` source (see the live-query doc). Every
  read connection `ATTACH`es it as `prof`, so overlay queries are range/aggregate SQL
  over `prof.page_access` filtered by the selected `sourceId`s. One store means any
  profile — loaded or interactive — can be overlaid in any view.
- **Assets:** the React + TypeScript front-end (source in `web/`, bundled by
  Vite) is built as part of the normal CMake build into `web_build/` (gitignored,
  never committed) and embedded into the binary by a build-time byte-array
  generator. The build emits `index.html`, `sqlinsite.js`, `sqlinsite.css` plus
  Monaco's editor worker and SQL chunk, each served from `/static/<name>`.
- **Live query (`--db-file`):** `QueryEngine` runs user SQL on a fresh read-only
  connection through the profiling VFS; runs are serialized (the VFS context is
  process-global) and cached with an in-memory history.

### Endpoints

All page ranges are inclusive and 1-based.

| Method/Path | Returns |
|---|---|
| `GET /` , `GET /static/*` | embedded assets |
| `GET /api/meta` | `meta` row + `objects` list (id, type, name, tableName, rootPage, **pageCount**, startPage, startLeafPage) + **`typeCounts`** + `hasProfile`, `hasDb`, and (when a manifest is loaded) `hasManifest`, `blockSize`, `pagesPerBlock`, `blockCount`, `manifestDbName`, `manifestMatch` |
| `GET /api/pages?from&to` | per-page rows `{pageNumber, pageType, objectId}` in range (per-block LOD). Rejects with 413 if the range exceeds a server cap; the client must use `/api/runs` instead. |
| `GET /api/runs?from&to` | runs overlapping the range `{startPage, endPage, pageType, objectId}` (zoomed-out LOD), from the map's pre-coalesced `runs` table. The profile overlay is applied client-side. |
| `GET /api/object/pages?objectId&from&to` | pages of one object by 0-based ordinal window (Tables view) |
| `GET /api/page/:n` | full single-page detail: `pages` row + its `pointers`, `cells`, `ptrmap` entries, and profile `{reads,writes}` |
| `GET /api/profile/pages?from&to[&sel]` | `{pageNumber, reads, writes}` for touched pages in range, filtered to the selected profile sources |
| `GET /api/profile/sources` | the profile selection tree: `{sources:[{sourceId, kind, sessionName, sessionId}]}` (loaded 'input' + interactive 'query'); refetched by the client after each run |
| `GET /api/blocks?from&to` | Block view: one row per block in the index window (or coalesced runs zoomed out) — `{blockIndex, blockId, startPage, endPage, realPages, dominantObjectId, usedPages, freePages, sharedWithParent}` (see BLOCK_VIEW.md) |
| `GET /api/block/:i` | full detail for block `i` (object mix, used/free, blockId/object name, shared-with-parent, profile totals) |
| `POST /api/analysis/query` | runs SQL on the **unified analysis connection** (primary base + `map`/`profile`/`manifest` attached, read-only, no profiling); returns a plain results table (see ANALYSIS_VIEW.md) |

Overlay queries accept `&sel=` (comma-separated `sourceId`s) to scope the profile to
the selected sources (loaded statements and/or interactive query runs).

**Live query (only when `--db-file` is supplied):**

| Method/Path | Returns |
|---|---|
| `GET /api/schema` | schema tree (tables/views → columns/indexes/triggers) joined to map page counts |
| `POST /api/query/run` (body = SQL) | `{queryId, columns[with provenance], rowCount, truncated, pageCount, accesses, profile, profileDeferred}` (400 on SQL error; `{cancelled:true}` if interrupted). Large profiles are deferred (empty `profile.pages`, `profileDeferred:true`) and fetched via `/profile` |
| `POST /api/query/cancel` | interrupts the in-flight run (served on another thread); that run returns `{cancelled:true}` |
| `GET /api/query/:id/rows?from&to` | a row window `{columns, rows, rowPages, rowCount}` (per-cell page, or null) |
| `GET /api/query/:id/profile` | a run's full per-page profile `{pages}` (fetched on demand for the map overlay) |
| `POST /api/query/explain` (body = SQL) | `{queryPlan, explain}` |
| `GET /api/query/history[/:id]` | past runs (list, or one run's metadata for restore) |

## Definitions

- `run` - a contiguous set of blocks of the same type, for the same object and marked as accessed at least once by the currently loaded profile settings.
- Top bar - A fixed bar at the top of the app that contains the name of the app and the view picker (a button for each view).
- Page Top Bar - A secondary bar shown only for the Page Views (Pages and Tables) that contains the zoom controls, profile controls, and session/map information.
- View - The main content window of the app that shows details about the blocks or data in the mapped SQLite file
- Navigation Panel - A resizable panel on the **left** hand side of the screen, shared by every view, whose content is the **B-Tree Tree** navigation control (see B_TREE_TREE.md). The panel (drag its right edge; width persisted in `localStorage`) and its tree are visually identical and keep their state — selection, expansion, scroll position, and the node search — when switching views. Only the main content shown to its right, and the way a click on a tree node is handled, change from view to view.

## Front-end

### Top Bar

The top bar should be organized in groups from left to right in this order:

- App Info (left aligned)
  - Application Name
- View picker (left aligned)
  - Pages button
  - Tables button
  - Query button
  - Page Tree button
- History navigator (right aligned)
  - Back button
  - Forwards button

## Views

Every view lays out the same way: the shared B-Tree Tree Navigation Panel on the
left (see B_TREE_TREE.md), and that view's own main content on the right. The tree
is one shared, view-agnostic instance kept mounted across view switches, so it
looks and behaves identically in every view. It holds no per-view logic; instead
each view **injects** (registers) its own handler for what activating a node does
— scroll the canvas, run a query, show page detail, … — and the tree calls the
active view's handler.

All views must handle billions of pages and or rows.  Pages and Tables tabs sharing a canvas renderer.

- **Page Views** - Views where the content are blocks each representing a page. See PAGES_AND_TABLES_VIEWS.md for more details.
  - **Pages** — the whole file as one grid ordered by page number (physical
    layout). This is the view that must scale to billions of pages.  
  - **Tables** — one band per object, each rendering that object's pages (via the
    `objectId`-scoped endpoints) with the same renderer and LOD. Large tables get
    the same run-based zoomed-out treatment.
-- **Live Views** These views are only active when the mapped SQLite db file is passed in via the `--db-file` parameter.
- **Query** - Execute queries and see results and how they map to pages in the db.  See QUERY_VIEW.md for more details.
- **Page Tree** - Shows the b-trees that make up the SQLite db and lets you drill down into the details of each individual page.

## History navigator

The view and the selected node in the navigation panel are added to the URL when opening a view or selecting a node in navigation panel.  A history of prior selected view+node are stored in local storage.

### UI

The user interacts with the history via two split buttons, Back and Forward.  The main portion of each button has a back or forward arrow icon.  The secondary portion of the split button opens a drop down with history in that direction.

## Behavior & edge cases

- Missing/invalid `--map-file` (not a SQLite map) → error, exit 1, no server.
- Port already in use → clear error, exit 1; `--port 0` picks a free port.
- `--profile-file` page numbers beyond the map → counted, surfaced as an
  "unmapped accesses" note.
- Requests for huge per-block ranges are rejected (413) so a client bug can't ask
  the server to serialize billions of rows; the client switches to runs.

## Tests

- **Server (C++, doctest):** open a small generated map file, start on an
  ephemeral port, and assert `/api/meta`, `/api/pages`, `/api/runs`,
  `/api/page/:n`, `/api/profile/*` return correct JSON for known fixtures; the
  per-block range cap returns 413; static assets serve with correct content
  types. With a `--db-file`, assert `/api/schema` and `/api/query/*` (run, rows,
  explain, history), `hasDb`, and row→page mapping (single-table cells resolve;
  expressions/aggregates stay unresolved).
- **Pure logic (C++):** run coalescing, the `AggregatingSink`, and the rowid
  query augmentation are unit-tested.
- **Front-end (TypeScript, Vitest):** the `core/` modules (palette, overlay,
  layout/zoom math, profile aggregates, api) and the query store (windowed row
  loading, history restore) are unit-tested. Canvas pixels are eyeballed by
  running `visualize serve` (served bytes are checked in tests).

