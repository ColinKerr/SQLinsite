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
- `--port` (optional, default `8080`; `0` picks a free port).

> Change note: the previous version shipped the whole map as JSON and drew one
> DOM node per page with D3. That is slow for small databases and impossible for
> large ones (a database may have ~4.3 billion pages). The redesign: the map is a
> queryable SQLite file, the server answers **page-range** queries, and the
> browser draws blocks directly on a **`<canvas>`** (no DOM per page, no D3) with
> zoom and level-of-detail. D3 is removed.

## Server architecture

- **HTTP server:** cpp-httplib v0.48.0 (FetchContent, header-only). No TLS.
- **Data source:** the server opens `--map-file` **read-only** with SQLite and
  answers queries against it directly — it never loads the full map into memory.
  Prepared statements + the map's indexes keep each request O(log n + result).
- **Profile:** if given, the CSV is aggregated once into an in-memory SQLite
  table `profile(pageNumber PRIMARY KEY, reads, writes)` so overlay queries are
  range/aggregate SQL joined on `pageNumber`.
- **Assets:** front-end (`index.html`, `app.js`, `style.css`) embedded in the
  binary via the CMake byte-array generator. No D3.

### Endpoints

All page ranges are inclusive and 1-based.

| Method/Path | Returns |
|---|---|
| `GET /` , `GET /static/*` | embedded assets |
| `GET /api/meta` | `meta` row + `objects` list (id, type, name, rootPage, **pageCount**, colorIndex) + **`typeCounts`** (pages per page type, from `type_counts`) |
| `GET /api/pages?from&to` | per-page rows `{pageNumber, pageType, objectId}` in range (per-block LOD). Rejects with 413 if the range exceeds a server cap; the client must use `/api/runs` instead. |
| `GET /api/runs?from&to` | runs overlapping the range `{startPage, endPage, pageType, objectId}` (zoomed-out LOD) |
| `GET /api/page/:n` | full single-page detail: `pages` row + its `pointers`, `cells`, `ptrmap` entries, and profile `{reads,writes}` |
| `GET /api/profile/pages?from&to` | `{pageNumber, reads, writes}` for touched pages in range (per-block overlay) |
| `GET /api/profile/histogram?from&to&bins` | `bins` equal buckets across the range, each `{reads, writes}` summed (zoomed-out overlay) |

Per-object (Tables view) variants accept `&objectId=` to scope `pages` / `runs`
to a single object's page sequence.

## Front-end (Canvas, no framework)

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

### Right-hand legend panel (resizable)

The right column is a **resizable** panel (drag its left edge; width persisted in
`localStorage`). It lists:

- **Page types** — each type's glyph/color and its **page count** (from
  `/api/meta` `typeCounts`).
- **Tables/indexes** — each object's color, name, and its **page count** (from
  `objects.pageCount`).

Counts give an at-a-glance size breakdown without scanning the canvas. Clicking a
type or object can filter/highlight it (nice-to-have).

### Zoom & pan

- Mouse wheel zooms `blockPx` (clamped), anchored at the cursor so the page under
  the pointer stays put.
- Vertical scroll / drag pans. The grid wraps to canvas width, so navigation is
  one-dimensional (page order).
- Buttons/keys for zoom-to-fit and 1:1.

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
- **Zoomed out:** `/api/profile/histogram` buckets shade runs by access density.
- A control toggles reads | writes | total | off.

## Views

Two tabs, sharing the canvas renderer:

- **Pages** — the whole file as one grid ordered by page number (physical
  layout). This is the view that must scale to billions of pages.
- **Tables** — one band per object, each rendering that object's pages (via the
  `objectId`-scoped endpoints) with the same renderer and LOD. Large tables get
  the same run-based zoomed-out treatment.

## Behavior & edge cases

- Missing/invalid `--map-file` (not a SQLite map) → error, exit 1, no server.
- Port already in use → clear error, exit 1; `--port 0` picks a free port.
- `--profile-file` page numbers beyond the map → counted, surfaced as an
  "unmapped accesses" note.
- Requests for huge per-block ranges are rejected (413) so a client bug can't ask
  the server to serialize billions of rows; the client switches to runs.

## Tests

- **Server (C++):** open a small generated map file, start on an ephemeral port,
  and assert `/api/meta`, `/api/pages`, `/api/runs`, `/api/page/:n`,
  `/api/profile/*` return correct JSON for known fixtures; the per-block range
  cap returns 413; static assets serve with correct content types.
- **Pure logic:** run coalescing and range→bucket math are unit-tested.
- The canvas drawing itself is verified manually (served bytes are checked in
  tests; pixels are eyeballed by running `visualize serve`).

## Future child commands

See NEXT_STEPS: a `visualize static` subcommand emitting a self-contained bundle,
and session/statement filtering in the overlay.
