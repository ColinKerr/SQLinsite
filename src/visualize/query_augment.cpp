#include "visualize/query_augment.hpp"

#include <algorithm>
#include <cctype>

namespace {

std::string toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Double-quotes an identifier, escaping embedded quotes.
std::string quoteIdent(const std::string& name) {
    std::string out = "\"";
    for (char c : name) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
    return out;
}

}  // namespace

Augmentation augmentWithRowids(const std::string& sql,
                               const std::vector<std::string>& sourceTables) {
    Augmentation result;
    if (sourceTables.empty()) return result;

    std::size_t p0 = 0;
    while (p0 < sql.size() && std::isspace(static_cast<unsigned char>(sql[p0]))) ++p0;

    const std::string lower = toLower(sql);
    // Must be a plain leading SELECT (not WITH/compound-leading, not a pragma).
    if (lower.compare(p0, 6, "select") != 0) return result;
    // Conservative: DISTINCT/GROUP BY change cardinality when a rowid is added.
    if (lower.find("distinct") != std::string::npos) return result;
    if (lower.find("group by") != std::string::npos) return result;

    std::string rowids;
    for (std::size_t i = 0; i < sourceTables.size(); ++i) {
        if (i) rowids += ", ";
        rowids += quoteIdent(sourceTables[i]) + ".rowid";
    }

    const std::size_t insertAt = p0 + 6;  // right after "select"
    result.sql = sql.substr(0, insertAt) + " " + rowids + "," + sql.substr(insertAt);
    result.tables = sourceTables;
    result.ok = true;
    return result;
}
