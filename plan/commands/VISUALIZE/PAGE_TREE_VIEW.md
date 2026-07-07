# Page Tree View

The goal of this view is to visualize and inspect the b-tree structures in a SQLite file and drill down into the individual pages.  It must scale to dbs with billions of pages.

This view uses the description of the file format found here: https://sqlite.org/fileformat.html and a similar strategy is used as the `sqlinsite map` command describe in MAP.md Approach section.

The view is split into two resizable areas vertically.  On the left hand side is the 'b-tree tree' which is a virtualized tree view containing all pages.  On the right hand side is the 'page detail view' showing the contents of the page selected in the 'b-tree tree'.

## b-tree tree

The root nodes of this tree are:

- Page 1
- Each table b-tree in the file is a root node.  The root page for that table serves as the root node.
- Each index b-tree in the file is a root node.  The root page for that index serves as the root node.
- The freelist.  The root page is a virtual node whose children are the freelist trunk pages.
- All other pages.  Pages not covered in the root node descriptions above are grouped together under this root node for review, the final implementation will properly categorize these pages.

Each page node it's child pages as child nodes.  If a page node points to overflow pages they are shown as children.

Page nodes are styled by page type and include a pop over with a text description of their type and basic details about the page.

The b-tree tree is on the left hand side and has a similar width to the 'Navigation Panel' in other views.  A key describing the page-type symbology is fixed at the bottom of the b-tree area and does not scroll; the tree itself fills the entire remaining vertical space above the key.

## Page Detail View

Shows the full contents of the page node selected in the b-tree tree.  A horizontal schematic view of the page that includes all header, cell, key and pointer information.  Pointers to other pages are clickable links.  The schematic approximately represents the byte size of each portion of the page, including free bytes.  Below the schematic view the full contents of the page is displayed, including contents of the headers, cell pointer array.  The data is shown in a tabular view below the schematic view that is color coded to the schematic view.  A key bar describes the color coding; it is fixed at the bottom of the page detail view and does not scroll with the schematic and table content above it.