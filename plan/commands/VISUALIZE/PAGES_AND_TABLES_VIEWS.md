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

### Tables-view bands

The Tables view draws one **band** per group: first every schema object (table /
index, from `/api/meta`), then a band for each present **structural page group** —
**Freelist**, **Lock-Byte**, **Pointer-map**, and **All other pages** (unowned pages
of no other structural group). These mirror the b-tree tree's structural roots (see
B_TREE_TREE.md). Overflow pages belong to their owning table, so they sit in that
table's band. Together the bands cover every page in the file — no page is omitted
from the Tables view. Structural-band blocks are colored per page type (gray ramp);
object-band blocks by the object palette. Activating any page node (object or
structural) in the tree scrolls to and selects that page's block in its band.

**Runs are shared with the Pages view.** Zoomed out, an object band fetches
`/api/object/runs` — the same runs as `/api/runs` filtered by that `objectId` (one
per physical run, no extra coalescing), each carrying its page range plus its
position in the band's packed ordinal space. Because the runs carry page numbers,
the profile overlay and hit-testing apply to them exactly as in the Pages view, so a
loaded profile no longer forces the Tables view back to per-page rendering.

### Navigation Panel

Navigation is the shared **B-Tree Tree** panel on the **left** (see
B_TREE_TREE.md), the same control and state used by every view — it replaces the
old page-types / tables-indexes list. The information that list carried is already
in the tree: the page-type glyph/color symbology is the tree's key, and each
node's page count and size are shown on the node.

The Pages and Tables views **register** their node-activation handler with the
shared tree (the tree updates the selection highlight and calls the handler). The
handler scrolls the canvas to the node's target:

#### Linking Behavior

Clicking a tree node **never switches the content view** — it scrolls/selects within
the view that is already showing (a page node in the Tables view scrolls to that
page's block inside its object band, not to the Pages grid).

- **Pages view**
  - Clicking on the table grouping node - scroll to and select the first **leaf** page for the table.
- **Tables view**
  - Clicking on the table grouping node - scroll to the band of the object the node belongs to (the
  beginning of that object).
- **Pages view** and **Tables view**
  - Clicking on any index grouping node - no action
  - Clicking on any page node - Scroll to and select the block that represents the page node selected

### Zoom & pan

- ctrl + Mouse wheel zooms `blockPx` (clamped), anchored at the cursor so the page under
  the pointer stays put. In the Tables view the anchor is resolved in the band's packed
  ordinal space (the row under the cursor is held fixed as the column count reflows).
- Clicking a run while zoomed out zooms into it, anchored at the run's start (top-left in
  the Pages view, the run's position in its band in the Tables view).
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

When any profile source is present (a loaded `--profile-file` and/or an interactive
Query-view run):

- **Per-block:** `/api/profile/pages` (filtered by the selected sources via `&sel=`)
  for the visible range; touched blocks get a read/write tint/badge, untouched blocks
  are drawn lightened.
- **Zoomed out:** each run is shaded like the blocks in the per-block view (by its
  brightest accessed page on the same scale); unaccessed runs are darkened, never
  omitted (so there are no bare-background stripes).
- The overlay is the **union of the selected profile sources**, so loaded profiles
  and interactive query runs shade the same views (see [VISUALIZE.md](./VISUALIZE.md)).
- Control for profile visualization is in the Page Top Bar `profile controls` section
  - The control should be a custom drop down with two sections
    - The first has two checkboxes one for reads and one for writes.
    - The second lists the profile **sources** (from `/api/profile/sources`): the
      loaded-profile sessions (with their statements) and a **Queries** group of
      interactive runs, each a checkbox that toggles that source in the overlay.

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