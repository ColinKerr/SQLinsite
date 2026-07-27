#pragma once

#include <string>
#include <vector>

// One entry of a query's FROM clause. A self-join yields two instances with the
// same `table` but different `ref`. Subquery / table-valued sources get an empty
// `table` (no base rows to map).
struct FromInstance {
    std::string table;  // base table name, or "" for a non-table source
    std::string ref;    // how the query references it: its alias, else the table name
};

// A classified top-level SELECT-list item (best-effort; only what row→page mapping
// needs to attribute output columns to FROM instances).
struct SelectItem {
    enum Kind { Simple, Star, TableStar } kind = Simple;
    std::string alias;  // TableStar: alias before ".*"; Simple: leading "alias." if any
};

struct Augmentation {
    bool ok = false;                       // true when a safe augmentation was produced
    std::string sql;                       // augmented SQL (rowid columns prepended)
    std::vector<FromInstance> instances;   // one per prepended rowid column, in order
};

// The FROM clause's table references in order (a self-join appears twice). Best
// effort; robust to JOIN/ON/USING, commas, aliases (`t x` / `t AS x`),
// schema-qualified names, quoting, and subqueries (emitted with an empty table).
std::vector<FromInstance> parseFromInstances(const std::string& sql);

// The comma-separated items of the top-level SELECT list (before FROM). Best effort.
std::vector<SelectItem> parseSelectItems(const std::string& sql);

// Builds a query that prepends one `<ref>.rowid` column per instance (in order) so
// the live-query view can map result rows back to pages — using each instance's
// FROM-clause reference (its alias when aliased) so the rowid resolves. Refuses
// (ok=false) unless the query is a plain leading SELECT with at least one instance
// and without DISTINCT / GROUP BY (where injecting rowid would change the result
// set). Callers must still verify the augmented rows match the original.
Augmentation augmentWithRowids(const std::string& sql,
                               const std::vector<FromInstance>& instances);
