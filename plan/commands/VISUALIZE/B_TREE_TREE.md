# B-Tree Tree

## Root nodes

- 'sqlite_schema' root node.  Page 1 for the sqlite file serves as the root node.
- Each table in the file is a root node, this node is called a 'Table Node' and contains the table and index b-trees for the table.  Node is named the name of the table.  
  - Child nodes: 
    - The table b-tree for this table.  The root page for that table serves as the first node.  Node is named the name of the table plus '(table)'
    - Indexes grouping node, only shown if the table has indexes.  It's children are the index b-trees, the root page for each index serves as the indexes first row.  Node is named the name of the index plus '(index)'
- The freelist.  The root page is a virtual node whose children are the freelist trunk pages.
- Lock-Byte root node.  Only shown if the Lock-Byte page exists.  The Lock-Byte page serves as the root node.
- All other pages.  Only shown if pages exist that are not covered in the root node descriptions above are grouped together under this root node for review, the final implementation will properly categorize these pages.

## Child nodes and expansion

Each page node it's child pages as child nodes.  If a page node points to overflow pages they are shown as children.

Nodes with children have arrows indicating that they can be expanded.  Clicking on the arrow expands/collapses the nodes children, clicking anywhere else on the node selects the node.  The arrow points to the right when collapsed and down when expanded.

## Tree Key

A key describing the page-type symbology.  Fixed at the bottom of the b-tree area and does not scroll.  Entries in the key are arranged in two or three columns depending on the width of the b-tree tree.

## Node Search Bar

A search box that finds page by page number.  Fixed at the top of the b-tree area and does not scroll.  A drop down shows matches to page number entered in the search box. Selecting a node from the drop down using arrow keys + enter or clicking with the mouse jumps to that node in the b-tree tree and closes the drop down.  The highest match is selected by default so return or enter jumps to that node.

The nodes shown in the drop down should reuse the same node control used in the b-tree tree.

## Styling

### Node Styling

All nodes, including grouping nodes, have an icon followed by the node name followed by the size in pages and bytes of the node and it's children.  Page count and size in bytes have muted color.  

Page nodes are styled by page type with type included in the tree key.

### Formatting for Sizes in bytes

Size in bytes is formatted in kilobytes (KB), megabytes (MB) or gigabytes (GB) ensuring value is never more than 4 digits including decimal digits.

### Page Layout

The tree key is fixed at the bottom and the tree itself fills the entire remaining vertical space above the key.