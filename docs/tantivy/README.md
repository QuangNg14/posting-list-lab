# Comparing saved Tantivy and PISA timings

This script reanalyzes the [search-benchmark-game snapshot](https://github.com/quickwit-oss/search-benchmark-game/tree/fd3da9e0840003d548d8bd1daa4f358386811d7a). It does not run either search engine and is separate from this repository's C++ BM25 benchmark.

Download the pinned [`results.json`](https://raw.githubusercontent.com/quickwit-oss/search-benchmark-game/fd3da9e0840003d548d8bd1daa4f358386811d7a/results.json) outside the checkout. From the repository root, run:

```sh
python docs/tantivy/paired_count_cohorts.py /path/to/results.json
```

The script verifies SHA-256 `9c28927ba63ba02686a42bbdc66d00cfd0dbc35452a2afaa48f9f46c20fb9e83` and prints aggregates, not query text. It pairs `TOP_10` rows by query within each exact tag and keeps only pairs whose separate `COUNT` results are equal and positive. Equal counts do not establish that the engines matched the same document IDs.

All 301 union queries qualified. Taking the median of ten saved timings per query, then the median across queries, gives **317.5 microseconds for Tantivy 0.26** and **208 microseconds for PISA 0.8.2**. The 147 queries with counts from 100,000 up to but not including 1 million accounted for 41,782.5 of the 62,258 microseconds of total signed difference (Tantivy minus PISA, summed over per-query medians).

That range is a candidate for profiling, not a wall-clock speedup or a diagnosis. No new CPU counter samples were collected; the WSL guest had no CPU PMU available. None of the 300 phrase queries met the equal-positive-count condition, so this comparison yields no phrase latency conclusion.
