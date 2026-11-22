# When a full scan beats an index

Searching for `item417` and searching for `stock` both ask for the ten best BM25 matches. In this corpus, only one document contains `item417`; all 12,000 contain `stock`. The index took 1.336 microseconds at the median for rare item lookups. For `stock`, it took 529.334 microseconds. A full scan answered that same common query in 163.842 microseconds and returned the same ranking.

## What the index can skip

The [scan](https://github.com/QuangNg14/posting-list-lab/blob/e8d52ec0e265f8d8c464e01eb168a79a1f8106db/search.cpp#L85-L100) doesn't reread or tokenize the original text. Each document already has a sorted vector of term IDs and counts. For a known query term, the scan visits each document, looks for the term, and scores matches.

The [inverted index](https://github.com/QuangNg14/posting-list-lab/blob/e8d52ec0e265f8d8c464e01eb168a79a1f8106db/search.cpp#L102-L122) stores the same information in the other direction: for each term, a list of matching document IDs and counts. A rare item lookup needs one posting instead of 12,000 document lookups. For `stock`, the index can't skip a single document. It also updates a hash table of scores, then copies the entries into a hit vector. The scan scores each document and appends the hit directly. Both paths still have to choose the top ten.

## Which queries won

These are [recorded timings](https://github.com/QuangNg14/posting-list-lab/blob/e8d52ec0e265f8d8c464e01eb168a79a1f8106db/README.md#L27-L38) from one CPU, an Intel Core Ultra 7 165H, running Ubuntu 24.04 under WSL2. All times are microseconds per query. The p50 is the median; p95 is the 95th percentile.

| Query | Scan p50 | Scan p95 | Index p50 | Index p95 |
| --- | ---: | ---: | ---: | ---: |
| Rare: one `itemN` | 85.268 | 210.501 | 1.336 | 2.112 |
| Common: `stock` | 163.842 | 329.113 | 529.334 | 808.187 |
| Mixed: one `partN` and one `itemN` | 203.506 | 338.223 | 27.209 | 41.880 |
| Unknown: no dictionary entry | 0.241 | 0.933 | 0.227 | 0.585 |

The common query is the counterexample: the scan won at both percentiles. The unknown query is a useful control, not an index victory. Both paths consult the same dictionary and return before scoring. At this scale, timer overhead matters.

## What the latency table leaves out

Both engines paid **13.060 ms** to prepare the corpus. Constructing the index from that prepared corpus took **another 1.751 ms** in this run. The table measures queries after construction; it doesn't charge that build time to the first lookup.

The index held 141,921 postings. Its [capacity calculation](https://github.com/QuangNg14/posting-list-lab/blob/e8d52ec0e265f8d8c464e01eb168a79a1f8106db/search.cpp#L123-L133) estimated **1,573,912 additional bytes**: the reserved slots in the directory of lists, plus the reserved slots in each posting list. That counts spare vector capacity. It isn't process resident memory (RSS), allocator overhead, peak build memory, or the per-query score table.

I'd keep the scan as a baseline before paying either cost. Measure the terms people actually search for: a workload full of unique identifiers and one full of `stock` make different demands. This single-threaded, warm, synthetic run doesn't establish a universal cutoff or predict production throughput.

## Reproduce the comparison

From a fresh checkout on Linux or WSL, use the measured revision. The recorded run used Zig 0.15.2; `c++` with the same flags also builds the program, but timings can differ.

```sh
git clone https://github.com/QuangNg14/posting-list-lab.git
cd posting-list-lab
git switch --detach e8d52ec0e265f8d8c464e01eb168a79a1f8106db
mkdir -p build
zig c++ -std=c++17 -O3 -DNDEBUG search.cpp main.cpp bench.cpp -o build/postings
zig c++ -std=c++17 -O3 -DNDEBUG search.cpp tests.cpp -o build/tests
./build/tests
./build/postings bench
```

The [fixture and timing loop](https://github.com/QuangNg14/posting-list-lab/blob/e8d52ec0e265f8d8c464e01eb168a79a1f8106db/bench.cpp#L14-L60) fix the seed at `1207`. Each document has 12 tokens: `stock`, a unique item ID, and ten sampled parts. Each pass runs 48 queries, twelve per regime, with `k=10`. The loop alternates which engine goes first. It discards three warmup passes, then collects twelve measured passes: 144 observations per engine per regime.

Look for `exact_top_k_pairs=720` before comparing timings. After **both timers stop** for each pair, the harness compares the ordered hits, including [exact document IDs and scores](https://github.com/QuangNg14/posting-list-lab/blob/e8d52ec0e265f8d8c464e01eb168a79a1f8106db/search.hpp#L16-L18). A mismatch stops the run. Matching times aren't the reproduction criterion; matching rankings let you interpret the times your machine produces.
