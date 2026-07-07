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

### Top Bar

The top bar should be organized in groups from left to right in this order:

- App Info (left aligned)
  - Application Name
- View picker (left aligned)
  - Pages button
  - Tables button
  - Query button
  - Page Tree button

## Views

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

