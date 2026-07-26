#include "visualize/query_augment.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

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

// A minimal SQL token: an identifier/keyword `word` (any quoting removed), or a
// single structural `punct` character. String/blob literals, numbers and comments
// are dropped so their contents can't be mistaken for a clause keyword or table.
struct Tok {
    std::string word;  // non-empty for identifiers/keywords
    char punct = 0;    // '(' ')' ',' '.' '*' ';' … when `word` is empty
};

bool identStart(unsigned char c) { return std::isalpha(c) || c == '_'; }
bool identChar(unsigned char c) { return std::isalnum(c) || c == '_' || c == '$'; }

std::vector<Tok> tokenize(const std::string& sql) {
    std::vector<Tok> toks;
    const std::size_t n = sql.size();
    for (std::size_t i = 0; i < n;) {
        const unsigned char c = static_cast<unsigned char>(sql[i]);
        if (std::isspace(c)) { ++i; continue; }
        if (c == '-' && i + 1 < n && sql[i + 1] == '-') {            // line comment
            i += 2;
            while (i < n && sql[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && sql[i + 1] == '*') {            // block comment
            i += 2;
            while (i + 1 < n && !(sql[i] == '*' && sql[i + 1] == '/')) ++i;
            i = (i + 1 < n) ? i + 2 : n;
            continue;
        }
        if (c == '\'') {                                            // string literal — dropped
            i += 1;
            while (i < n) {
                if (sql[i] == '\'') {
                    if (i + 1 < n && sql[i + 1] == '\'') { i += 2; continue; }
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }
        if (c == '"' || c == '`' || c == '[') {                     // quoted identifier
            const char close = (c == '[') ? ']' : static_cast<char>(c);
            std::string val;
            i += 1;
            while (i < n) {
                if (sql[i] == close) {
                    if (close != ']' && i + 1 < n && sql[i + 1] == close) { val += close; i += 2; continue; }
                    ++i;
                    break;
                }
                val += sql[i++];
            }
            toks.push_back({val, 0});
            continue;
        }
        if (identStart(c)) {                                        // identifier / keyword
            std::size_t j = i + 1;
            while (j < n && identChar(static_cast<unsigned char>(sql[j]))) ++j;
            toks.push_back({sql.substr(i, j - i), 0});
            i = j;
            continue;
        }
        if (std::isdigit(c)) {                                      // number — dropped
            std::size_t j = i + 1;
            while (j < n && (identChar(static_cast<unsigned char>(sql[j])) || sql[j] == '.')) ++j;
            i = j;
            continue;
        }
        toks.push_back({std::string(), static_cast<char>(c)});      // structural punctuation
        ++i;
    }
    return toks;
}

// Words that end the FROM clause (at depth 0) or otherwise can't be a table alias.
bool isReserved(const std::string& lower) {
    static const std::unordered_set<std::string> kWords = {
        "where", "group", "having", "window", "order", "limit", "union", "intersect",
        "except", "join", "left", "right", "inner", "outer", "cross", "natural",
        "full", "on", "using", "as", "select", "returning",
    };
    return kWords.count(lower) != 0;
}
bool endsFromClause(const std::string& lower) {
    static const std::unordered_set<std::string> kEnd = {
        "where", "group", "having", "window", "order", "limit",
        "union", "intersect", "except", "returning",
    };
    return kEnd.count(lower) != 0;
}

// Index of the token just after the first top-level (depth-0) FROM, or toks.size().
std::size_t afterTopLevelFrom(const std::vector<Tok>& toks) {
    int depth = 0;
    for (std::size_t i = 0; i < toks.size(); ++i) {
        if (toks[i].punct == '(') ++depth;
        else if (toks[i].punct == ')') { if (depth) --depth; }
        else if (depth == 0 && !toks[i].word.empty() && toLower(toks[i].word) == "from") return i + 1;
    }
    return toks.size();
}

}  // namespace

std::vector<FromInstance> parseFromInstances(const std::string& sql) {
    const std::vector<Tok> toks = tokenize(sql);
    const std::size_t n = toks.size();
    std::vector<FromInstance> out;

    bool expectRef = true;
    for (std::size_t i = afterTopLevelFrom(toks); i < n; ++i) {
        const Tok& t = toks[i];
        if (t.punct == '(') {                       // subquery / expression — skip balanced
            int d = 1;
            for (++i; i < n && d; ++i) {
                if (toks[i].punct == '(') ++d;
                else if (toks[i].punct == ')') --d;
            }
            --i;
            if (expectRef) {                        // a subquery source: no base table
                FromInstance inst;
                if (i + 1 < n && !toks[i + 1].word.empty() &&
                    !isReserved(toLower(toks[i + 1].word))) {
                    inst.ref = toks[i + 1].word;    // its alias, if any
                    ++i;
                }
                out.push_back(inst);
                expectRef = false;
            }
            continue;
        }
        if (t.punct == ',') { expectRef = true; continue; }
        if (t.punct == ';' || t.punct == ')') break;
        if (t.word.empty()) continue;

        const std::string lw = toLower(t.word);
        if (endsFromClause(lw)) break;
        if (lw == "join") { expectRef = true; continue; }
        if (!expectRef) continue;                   // inside an ON/USING/join-type run

        // A table reference: optional `schema .` then the table name.
        std::string table = t.word;
        if (i + 2 < n && toks[i + 1].punct == '.' && !toks[i + 2].word.empty()) {
            table = toks[i + 2].word;
            i += 2;
        }
        // Optional alias: `AS x`, or a bare non-reserved identifier.
        std::string ref = table;
        if (i + 2 < n && !toks[i + 1].word.empty() && toLower(toks[i + 1].word) == "as" &&
            !toks[i + 2].word.empty()) {
            ref = toks[i + 2].word;
            i += 2;
        } else if (i + 1 < n && !toks[i + 1].word.empty() &&
                   !isReserved(toLower(toks[i + 1].word))) {
            ref = toks[i + 1].word;
            i += 1;
        }
        out.push_back({table, ref});
        expectRef = false;
    }
    return out;
}

std::vector<SelectItem> parseSelectItems(const std::string& sql) {
    const std::vector<Tok> toks = tokenize(sql);
    const std::size_t n = toks.size();
    std::vector<SelectItem> items;

    // The select list spans from just after a leading top-level SELECT (skipping an
    // optional ALL/DISTINCT) to the first top-level FROM.
    std::size_t i = 0;
    while (i < n && toks[i].word.empty()) ++i;
    if (i >= n || toLower(toks[i].word) != "select") return items;
    ++i;
    if (i < n && !toks[i].word.empty()) {
        const std::string lw = toLower(toks[i].word);
        if (lw == "all" || lw == "distinct") ++i;
    }
    const std::size_t fromIdx = afterTopLevelFrom(toks);  // token index of FROM + 1
    const std::size_t end = (fromIdx == 0 || fromIdx > n) ? n : fromIdx - 1;

    auto classify = [&](std::size_t lo, std::size_t hi) {
        SelectItem it;
        const std::size_t len = hi - lo;
        if (len == 1 && toks[lo].punct == '*') { it.kind = SelectItem::Star; }
        else if (len == 3 && !toks[lo].word.empty() && toks[lo + 1].punct == '.' &&
                 toks[lo + 2].punct == '*') {
            it.kind = SelectItem::TableStar;
            it.alias = toks[lo].word;
        } else {
            it.kind = SelectItem::Simple;
            if (len >= 3 && !toks[lo].word.empty() && toks[lo + 1].punct == '.' &&
                !toks[lo + 2].word.empty())
                it.alias = toks[lo].word;  // leading `alias.` of a column reference
        }
        items.push_back(it);
    };

    int depth = 0;
    std::size_t itemLo = i;
    for (std::size_t k = i; k < end; ++k) {
        if (toks[k].punct == '(') ++depth;
        else if (toks[k].punct == ')') { if (depth) --depth; }
        else if (depth == 0 && toks[k].punct == ',') {
            classify(itemLo, k);
            itemLo = k + 1;
        }
    }
    if (itemLo < end) classify(itemLo, end);
    return items;
}

Augmentation augmentWithRowids(const std::string& sql,
                               const std::vector<FromInstance>& instances) {
    Augmentation result;
    if (instances.empty()) return result;

    std::size_t p0 = 0;
    while (p0 < sql.size() && std::isspace(static_cast<unsigned char>(sql[p0]))) ++p0;

    const std::string lower = toLower(sql);
    // Must be a plain leading SELECT (not WITH/compound-leading, not a pragma).
    if (lower.compare(p0, 6, "select") != 0) return result;
    // Conservative: DISTINCT/GROUP BY change cardinality when a rowid is added.
    if (lower.find("distinct") != std::string::npos) return result;
    if (lower.find("group by") != std::string::npos) return result;

    std::string rowids;
    for (std::size_t i = 0; i < instances.size(); ++i) {
        if (i) rowids += ", ";
        rowids += quoteIdent(instances[i].ref) + ".rowid";
    }

    const std::size_t insertAt = p0 + 6;  // right after "select"
    result.sql = sql.substr(0, insertAt) + " " + rowids + "," + sql.substr(insertAt);
    result.instances = instances;
    result.ok = true;
    return result;
}
