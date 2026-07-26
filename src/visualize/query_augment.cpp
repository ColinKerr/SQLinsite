#include "visualize/query_augment.hpp"

#include <algorithm>
#include <cctype>
#include <map>
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

// A minimal SQL token: an identifier/keyword `word` (with any quoting removed), or
// a single structural `punct` character. String/blob literals, numbers and comments
// are dropped so their contents can't be mistaken for a clause keyword or table.
struct Tok {
    std::string word;  // non-empty for identifiers/keywords
    char punct = 0;    // '(' ')' ',' '.' ';' etc. when `word` is empty
};

bool identStart(unsigned char c) { return std::isalpha(c) || c == '_'; }
bool identChar(unsigned char c) { return std::isalnum(c) || c == '_' || c == '$'; }

std::vector<Tok> tokenize(const std::string& sql) {
    std::vector<Tok> toks;
    const std::size_t n = sql.size();
    for (std::size_t i = 0; i < n;) {
        const unsigned char c = static_cast<unsigned char>(sql[i]);
        if (std::isspace(c)) { ++i; continue; }
        // Comments.
        if (c == '-' && i + 1 < n && sql[i + 1] == '-') {
            i += 2;
            while (i < n && sql[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && sql[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(sql[i] == '*' && sql[i + 1] == '/')) ++i;
            i = (i + 1 < n) ? i + 2 : n;
            continue;
        }
        // String / blob literals — dropped.
        if (c == '\'') {
            i += 1;
            while (i < n) {
                if (sql[i] == '\'') {
                    if (i + 1 < n && sql[i + 1] == '\'') { i += 2; continue; }  // '' escape
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }
        // Quoted identifiers → a word with the quoting removed.
        if (c == '"' || c == '`' || c == '[') {
            const char close = (c == '[') ? ']' : static_cast<char>(c);
            std::string val;
            i += 1;
            while (i < n) {
                if (sql[i] == close) {
                    if (close != ']' && i + 1 < n && sql[i + 1] == close) {  // "" / `` escape
                        val += close; i += 2; continue;
                    }
                    ++i;
                    break;
                }
                val += sql[i++];
            }
            toks.push_back({val, 0});
            continue;
        }
        // Bare identifier / keyword.
        if (identStart(c)) {
            std::size_t j = i + 1;
            while (j < n && identChar(static_cast<unsigned char>(sql[j]))) ++j;
            toks.push_back({sql.substr(i, j - i), 0});
            i = j;
            continue;
        }
        // Numbers — dropped (can't be a table/alias; keep structure simple).
        if (std::isdigit(c)) {
            std::size_t j = i + 1;
            while (j < n && (identChar(static_cast<unsigned char>(sql[j])) || sql[j] == '.')) ++j;
            i = j;
            continue;
        }
        // Any other character is structural punctuation.
        toks.push_back({std::string(), static_cast<char>(c)});
        ++i;
    }
    return toks;
}

// Words that end the FROM clause (at depth 0) or otherwise can't be a table alias.
const std::unordered_set<std::string>& reservedWords() {
    static const std::unordered_set<std::string> kWords = {
        "where", "group", "having", "window", "order", "limit", "union", "intersect",
        "except", "join", "left", "right", "inner", "outer", "cross", "natural",
        "full", "on", "using", "as", "select", "returning",
    };
    return kWords;
}
bool endsFromClause(const std::string& lower) {
    static const std::unordered_set<std::string> kEnd = {
        "where", "group", "having", "window", "order", "limit",
        "union", "intersect", "except", "returning",
    };
    return kEnd.count(lower) != 0;
}

// For each source table (matched case-insensitively) the identifier that must be
// used to reference it in the query — its FROM-clause alias when aliased, else the
// table name. A table seen with two different references (self-join) maps to ""
// (ambiguous). Best-effort: anything not resolved is simply absent.
std::map<std::string, std::string> resolveTableRefs(
    const std::string& sql, const std::vector<std::string>& sourceTables) {
    std::unordered_set<std::string> wanted;
    for (const std::string& t : sourceTables) wanted.insert(toLower(t));

    const std::vector<Tok> toks = tokenize(sql);
    const std::size_t n = toks.size();

    // Locate the first top-level FROM (depth 0).
    std::size_t i = 0;
    int depth = 0;
    for (; i < n; ++i) {
        if (toks[i].punct == '(') ++depth;
        else if (toks[i].punct == ')') { if (depth) --depth; }
        else if (depth == 0 && !toks[i].word.empty() && toLower(toks[i].word) == "from") { ++i; break; }
    }

    std::map<std::string, std::string> refs;
    bool expectRef = true;
    for (; i < n; ++i) {
        const Tok& t = toks[i];
        if (t.punct == '(') {                 // subquery / expression — skip balanced
            int d = 1;
            for (++i; i < n && d; ++i) {
                if (toks[i].punct == '(') ++d;
                else if (toks[i].punct == ')') --d;
            }
            --i;               // loop's ++i lands after the ')'
            expectRef = false;  // a subquery source has no base table for us
            continue;
        }
        if (t.punct == ',') { expectRef = true; continue; }
        if (t.punct == ';' || t.punct == ')') break;
        if (t.word.empty()) continue;         // other punctuation

        const std::string lw = toLower(t.word);
        if (depth == 0 && endsFromClause(lw)) break;
        if (lw == "join") { expectRef = true; continue; }
        if (!expectRef) continue;             // inside an ON/USING/join-type run

        // A table reference starts here: an optional `schema .` then the table name.
        std::string table = t.word;
        if (i + 2 < n && toks[i + 1].punct == '.' && !toks[i + 2].word.empty()) {
            table = toks[i + 2].word;
            i += 2;
        }
        // Optional alias: `AS x`, or a bare non-reserved identifier.
        std::string ref = table;
        if (i + 1 < n && !toks[i + 1].word.empty() && toLower(toks[i + 1].word) == "as" &&
            i + 2 < n && !toks[i + 2].word.empty()) {
            ref = toks[i + 2].word;
            i += 2;
        } else if (i + 1 < n && !toks[i + 1].word.empty() &&
                   reservedWords().count(toLower(toks[i + 1].word)) == 0) {
            ref = toks[i + 1].word;
            i += 1;
        }
        expectRef = false;

        const std::string key = toLower(table);
        if (wanted.count(key)) {
            auto it = refs.find(key);
            if (it == refs.end()) refs.emplace(key, ref);
            else if (it->second != ref) it->second.clear();  // ambiguous self-join
        }
    }
    return refs;
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

    // Reference each source table the way the query does — by its alias when the
    // FROM clause aliases it (otherwise `<name>.rowid` fails to resolve).
    const std::map<std::string, std::string> refs = resolveTableRefs(sql, sourceTables);

    std::string rowids;
    for (std::size_t i = 0; i < sourceTables.size(); ++i) {
        if (i) rowids += ", ";
        auto it = refs.find(toLower(sourceTables[i]));
        const std::string& ref =
            (it != refs.end() && !it->second.empty()) ? it->second : sourceTables[i];
        rowids += quoteIdent(ref) + ".rowid";
    }

    const std::size_t insertAt = p0 + 6;  // right after "select"
    result.sql = sql.substr(0, insertAt) + " " + rowids + "," + sql.substr(insertAt);
    result.tables = sourceTables;
    result.ok = true;
    return result;
}
