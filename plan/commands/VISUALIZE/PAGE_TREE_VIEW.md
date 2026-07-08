# Page Tree View

The goal of this view is to visualize and inspect the b-tree structures in a SQLite file and drill down into the individual pages.  It must scale to dbs with billions of pages.

This view uses the description of the file format found here: https://sqlite.org/fileformat.html and a similar strategy is used as the `sqlinsite map` command describe in MAP.md Approach section.

The view is split into two resizable areas vertically.  On the left hand side is the 'b-tree tree' which is a virtualized tree view containing all pages.  On the right hand side is the 'page detail view' showing the contents of the page selected in the 'b-tree tree'.

## b-tree tree


### Root nodes

- 'sqlite_schema' root node.  Page 1 for the sqlite file serves as the root node.
- Each table b-tree in the file is a root node.  The root page for that table serves as the root node.
- Each index b-tree in the file is a root node.  The root page for that index serves as the root node.
- The freelist.  The root page is a virtual node whose children are the freelist trunk pages.
- Lock-Byte root node.  Only shown if the Lock-Byte page exists.  The Lock-Byte page serves as the root node.
- All other pages.  Only shown if pages exist that are not covered in the root node descriptions above are grouped together under this root node for review, the final implementation will properly categorize these pages.

### Child nodes and expansion

Each page node it's child pages as child nodes.  If a page node points to overflow pages they are shown as children.

Nodes with children have arrows indicating that they can be expanded.  Clicking on the arrow expands/collapses the nodes children, clicking anywhere else on the node selects the node.  The arrow points to the right when collapsed and down when expanded.

### Tree Key

A key describing the page-type symbology.  Fixed at the bottom of the b-tree area and does not scroll.  Entries in the key are arranged in two or three columns depending on the width of the b-tree tree.

### Styling

Page nodes are styled by page type and include a pop over with a text description of their type and basic details about the page.

The b-tree tree is on the left hand side and has a similar width to the 'Navigation Panel' in other views.  The tree key is fixed at the bottom and the tree itself fills the entire remaining vertical space above the key.

## Page Detail View

Shows the full contents of the page node selected in the b-tree tree.  

If pointer to another page exists in the header or cell records it is shown as a clickable control matching the node in the b-tree tree.  Clicking on the control navigates to the Page Detail View for that page and expands and selects the node in the b-tree tree.

### Horizontal Schematic View

A fixed section of the Page Detail View including the page number, header info and schematic view.

The horizontal schematic view of the page that includes all header, cell, key and pointer information.  The schematic approximately represents the byte size of each portion of the page, including free bytes.  Clicking on the sections of the schematic scrolls to the detailed data shown in the Full Page Contents area.

### Full Page Contents

An area below the schematic view where the full contents of the page is displayed, including contents of the headers, cell pointer array and cells.  The data is shown in a tabular view below the schematic view that is color coded to the schematic view.  

#### Values from overflow pages

When a cell/record overflows into overflow pages show the data from the overflow pages in the full page contents for the leaf page.  

Depending on the type and how data is split it should be handled in one of the following ways
- If an entire value comes from an overflow page the value is written followed by '-> overflow [clickable page control]'.  The value and the clickable page control should be colored the same.
- If a value spans multiple pages it is color coded to match the following rules
  - The portion from the leaf page is white
  - If the value is variable length the type area should include the total number of bytes then a colored segment with the number of bytes from each overflow page followed by a clickable page control for each overflow.  
    - If the type is a string, the portion of the value from each overflow page shares the color giving to the clickable page control for that page.
    - If the type is a BLOB the number of bytes in each page are shown as such: `BLOB (42 bytes, 3000 bytes -> p32, 2700 bytes -> p33)` where 42 bytes are in the leaf page and 3000 bytes are in overflow page 32 and 2700 bytes are in overflow page 33.

If an overflow page is selected in the b-tree tree the leaf or internal page that owns the overflow page should be used to interpret the data in the overflow page to show the Full Page Contents of the overflow page. The owning page is shown as a clickable page control ("owned by [p_owner]"). The Full Page Contents shows only the data that physically lives on this overflow page: the owning cell restricted to the column value slices carried by this page (a column with no bytes on this page is omitted, and a column that overflows onto this page shows only its slice — its byte count for a BLOB, its text fragment for a string).

### Page View Detail Key

A key bar describes the color coding; it is fixed at the bottom of the page detail view and does not scroll with the schematic and table content above it.