# Pages and Tables Views

Describes the 'Pages' and 'Tables' views activated by the 'Pages' and 'Tables' buttons in the application Top Bar.

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

Navigation is the shared **B-Tree Tree** panel on the **left** (see
B_TREE_TREE.md), the same control and state used by every view — it replaces the
old page-types / tables-indexes list. The information that list carried is already
in the tree: the page-type glyph/color symbology is the tree's key, and each
node's page count and size are shown on the node.

Clicking a tree node scrolls the canvas to that node's target and highlights it:

- **Pages view** — scroll to the node's first **leaf** page (or to a page node's
  own page).
- **Tables view** — scroll to the band of the object the node belongs to (the
  beginning of that object).

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

### Page Top Bar

The page top bar, shared between both views, should be contained within the 'view' area (the canvas area to the **right** of the left-hand Navigation Panel) and not extend into the navigation panel.

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