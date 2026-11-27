#include "../search.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: bm25 CORPUS_LINES\n";
        return 2;
    }
    std::ifstream corpus_file(argv[1]);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(corpus_file, line)) lines.push_back(line);
    if (!corpus_file.eof() || lines.empty()) {
        std::cerr << "could not read corpus\n";
        return 1;
    }
    const search::Corpus corpus(lines);
    const search::Index index(corpus);
    while (std::getline(std::cin, line)) {
        for (auto hit : index.retrieve(line, lines.size())) std::cout << hit.doc << ' ';
        std::cout << std::endl;
    }
    return std::cin.eof() ? 0 : 1;
}
