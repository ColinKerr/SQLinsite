#pragma once

#include <string>
#include <vector>

struct Augmentation {
    bool ok = false;                  // true when a safe augmentation was produced
    std::string sql;                  // augmented SQL (rowid columns prepended)
    std::vector<std::string> tables;  // source tables, in prepended-column order
};

// Builds a query that prepends one `"table".rowid` column per distinct source
// table so the live-query view can map result rows back to pages. Refuses
// (ok=false) unless the query is a plain leading SELECT with at least one source
// table and without DISTINCT / GROUP BY (where injecting rowid would change the
// result set). Callers must still verify the augmented rows match the original
// (prefer leaving mapping unresolved over guessing wrong).
Augmentation augmentWithRowids(const std::string& sql,
                               const std::vector<std::string>& sourceTables);
