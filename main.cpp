#include "search.hpp"
#include <charconv>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

void benchmark();

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "bench") { benchmark(); return 0; }
        if (argc < 5 || std::string_view(argv[1]) != "search") {
            std::cerr << "Usage: postings search FILE K QUERY... | postings bench\n";
            return 2;
        }
        std::size_t k{};
        const std::string_view arg = argv[3];
        auto [p, err] = std::from_chars(arg.data(), arg.data() + arg.size(), k);
        if (p != arg.data() + arg.size() || err != std::errc{}) throw std::invalid_argument("K must be a non-negative integer");
        std::ifstream file(argv[2]);
        if (!file) throw std::runtime_error("cannot open corpus");
        std::vector<std::string> lines;
        for (std::string line; std::getline(file, line);) lines.push_back(line);
        if (file.bad()) throw std::runtime_error("cannot read corpus");
        std::string query = argv[4];
        for (int i = 5; i < argc; ++i) { query += ' '; query += argv[i]; }
        const search::Corpus corpus(lines);
        const search::Index index(corpus);
        auto hits = index.retrieve(query, k);
        for (auto h : hits) std::cout << h.doc + 1 << '\t' << std::fixed << std::setprecision(6)
                                      << h.score << '\t' << lines[h.doc] << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
