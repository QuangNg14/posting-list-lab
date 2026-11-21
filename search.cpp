#include "search.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace search {
namespace {

std::vector<std::string> tokens(std::string_view text) {
    std::vector<std::string> result;
    std::string token;
    for (char c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) token += c;
        else if (!token.empty()) { result.push_back(token); token.clear(); }
    }
    if (!token.empty()) result.push_back(token);
    return result;
}

std::vector<Entry> frequencies(std::vector<Id>& ids) {
    std::sort(ids.begin(), ids.end());
    std::vector<Entry> result;
    for (Id id : ids) {
        if (result.empty() || id != result.back().id) result.push_back({id, 1});
        else ++result.back().count;
    }
    return result;
}

double contribution(const Term& t, const Entry& e, double norm) {
    return t.weight * (e.count * 2.2 / (e.count + norm));
}

Hits top(Hits hits, std::size_t k) {
    const auto count = std::min(k, hits.size());
    std::partial_sort(hits.begin(), hits.begin() + count, hits.end(), [](Hit a, Hit b) {
        return a.score != b.score ? a.score > b.score : a.doc < b.doc;
    });
    hits.resize(count);
    return hits;
}

Id checked_id(std::size_t value) {
    if (value >= std::numeric_limits<Id>::max()) throw std::length_error("corpus exceeds 32-bit index");
    return static_cast<Id>(value);
}
}

Corpus::Corpus(const std::vector<std::string>& lines) {
    checked_id(lines.size());
    std::vector<std::size_t> df;
    double total_length = 0;
    for (const auto& line : lines) {
        std::vector<Id> ids;
        for (const auto& token : tokens(line)) {
            auto [it, fresh] = ids_.try_emplace(token, checked_id(ids_.size()));
            if (fresh) df.push_back(0);
            ids.push_back(it->second);
        }
        checked_id(ids.size());
        auto terms = frequencies(ids);
        for (auto e : terms) ++df[e.id];
        const auto length = static_cast<double>(ids.size());
        docs_.push_back({std::move(terms), length});
        total_length += length;
    }
    const double average = total_length == 0 ? 1.0 : total_length / docs_.size();
    for (auto& doc : docs_) doc.norm = 1.2 * (0.25 + 0.75 * doc.norm / average);
    for (auto n : df) idf_.push_back(std::log1p((docs_.size() - n + 0.5) / (n + 0.5)));
}

std::vector<Term> Corpus::analyze(std::string_view query) const {
    std::vector<Id> ids;
    for (const auto& token : tokens(query)) {
        if (auto it = ids_.find(token); it != ids_.end()) ids.push_back(it->second);
    }
    checked_id(ids.size());
    std::vector<Term> terms;
    for (auto e : frequencies(ids)) terms.push_back({e.id, e.count * idf_[e.id]});
    return terms;
}

Hits Corpus::scan(std::string_view query, std::size_t k) const {
    const auto terms = analyze(query);
    Hits hits;
    if (terms.empty() || k == 0) return hits;
    for (Id id = 0; id < docs_.size(); ++id) {
        const auto& doc = docs_[id];
        double score = 0;
        for (const auto& t : terms) {
            const auto pos = std::lower_bound(doc.terms.begin(), doc.terms.end(), t.id,
                                              [](Entry e, Id term) { return e.id < term; });
            if (pos != doc.terms.end() && pos->id == t.id) score += contribution(t, *pos, doc.norm);
        }
        if (score > 0) hits.push_back({id, score});
    }
    return top(std::move(hits), k);
}

Index::Index(const Corpus& corpus) : corpus_(corpus), lists_(corpus.vocabulary_size()) {
    for (Id id = 0; id < corpus.documents().size(); ++id) {
        for (auto e : corpus.documents()[id].terms) lists_[e.id].push_back({id, e.count});
    }
}

Hits Index::retrieve(std::string_view query, std::size_t k) const {
    const auto terms = corpus_.analyze(query);
    if (terms.empty() || k == 0) return {};
    std::unordered_map<Id, double> scores;
    for (const auto& t : terms) {
        for (const auto& e : lists_[t.id]) {
            scores[e.id] += contribution(t, e, corpus_.documents()[e.id].norm);
        }
    }
    Hits hits;
    hits.reserve(scores.size());
    for (auto [id, score] : scores) hits.push_back({id, score});
    return top(std::move(hits), k);
}

std::size_t Index::posting_count() const {
    std::size_t n = 0;
    for (const auto& list : lists_) n += list.size();
    return n;
}

std::size_t Index::reserved_bytes() const {
    std::size_t n = lists_.capacity() * sizeof(std::vector<Entry>);
    for (const auto& list : lists_) n += list.capacity() * sizeof(Entry);
    return n;
}
}
