# Page Tree View

The goal of this view is to visualize and inspect the b-tree structures in a SQLite file and drill down into the individual pages.  It must scale to dbs with billions of pages.

This view uses the description of the file format found here: https://sqlite.org/fileformat.html and a similar strategy is used as the `sqlinsite map` command describe in MAP.md Approach section.

The view is split into two resizable areas vertically.  On the left hand side is the 'b-tree tree' which is a virtualized tree view containing all pages.  On the right hand side is the content area showing either the appropriate 'page detail view' overview view depending on the node selected.

## B-Tree Tree

The B-Tree Tree is defined in the B_TREE_TREE.md file.

### Linking Behavior

The Page Tree view **registers** its node-activation handler with the shared tree
(see B_TREE_TREE.md). When a node is activated the handler fills the content area
on the right:

- Table Node
  - Activating a table node shows the TableOverview control for this table in the content area
- Index Grouping Node
  - Activating an index grouping node shows the IndexOverview control for this table in the content area
- Page Node
  - Activating a page node shows the PageDetail control of the appropriate type for the selected page in the content area

## Table Overview view

Shows details about the table and it's indexes when the root node for a table is selected in the b-tree tree.

### Content

- Header
  - Table name
- Table details
  - Root page show using a PageCard
  - Number of rows in the table
  - Number of pages in the table
  - Size in bytes of the table based on number of pages * page size.
- Table SQL
  - Monospaced code text box with CREATE TABLE statement
- Indexes
  - A table view of the indexes for the sql table.
  - Columns
    - Name - Name of the index
    - Pages - Number of pages used to store the index
    - Size - Size in bytes of the index based on number of pages * page size.
    - Root - Root page for index shown as a PageCard

## Index Overview view

Shows details about the indexes for a table when the index grouping node is selected in the b-tree tree.

### Content

Reuses the 'Indexes' section from the Table Overview view but adds a new column 'Statement' which shows the SQL used to create the index.

## Page Detail View

Shows the full contents of the page node selected in the b-tree tree.  

If pointer to another page exists in the header or cell records it is shown as a clickable control matching the node in the b-tree tree.  Clicking on the control navigates to the Page Detail View for that page and expands and selects the node in the b-tree tree.

### Header

The header includes the following with each line below a line in the header

- Table or index b-tree this page is part of
- Page Number, page type description
- Row Count (if this is a table-internal or table-leaf page)
- The decoded contents of the header

### Horizontal Schematic View

A fixed section of the Page Detail View including the page number, header info and schematic view.

The horizontal schematic view of the page that includes all header, cell, key and pointer information.  The schematic approximately represents the byte size of each portion of the page, including free bytes.  Clicking on the sections of the schematic scrolls to the detailed data shown in the Full Page Contents area.

### Full Page Contents

An area below the schematic view where the full contents of the page is displayed, including contents of the headers, cell pointer array and cells.  The data is shown in a tabular view below the schematic view that is color coded to the schematic view.  Each type of page has a different page content control.

#### Table Leaf Pages

Shows the pages cell records and the values stored in that cell.

Hovering over a cell in the detail view selects the corresponding cell in the Schematic View and vice versa.  Clicking a cell in the Schematic View scrolls and highlights the corresponding cell in the TableLeafPage control.

##### Values from overflow pages

When a cell/record overflows into overflow pages show the data from the overflow pages in the full page contents for the leaf page.  

Depending on the type and how data is split it should be handled in one of the following ways
- If an entire value comes from an overflow page the value is written followed by '-> overflow [clickable page control]'.  The value and the clickable page control should be colored the same.
- If a value spans multiple pages it is color coded to match the following rules
  - The portion from the leaf page is white
  - If the value is variable length the type area should include the total number of bytes then a colored segment with the number of bytes from each overflow page followed by a clickable page control for each overflow.  
    - If the type is a string, the portion of the value from each overflow page shares the color giving to the clickable page control for that page.
    - If the type is a BLOB the number of bytes in each page are shown as such: `BLOB (42 bytes, 3000 bytes -> p32, 2700 bytes -> p33)` where 42 bytes are in the leaf page and 3000 bytes are in overflow page 32 and 2700 bytes are in overflow page 33.

#### Table Interior Pages

Table interior pages have a different layout than Table leaf pages because their cells hold no data and only point to child table interior or table leaf cells.  Cell records are displayed in a table control called the 'Table Interior Cell control' with the following headings:

- 'Cell (record)' - The Cell number.
- 'Row Count' - The number of rows included in the range shown in 'Row Ids'
- 'Bytes' - The start and stop bytes plus the total number of bytes in the format `start-stop (total B)` e.g. `42-53 (11B)`
- 'Page' - The clickable page control for the page pointed to by the cell
- 'Row Ids' - The range of row Ids of the Page (left child).  Includes the row ids from the page referenced by the Left Child's right most pointer.
  - If only one row id is referenced so as a single id rather than a range

Hovering over a row in the table highlights the corresponding cell in the Schematic View and vice versa.  Clicking a cell in the Schematic View scrolls and highlights the corresponding row in the Table Interior cell control.

#### Overflow Pages

If an overflow page is selected in the b-tree tree the leaf or internal page that owns the overflow page should be used to interpret the data in the overflow page to show the Full Page Contents of the overflow page. The owning page is shown as a clickable page control ("owned by [p_owner]"). The Full Page Contents shows only the data that physically lives on this overflow page: the owning cell restricted to the column value slices carried by this page (a column with no bytes on this page is omitted, and a column that overflows onto this page shows only its slice — its byte count for a BLOB, its text fragment for a string).



### Page View Detail Key

A key bar describes the color coding; it is fixed at the bottom of the page detail view and does not scroll with the schematic and table content above it.