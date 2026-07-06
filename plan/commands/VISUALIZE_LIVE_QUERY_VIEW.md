# Live Query Visualization

> **Status:** implemented. Enable with `sqlinsite visualize serve --map-file <map> --db-file <db>`.
> Front-end: `web/src/components/{SchemaPanel,QueryEditor,ResultsView,ResultsTable,QueryCanvas}.tsx`
> and `web/src/state/queryStore.ts`. Back-end: `src/visualize/query_engine.*` (runs, history,
> schema, row→page) and `src/visualize/query_augment.*`. Row→page mapping is best-effort and
> per-column (see ARCHITECTURE.md → "Live query view"): a cell shows its leaf page when the
> column resolves to a single source table, and is left blank otherwise.

The goal of this view is to let the user profile one or more queries interactively then review the profile results AND the data returned by the queries.

For this mode the navigation Panel is replaced with the 'schema panel'.  The schema panel is a tree showing the schemas in the SQLite file.  The view is is a new control called the 'query and data viewer' is split horizontally into two parts separated by a resizable divider.  On top, taking up 1/3rd of the view vertically is a text editor for entering a SQL query called the 'query editor'.  The bottom 2/3rds of the view is called 'results view' and can show one of four tabs, the default tab is a table control that shows results of the query called 'results table', the next two tabs are the pages and tables view that show the profile results of the last executed query or selection from the schema tree view.  The final tab is a hidden tab just to show the results from the 'Explain' button in the 'query control bar', it is called 'explain results'.

## Schema Panel

The schema panel is a tree derived from the `sqlite_schema` table the root nodes are 'Tables' and 'Views' their children are the tables and views respectively found in the sqlite_schema table.  The individual tables and views have children 'Columns', 'Indexes', and 'Triggers' and their children are the columns indexes and triggers respectively found via hte sqlite_schema table.

Each node in the tree has some information to the right of the name.  Number of pages, number of pages access by the selected profile run and if child nodes are queried from the sqlite_schema table, count of child nodes.  NOTE: The root 'Tables' node would show the count of children (number of tables) but each individual table node would not show a count because that would be just the fixed Columns, Indexes and Triggers nodes.

Each node table, view, column, index and trigger node has a 'Run' button (play symbol) that fills the 'results view' with the content related to that node.

## Results View

### Query Editor

The query editor has a 'query control bar' at the top with following controls: 'Run' button (play symbol), 'History' button, 'Format' button, and 'Explain' button in that order.  Below the query control bar is the query editor which is a text editor control with intellisense driven by SQLites 'sqlite_schema' table.  Uses the Monaco text editor: https://github.com/microsoft/monaco-editor/blob/main/README.md

### Query Control Bar

 - 'Run' Button - Runs the SQL query in the query editor using a fresh db connection on each run to ensure no prior pages are cached.  The query is run using the code behind the `sqlinsite profile` command.  The results returned by the query are loaded into the 'results view'.  The query and the profile results are stored in the history to be retrieved later.
 - 'History' button - shows a drop down with each previous query run and the number of pages loaded when running that query.  Selecting one of the previously run queries sets the query in the 'query editor', loads up the profile results and sets the 'results view' to show results as though they had just hit the Run button with the old query.
 - 'Format' button - Pretty formats the SQL query currently in the 'query editor'
 - 'Explain' button - Runs the SQL query currently in the 'query editor' twice, once prefixed with `EXPLAIN QUERY PLAN` and once prefixed with `EXPLAIN`.  The results are shown in 'explain results'.

 ### Results Table

 A virtualized table control showing the results for either a query executed from the query editor or the results of selecting a node in the schema panel.  The table should also show the pages containing the data from each row and column within that row.

