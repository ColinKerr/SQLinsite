# Details for `sqlinsite visualize` command

`visualize` is a parent command with child commands. v1 ships one:

```
sqlinsite visualize serve --map-file <map.sqlite> [--profile-file <run.csv>] [--port <n>]
```

`serve` starts a local web server that hosts an interactive, **canvas-based** view
of a `sqlinsite map` result, optionally overlaid with a `sqlinsite profile` CSV.
It runs in the foreground and prints its URL; stop it with Ctrl-C.

- `--map-file` (required) — the **SQLite** map from `sqlinsite map`.
- `--profile-file` (optional) — CSV from `sqlinsite profile`; enables the
  read/write overlay.
- `--db-file` (optional) - The SQLite file mapped by the `map-file`.  File will be opened read-only.
- `--port` (optional, default `8080`; `0` picks a free port).



## Server architecture

- **HTTP server:** cpp-httplib v0.48.0 (FetchContent, header-only). No TLS.
- **Data source:** the server opens `--map-file` **read-only** with SQLite and
  answers queries against it directly — it never loads the full map into memory.
  Prepared statements + the map's indexes keep each request O(log n + result).
- **Profile:** if given, the CSV is aggregated once into an in-memory SQLite
  table `profile(pageNumber PRIMARY KEY, reads, writes)` so overlay queries are
  range/aggregate SQL joined on `pageNumber`.
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
| `GET /api/meta` | `meta` row + `objects` list (id, type, name, tableName, rootPage, **pageCount**, startPage, startLeafPage) + **`typeCounts`** + `hasProfile`, `hasDb`, and the profile `sessions` manifest |
| `GET /api/pages?from&to` | per-page rows `{pageNumber, pageType, objectId}` in range (per-block LOD). Rejects with 413 if the range exceeds a server cap; the client must use `/api/runs` instead. |
| `GET /api/runs?from&to[&profiled&sel]` | runs overlapping the range `{startPage, endPage, pageType, objectId}` (zoomed-out LOD); with `profiled` they are recomputed to the accessed spans for the selected leaves |
| `GET /api/object/pages?objectId&from&to` | pages of one object by 0-based ordinal window (Tables view) |
| `GET /api/page/:n` | full single-page detail: `pages` row + its `pointers`, `cells`, `ptrmap` entries, and profile `{reads,writes}` |
| `GET /api/profile/pages?from&to[&sel]` | `{pageNumber, reads, writes}` for touched pages in range, filtered to the selected session/query leaves |

Overlay queries accept `&sel=` (comma-separated leaf ids) to scope the profile to
the selected sessions/queries.

**Live query (only when `--db-file` is supplied):**

| Method/Path | Returns |
|---|---|
| `GET /api/schema` | schema tree (tables/views → columns/indexes/triggers) joined to map page counts |
| `POST /api/query/run` (body = SQL) | `{queryId, columns[with provenance], rowCount, truncated, pageCount, accesses, profile}` (400 on SQL error) |
| `GET /api/query/:id/rows?from&to` | a row window `{columns, rows, rowPages, rowCount}` (per-cell page, or null) |
| `POST /api/query/explain` (body = SQL) | `{queryPlan, explain}` |
| `GET /api/query/history[/:id]` | past runs (list, or one run's metadata for restore) |

## Definitions

- `run` - a contiguous set of blocks of the same type, for the same object and marked as accessed at least once by the currently loaded profile settings.
- Top bar - A fixed bar at the top of the app that contains the name of the app and the view picker (a button for each view).
- Page Top Bar - A secondary bar shown only for the Page Views (Pages and Tables) that contains the zoom controls, profile controls, and session/map information.
- View - The main content window of the app that shows details about the blocks or data in the mapped SQLite file
- Navigation Panel - A resizable bar on the right hand side of the screen that contains navigation controls as specified in this document.

## Front-end

A single `<canvas>` per view, sized to its container and scaled for
`devicePixelRatio`. State: `blockPx` (zoom), scroll offset, and the derived
`blocksPerRow = floor(width / blockPx)`.

### Rendering loop

1. From scroll offset + `blockPx`, compute the visible page range `[from, to]`.
2. **Level of detail:**
   - `blockPx >= LOD_THRESHOLD` (e.g. 4px): fetch `/api/pages?from&to`, draw one
     filled rect per page (color = object/structural). At larger `blockPx`
     (e.g. ≥ 14px) also draw the page-type glyph.
   - `blockPx < LOD_THRESHOLD`: fetch `/api/runs?from&to` and draw each run as a
     filled span across the rows it covers — millions of pages collapse to a few
     fills.
3. Drawing happens on `requestAnimationFrame`. To avoid flashing, the grid keeps
   the previously fetched page/run data drawn until the replacement fetch
   resolves (the cache is never cleared up-front), fetches a one-screen overscan
   margin so small scrolls don't refetch, and skips the fetch entirely when the
   visible range is already covered. A superseded-fetch guard discards stale
   responses during rapid scrolling. The profile (bounded by accessed pages) is
   loaded once into a page-number lookup, so the overlay needs no per-scroll
   fetch and applies in both views.

### Color & symbol

- **Color → object** via a stable palette indexed by `objectId` (from
  `/api/meta`); structural pages (overflow/freelist/pointer-map/lock-byte/
  unallocated) use a neutral gray ramp.
- **Symbol → page type** drawn only when blocks are large enough to read.
- No block or run should be colored 100% black.

### Navigation Panel

The right column is a **resizable** panel (drag its left edge; width persisted in
`localStorage`). It lists:

- **Page types** — each type's glyph/color and its **page count** (from
  `/api/meta` `typeCounts`).
