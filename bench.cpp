#include "search.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;
using Samples = std::array<std::vector<double>, 2>;
constexpr int seed = 1207, docs = 12000, queries = 48, warmups = 3, passes = 12, k = 10;
constexpr std::array<const char*, 4> labels{"rare", "common", "mixed", "unknown"};

double micros(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double, std::micro>(to - from).count();
}
double quantile(std::vector<double> data, double q) {
    std::sort(data.begin(), data.end());
    return data[static_cast<std::size_t>(std::ceil(q * data.size())) - 1];
}
}

void benchmark() {
    std::mt19937 rng(seed);
    std::vector<std::string> lines, workload;
    for (int i = 0; i < docs; ++i) {
        std::string doc = "stock item" + std::to_string(i);
        for (int j = 0; j < 10; ++j) doc += " part" + std::to_string(rng() % 256);
        lines.push_back(std::move(doc));
    }
    for (int i = 0; i < queries / 4; ++i) {
        workload.push_back("item" + std::to_string(rng() % docs));
        workload.emplace_back("stock");
        workload.push_back("part" + std::to_string(rng() % 256) + " item" + std::to_string(i));
        workload.push_back("absent" + std::to_string(i));
    }
    auto start = Clock::now();
    const search::Corpus corpus(lines);
    auto prepared = Clock::now();
    const search::Index index(corpus);
    auto indexed = Clock::now();
    std::array<Samples, 4> groups;
    std::size_t comparisons = 0;
    for (int round = 0; round < warmups + passes; ++round) {
        for (int i = 0; i < queries; ++i) {
            std::array<search::Hits, 2> hits;
            for (int j = 0; j < 2; ++j) {
                const int path = (round + i + j) % 2;
                auto begin = Clock::now();
                hits[path] = path ? index.retrieve(workload[i], k) : corpus.scan(workload[i], k);
                auto end = Clock::now();
                if (round >= warmups) groups[i % 4][path].push_back(micros(begin, end));
            }
            if (hits[0] != hits[1]) throw std::logic_error("top-k mismatch for " + workload[i]);
            ++comparisons;
        }
    }
    std::cout << std::fixed << std::setprecision(3)
              << "seed=" << seed << " documents=" << docs << " k=" << k << " queries=" << queries
              << " warmup_passes=" << warmups << " measured_passes=" << passes << '\n'
              << "common_preparation_ms=" << micros(start, prepared) / 1000
              << " additional_index_build_ms=" << micros(prepared, indexed) / 1000
              << " postings=" << index.posting_count() << " reserved_index_bytes=" << index.reserved_bytes() << '\n'
              << "regime,samples_per_path,scan_p50_us,scan_p95_us,index_p50_us,index_p95_us\n";
    for (std::size_t i = 0; i < groups.size(); ++i) {
        std::cout << labels[i] << ',' << groups[i][0].size();
        for (const auto& path : groups[i]) std::cout << ',' << quantile(path, 0.50) << ',' << quantile(path, 0.95);
        std::cout << '\n';
    }
    std::cout << "exact_top_k_pairs=" << comparisons << '\n';
}
