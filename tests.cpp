#include "search.hpp"
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

namespace {
void require(bool value, const std::string& label) {
    if (!value) throw std::logic_error(label);
}

search::Hits exact(const search::Corpus& c, const search::Index& index, const std::string& q, std::size_t k) {
    auto reference = c.scan(q, k);
    require(reference == index.retrieve(q, k), "score or order mismatch: " + q);
    return reference;
}

void ids(const search::Hits& hits, const std::vector<search::Id>& wanted) {
    require(hits.size() == wanted.size(), "wrong number of hits");
    for (std::size_t i = 0; i < wanted.size(); ++i) require(hits[i].doc == wanted[i], "incorrect rank");
}
}

int main() {
    try {
        const search::Corpus c({"stock amber amber", "stock blue blue", "stock blue blue", "stock rare", ""});
        const search::Index index(c);
        ids(exact(c, index, "RARE.", 10), {3});
        ids(exact(c, index, "stock", 10), {3, 0, 1, 2});
        ids(exact(c, index, "blue", 1), {1});
        ids(exact(c, index, "absent", 10), {});
        ids(exact(c, index, "!", 10), {});
        ids(exact(c, index, "stock", 0), {});
        auto once = exact(c, index, "blue", 3);
        auto twice = exact(c, index, "BLUE,blue absent", 3);
        ids(twice, {1, 2});
        require(twice.front().score == 2 * once.front().score, "repeated terms must count");
        // Five documents, two containing blue, length three vs. mean length 2.2.
        const double known = std::log(1.0 + 3.5 / 2.5) * 4.4 / (2.0 + 1.2 * (0.25 + 0.75 * 3.0 / 2.2));
        require(std::abs(once.front().score - known) < 1e-12, "BM25 example disagrees");
        const search::Corpus empty({});
        ids(exact(empty, search::Index(empty), "rare", 1), {});
        const search::Corpus blanks({"", "!"});
        ids(exact(blanks, search::Index(blanks), "x", 5), {});
        std::mt19937 rng(7301);
        std::vector<std::string> lines;
        for (int d = 0; d < 97; ++d) {
            std::string line;
            for (int j = 0; j < d % 13; ++j) line += " t" + std::to_string(rng() % 23);
            lines.push_back(std::move(line));
        }
        const search::Corpus random(lines);
        const search::Index inv(random);
        for (int i = 0; i < 200; ++i) {
            auto q = "t" + std::to_string(rng() % 27) + " t" + std::to_string(rng() % 27);
            exact(random, inv, q, rng() % 15);
        }
        std::cout << "PASS: rare/common, OOV, repeated terms, ties, k limits, empty corpora, BM25 value, 200 seeded top-k comparisons\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
