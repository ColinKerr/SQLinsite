#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

// Decodes a single database page's full contents for the Page Tree view's detail
// pane: a byte-proportional region layout (for the schematic), the decoded cells
// with real column values, and the page's outgoing pointers (for links).
//
// Like CellPageMap it retains its own read-only connection and reads individual
// pages on demand through the `sqlite_dbpage` vtab (outside the profiling VFS), so
// it never loads the whole file and never pollutes a query profile. The page type
// comes from the map (the raw bytes alone can't distinguish overflow/freelist/etc).
// Not thread-safe; the server serializes access behind its request handling.
class PageContent {
public:
    // Opens `dbPath` read-only. Throws std::runtime_error on failure.
    static PageContent open(const std::string& dbPath);

    ~PageContent();
    PageContent(PageContent&&) noexcept;
    PageContent& operator=(PageContent&&) noexcept;
    PageContent(const PageContent&) = delete;
    PageContent& operator=(const PageContent&) = delete;

    // JSON detail of `pageNumber` given its `pageType` (from the map). `pageTypeOf`
    // resolves a pointer target's page type (from the map) so pointers can be shown
    // with type symbology. Empty string if the page cannot be read.
    std::string pageJson(std::int64_t pageNumber, const std::string& pageType,
                         const std::function<std::string(std::int64_t)>& pageTypeOf) const;

private:
    PageContent() = default;
    std::vector<std::uint8_t> readPage(std::int64_t pageNumber) const;
    void close() noexcept;

    sqlite3* db_ = nullptr;
    sqlite3_stmt* pageStmt_ = nullptr;
    int pageSize_ = 0;
    int usableSize_ = 0;
};
