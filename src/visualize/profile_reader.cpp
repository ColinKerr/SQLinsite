#include "visualize/profile_reader.hpp"

#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

namespace {

// Tokenizes RFC-4180-style CSV text into rows of fields, honoring quoted
// fields (which may contain commas, quotes, and newlines).
std::vector<std::vector<std::string>> tokenizeCsv(const std::string& text) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool inQuotes = false;
    bool rowStarted = false;

    auto endField = [&] { row.push_back(field); field.clear(); };
    auto endRow = [&] {
        endField();
        rows.push_back(row);
        row.clear();
        rowStarted = false;
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        rowStarted = true;
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += c;
            }
        } else if (c == '"') {
            inQuotes = true;
        } else if (c == ',') {
            endField();
        } else if (c == '\n') {
            endRow();
        } else if (c != '\r') {
            field += c;
        }
    }
    if (rowStarted || !field.empty() || !row.empty()) {
        endRow();
    }
    return rows;
}

}  // namespace

ProfileAggregate aggregateProfileCsv(const std::string& csvText) {
    const auto rows = tokenizeCsv(csvText);
    std::map<std::int64_t, PageAccess> byPage;

    // Leaf = (sessionName, statementIndex). Ids are assigned in first-seen order.
    using LeafKey = std::pair<std::string, std::int64_t>;
    std::map<LeafKey, int> leafIds;
    std::map<std::pair<int, std::int64_t>, LeafPageAccess> byLeafPage;

    ProfileAggregate agg;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        if (r == 0) continue;  // header
        const auto& fields = rows[r];
        if (fields.size() < 6) continue;

        std::int64_t statementIndex = 0;
        std::int64_t pageNumber = 0;
        try {
            statementIndex = std::stoll(fields[1]);
            pageNumber = std::stoll(fields[4]);
        } catch (...) {
            continue;
        }
        if (pageNumber <= 0) continue;

        const LeafKey key{fields[0], statementIndex};
        auto [it, inserted] = leafIds.try_emplace(key, static_cast<int>(agg.leaves.size()));
        if (inserted) {
            agg.leaves.push_back({it->second, fields[0], statementIndex});
        }
        const int leafId = it->second;

        PageAccess& a = byPage[pageNumber];
        a.pageNumber = pageNumber;
        LeafPageAccess& lp = byLeafPage[{leafId, pageNumber}];
        lp.leafId = leafId;
        lp.pageNumber = pageNumber;
        if (fields[5] == "Read") {
            ++a.reads;
            ++lp.reads;
            ++agg.totalReads;
        } else if (fields[5] == "Write") {
            ++a.writes;
            ++lp.writes;
            ++agg.totalWrites;
        }
    }

    for (const auto& [num, access] : byPage) {
        agg.pages.push_back(access);
    }
    for (const auto& [key, lp] : byLeafPage) {
        agg.leafPages.push_back(lp);
    }
    return agg;
}

ProfileAggregate aggregateProfileFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("visualize: cannot read profile file: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return aggregateProfileCsv(ss.str());
}

std::string profileJson(const ProfileAggregate& agg) {
    nlohmann::json pages = nlohmann::json::array();
    for (const PageAccess& a : agg.pages) {
        pages.push_back({{"pageNumber", a.pageNumber},
                         {"reads", a.reads},
                         {"writes", a.writes}});
    }
    nlohmann::json j = {
        {"pages", std::move(pages)},
        {"totals", {{"reads", agg.totalReads}, {"writes", agg.totalWrites}}},
    };
    return j.dump();
}
