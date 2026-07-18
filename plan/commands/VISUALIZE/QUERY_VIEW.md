# Query Visualization

The goal of this view is to let the user profile a query interactively then review the profile results AND the data returned by the queries.

For this mode the Navigation Panel is the shared **B-Tree Tree** on the left (see B_TREE_TREE.md), the same control and state as every other view.  The main content to its right is a new control called the 'query and data viewer' which is split horizontally into two parts separated by a resizable divider.  On top, taking up 1/3rd of the view vertically is a text editor for entering a SQL query called the 'query editor'.  The bottom 2/3rds of the view is called 'results view' and can show one of four tabs, the default tab is a table control that shows results of the query called 'results table', the next two tabs are the pages and tables view that show the profile results of the last executed query or selection in the tree.  The final tab is a hidden tab just to show the results from the 'Explain' button in the 'query control bar', it is called 'explain results'.

## Navigation (B-Tree Tree)

The Query view navigates with the shared B-Tree Tree (see B_TREE_TREE.md); it
replaces the earlier bespoke schema panel.  Its nodes are the file's b-trees — the
tables and their indexes, plus pages — so navigation is at object/page
granularity rather than the schema's columns/triggers granularity.

The Query view **registers** its node-activation handler with the shared tree. The
handler runs the query for the activated node and fills the 'results view' with its
data and profile, exactly as the query control bar's 'Run' does:

- Table Grouping node - Runs SQL that selects that objects data.
- Table interior node - Runs SQL that selects the rows contained by that page and it's child pages.
- Table leaf node - Runs SQL that selects the rows contained by that page and it's overflow pages.
- Table leaf overflow node - Runs SQL that selects the rows contained by the owning leaf node and highlights the portion of the data coming from the overflow page.
- All Index nodes - Do nothing.

Run the SQL by setting the query editor text box to the appropriate SQL then running the query as though the 'Run' button had been pressed

Only run the query if the current contents of the query editor text box is different than the query for the selected node.

Arbitrary SQL — including against views, which are not b-trees and so do not
appear in the tree — is still run from the query editor.  The editor's
autocomplete is removed so the schema API can also be removed (`/api/schema`)

The tree does not show any per-node profile results (e.g. accessed page counts)
for now; nodes show only their static page count and size, the same as in every
other view.

## Results View

### Query Editor

The query editor has a 'query control bar' at the top with following controls: 'Run' button (play symbol), 'History' button, 'Format' button, and 'Explain' button in that order.  Below the query control bar is the query editor which is a Monaco text editor control (https://github.com/microsoft/monaco-editor/blob/main/README.md).  Autocomplete/intellisense is removed for now.

### Query Control Bar

 - 'Run' Button - Runs the SQL query in the query editor using a fresh db connection on each run to ensure no prior pages are cached.  The query is run using the code behind the `sqlinsite profile` command.  The results returned by the query are loaded into the 'results view'.  The query and the profile results are stored in the history to be retrieved later.
 - 'History' button - shows a drop down with each previous query run and the number of pages loaded when running that query.  Selecting one of the previously run queries sets the query in the 'query editor', loads up the profile results and sets the 'results view' to show results as though they had just hit the Run button with the old query.
 - 'Format' button - Pretty formats the SQL query currently in the 'query editor'
 - 'Explain' button - Runs the SQL query currently in the 'query editor' twice, once prefixed with `EXPLAIN QUERY PLAN` and once prefixed with `EXPLAIN`.  The results are shown in 'explain results'.

 ### Results Table

 A virtualized table control showing the results for either a query executed from the query editor or the results of selecting a node in the tree.  The table should also show the pages containing the data from each row and column within that row.