- **Tables/indexes** — each object's color, name, and its **page count** (from
  `objects.pageCount`).
  - Tables are root nodes and indexes of that table are child nodes
  - Each node should have the starting block and the object identifier so that clicking on the node will scroll to the first **leaf** block or the beginning of the object in block and table views respectively.
  - Each node should have a count of pages associated with that node

### Top Bar

The top bar should be organized in groups from left to right in this order:

- App Info (left aligned)
  - Application Name
- View picker (left aligned)
  - Pages button
  - Tables button
  - Query button



### Zoom & pan

- ctrl + Mouse wheel zooms `blockPx` (clamped), anchored at the cursor so the page under
  the pointer stays put.
- Vertical scroll / drag pans. The grid wraps to canvas width, so navigation is
  one-dimensional (page order).
- Buttons/keys for zoom-to-fit and 1:1.
  - When zooming the block or run in the upper left hand corner of the view should remain in the upper left hand corner post change to zoom level.
  - Zoom percentage, -, + and Fit buttons are in the Page Top Bar in that order in the `Zoom controls` group.  
    - The scale in percentage is calculated using the formula `current pixels`/`max pixels`.
    - The Fit button best-fits the view: it zooms in or out to the largest level at which the whole view fits vertically — the entire db in the Pages view, or all table bands in the Tables view — or the maximum zoom-out if it cannot fit. When everything fits it scrolls to the top; when it cannot fit the scroll position is left unchanged.

### Hover & popups (level-of-detail aware)

- **Zoomed in (per-block):** the hovered pixel maps to one `pageNumber`; a
  debounced `/api/page/:n` populates a popup with header fields, free bytes,
  rowid/key range, cells, and pointers rendered as **clickable links**. Clicking
  a pointer navigates to the target page (scroll + highlight outline drawn on the
  canvas).
- **Zoomed out (per-run):** the hovered pixel maps to a **run** (already in the
  fetched run set); the popup summarizes the contiguous span — purpose
  (type/object), page range, page count, and profile read/write totals for the
  span. Clicking a run zooms into it.

### Profile overlay

When a profile is loaded:

- **Per-block:** `/api/profile/pages` for the visible range; touched blocks get a
  read/write tint/badge, untouched blocks are drawn lightened.
- **Zoomed out:** each run is shaded like the blocks in the per-block view (by its
  brightest accessed page on the same scale); unaccessed runs are darkened, never
  omitted (so there are no bare-background stripes).
- Control for profile visualization is in the Page Top Bar `profile controls` section
  - The control should be a custom drop down with two sections
    - The first has two checkboxes one for reads and one for writes.
    - The second has checkboxes for each session in the profile and child checkboxes for each query in the session.

### View scroll bar

Each view should have a scroll bar that is a scaled image of the entire view.  Clicking on a location in that scaled image of the view will scroll the view to that location.

## Views

All views must handle billions of pages and or rows.  Pages and Tables tabs sharing a canvas renderer.

- **Page Views** - Views where the content are blocks each representing a page. These views share a 'Page Top Bar' described below.
  - **Pages** — the whole file as one grid ordered by page number (physical
    layout). This is the view that must scale to billions of pages.
  - **Tables** — one band per object, each rendering that object's pages (via the
    `objectId`-scoped endpoints) with the same renderer and LOD. Large tables get
    the same run-based zoomed-out treatment.
- **Query** - This view is only active when the mapped SQLite db file is passed in via the `--db-file` parameter.  See VISUALIZE_LIVE_QUERY_VIEW.md for more details about this view.

### Page Top Bar

The page top bar should be contained within the 'view' area and not extend into the navigation panel.

- Zoom controls (right aligned)
  - Zoom %
  - zoom out (-)
  - zoom in (+)
  - zoom fit (Fit)
- Profile controls (right aligned)
- Session information
  - Map Information
    - Total number of pages
    - Number of unique pages accessed by statements currently selected by profile controls

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

