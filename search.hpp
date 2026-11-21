#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace search {

using Id = std::uint32_t;
struct Entry { Id id; std::uint32_t count; };
struct Document { std::vector<Entry> terms; double norm; };
struct Term { Id id; double weight; };
struct Hit { Id doc; double score; };
inline bool operator==(const Hit& a, const Hit& b) { return a.doc == b.doc && a.score == b.score; }
using Hits = std::vector<Hit>;

// Both paths reuse token IDs, document frequencies and length normalization.
// The reference has no term-to-document lookup.
class Corpus {
public:
    explicit Corpus(const std::vector<std::string>& lines);
    std::vector<Term> analyze(std::string_view query) const;
    const std::vector<Document>& documents() const { return docs_; }
    std::size_t vocabulary_size() const { return idf_.size(); }
    Hits scan(std::string_view query, std::size_t k) const;
private:
    std::vector<Document> docs_;
    std::unordered_map<std::string, Id> ids_;
    std::vector<double> idf_;
};

class Index {
public:
    explicit Index(const Corpus& corpus);
    Index(Corpus&&) = delete;
    Hits retrieve(std::string_view query, std::size_t k) const;
    std::size_t posting_count() const;
    std::size_t reserved_bytes() const;
private:
    const Corpus& corpus_;
    std::vector<std::vector<Entry>> lists_;
};

}
