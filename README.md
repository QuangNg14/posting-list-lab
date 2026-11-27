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

## Relevance on BEIR SciFact

The separate `retrieval/` benchmark uses all 5,183 documents and 300 judged queries in the [BEIR SciFact test split](https://github.com/beir-cellar/beir#beers-available-datasets). BM25 calls this repository's C++ postings index. Dense search uses exact cosine ranking with [all-MiniLM-L6-v2](https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/tree/1110a243fdf4706b3f48f1d95db1a4f5529b4d41), the AVX2 uint8 ONNX export, a 256-token limit, attention-mask mean pooling and L2 normalization. Both see title plus abstract. Hybrid uses reciprocal rank fusion of each system's top 100, with equal weights and `k=60`. No training or threshold tuning is needed to run it.

```sh
# Linux or WSL, x86-64/AVX2, Python 3.11+ and a C++17 compiler
mkdir -p build/retrieval
python3 -m venv build/retrieval/venv
. build/retrieval/venv/bin/activate
python -m pip install -r retrieval/requirements.txt
python retrieval/prepare.py
c++ -std=c++17 -O3 -DNDEBUG search.cpp retrieval/bm25.cpp -o build/retrieval/bm25
python -m unittest discover -s retrieval
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 TOKENIZERS_PARALLELISM=false python retrieval/benchmark.py
```

For one local quality gate, run `OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 TOKENIZERS_PARALLELISM=false python retrieval/gate.py` after setup. It rebuilds the C++ adapter, reissues all 300 queries to all three systems, writes fresh results, and exits nonzero if any of the nine [checked-in floors](retrieval/thresholds.json) is missed. The floors sit 0.006 to 0.015 below the measured baselines; they are drift tolerances, not generalization targets. The gate never uses an old report as evidence. Only document embeddings are cached across runs, keyed by the model, tokenizer, corpus, runtime and encoding code. `--thresholds FILE` substitutes a different set of floors; latency is reported but not gated on shared hardware.

| System | Recall@10 | nDCG@10 | MRR | p50 ms | p95 ms |
|---|---:|---:|---:|---:|---:|
| C++ BM25 | 0.7843 | 0.6622 | 0.6348 | 5.64 | 12.49 |
| MiniLM | 0.7806 | 0.6391 | 0.6037 | 17.83 | 39.63 |
| RRF hybrid | 0.8089 | 0.6772 | 0.6462 | 23.71 | 47.62 |

This run used Ubuntu under WSL2 on a shared Core Ultra 7 165H, Zig 0.15.2 `c++ -O3`, ONNX Runtime 1.23.2 CPU, one inference/BLAS thread and sequential queries. Each system received five warm-up queries, then one timed observation per judged query, rotating system order. Percentiles use nearest rank. These are warm single-query latencies, not an isolated throughput benchmark.

Hybrid improved mean nDCG@10 by 0.0150 over BM25 but lost on **38 of 300** queries. On query 303, which names DMRT1 and MHM, BM25 ranked the first relevant document 2nd and dense ranked it 507th. The document got no dense vote in the top-100 fusion pool and fell to 14th. On query 660 it moved from 1st in BM25 to 8th in hybrid (dense: 620th). Exact-term evidence can be outweighed by agreement on other documents. The result is specific to these judgments, this encoder and this corpus.

Recall@10 and nDCG@10 are macro averages over judged queries. MRR uses the first judged-relevant document in each system's **returned list**, not just its top ten; a miss scores zero. The lexical system returns positive-score matches, dense ranks the whole corpus, and hybrid can return at most 200 candidates. Unjudged documents count as nonrelevant. Ties use corpus row order. The per-query timer includes query processing, ranking, BM25 subprocess communication, and both retrieval calls plus fusion for hybrid. It excludes model loading, document encoding, index construction, warm-up, and metric calculation. Disk artifacts stay under ignored `build/retrieval/`.

Data: [SciFact, Wadden et al.](https://aclanthology.org/2020.emnlp-main.609/), [CC BY-NC 2.0 per AllenAI](https://huggingface.co/datasets/allenai/scifact); [BEIR, Thakur et al.](https://arxiv.org/abs/2104.08663). The model is [Apache-2.0](https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2). Neither weights nor dataset text are vendored.
