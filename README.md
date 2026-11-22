# Posting List Lab

Search a text file with BM25, then measure when a postings index pays for itself. Each line is one document; the CLI prints the matching line number, relevance score, and text. No external libraries, server, or downloads at runtime.

**Case study:** [When a full scan beats an index](docs/when-a-full-scan-beats-an-index.md) explains why a rare item lookup and a search for `stock` favored opposite retrieval paths.

```sh
# Linux or WSL, with a C++17 compiler (c++, clang++, or zig c++).
mkdir -p build
c++ -std=c++17 -O3 -DNDEBUG search.cpp main.cpp bench.cpp -o build/postings
c++ -std=c++17 -O3 -DNDEBUG search.cpp tests.cpp -o build/tests
./build/tests
./build/postings search example.txt 3 copper lantern
./build/postings bench
```

## What is compared

Both engines lowercase ASCII letters, split on non-alphanumeric bytes, and use the same document frequencies and BM25 scoring (`k1=1.2`, `b=0.75`). Multiple query terms are ORed; repeats multiply a term's weight. Only positive-score hits are returned, ordered by score descending, then line number ascending.

The scan is a competitive reference, not repeated raw-text tokenization: it visits every document for a known query and looks up term frequencies in each sorted document vector. The inverted index stores `(document ID, term frequency)` once per document/term, traverses only the relevant lists, and accumulates scores in a sparse hash table. Both use the same final top-k selection. An all-unknown query takes the same dictionary shortcut in both paths.

## Benchmark protocol

The fixture uses seed `1207`, 12,000 synthetic 12-token documents, and `k=10`. Each document contains `stock`, one unique `itemN`, and ten tokens sampled from 256 parts. Each pass contains 48 queries: 12 unique-item lookups, 12 repeats of the ubiquitous `stock`, 12 mixed part/item lookups, and 12 absent strings. A single thread runs both paths on the same in-memory snapshot, alternating which goes first. Every query checks **exact score and document order**, outside the timer.

Three warmup passes precede 12 measured passes: 144 single-query samples per path per regime, 576 per path in total. Reported latencies are nearest-rank microseconds including query analysis, scoring, and top-k selection, excluding corpus setup, checking and I/O. Build timings are separate single observations. Reserved index bytes count vector capacities for the list directory and its posting entries; this is **additional index storage**, not process RSS, peak build memory, or scratch space.

### One local run

Ubuntu 24.04 under WSL2, x86-64 Intel Core Ultra 7 165H; Zig 0.15.2 `c++ -std=c++17 -O3 -DNDEBUG`. Times below are microseconds per query on a warm, in-memory synthetic corpus.

| Query regime | Scan p50 | Scan p95 | Postings p50 | Postings p95 |
| --- | ---: | ---: | ---: | ---: |
| Rare | 85.268 | 210.501 | 1.336 | 2.112 |
| Common | 163.842 | 329.113 | 529.334 | 808.187 |
| Mixed | 203.506 | 338.223 | 27.209 | 41.880 |
| Unknown | 0.241 | 0.933 | 0.227 | 0.585 |

Preparing the shared corpus took 13.060 ms. Building the additional index took 1.751 ms and reserved 1,573,912 bytes for 141,921 postings and their list directory. The rare-term query touches just one posting. The common query touches 12,000 and pays hash-accumulation overhead; here **the scan was faster**. Unknown-query times are close to timer overhead. These measurements do not establish real-corpus relevance, throughput under concurrency, or a universal speedup.
