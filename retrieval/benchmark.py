"""Compare warm, single-query retrieval against BEIR SciFact test judgments."""

import csv
import hashlib
import inspect
import io
import json
import math
import subprocess
import zipfile
from collections import defaultdict
from statistics import mean
from time import perf_counter_ns

import numpy as np
import onnxruntime as ort
from tokenizers import Tokenizer
from tokenizers import __version__ as tokenizer_version

from prepare import CACHE, FILES, verify


def load():
    with zipfile.ZipFile(CACHE / "scifact.zip") as archive:
        def rows(name):
            with archive.open(f"scifact/{name}.jsonl") as stream:
                return [json.loads(line) for line in stream]

        corpus = rows("corpus")
        lookup = {row["_id"]: i for i, row in enumerate(corpus)}
        texts = [" ".join((row["title"] + " " + row["text"]).split()) for row in corpus]
        queries = {row["_id"]: row["text"] for row in rows("queries")}
        qrels = defaultdict(dict)
        with archive.open("scifact/qrels/test.tsv") as stream:
            for row in csv.DictReader(io.TextIOWrapper(stream), delimiter="\t"):
                if (grade := int(row["score"])) > 0:
                    qrels[row["query-id"]][lookup[row["corpus-id"]]] = grade
    return texts, queries, dict(qrels)


def encoder():
    tokenizer = Tokenizer.from_file(str(CACHE / "tokenizer.json"))
    tokenizer.enable_truncation(max_length=256)
    tokenizer.enable_padding(pad_id=0, pad_token="[PAD]")
    options = ort.SessionOptions()
    options.intra_op_num_threads = options.inter_op_num_threads = 1
    model = ort.InferenceSession(str(CACHE / "model.onnx"), options, providers=["CPUExecutionProvider"])

    def encode(texts):
        batch = tokenizer.encode_batch(texts)
        ids = np.asarray([t.ids for t in batch], dtype=np.int64)
        mask = np.asarray([t.attention_mask for t in batch], dtype=np.int64)
        hidden, = model.run(["last_hidden_state"], {
            "input_ids": ids, "attention_mask": mask, "token_type_ids": np.zeros_like(ids)
        })
        pooled = (hidden * mask[:, :, None].astype(np.float32)).sum(axis=1)
        return pooled / np.linalg.norm(pooled, axis=1, keepdims=True)

    return encode


def reciprocal_rank_fusion(lexical, dense):
    scores = defaultdict(float)
    for ranking in (lexical, dense):
        for rank, doc in enumerate(ranking[:100], 1):
            scores[doc] += 1 / (60 + rank)
    return sorted(scores, key=lambda doc: (-scores[doc], doc))


def quality(ranking, relevant):
    gains = [relevant.get(doc, 0) for doc in ranking[:10]]
    ideal = sorted(relevant.values(), reverse=True)[:10]

    def dcg(values):
        return sum(gain / math.log2(rank + 1) for rank, gain in enumerate(values, 1))

    rank = next((i for i, doc in enumerate(ranking, 1) if doc in relevant), None)
    return {"recall@10": sum(g > 0 for g in gains) / len(relevant),
            "ndcg@10": dcg(gains) / dcg(ideal), "mrr": 1 / rank if rank else 0}, rank


def document_vectors(encode, texts):
    # Only document embeddings are cached. Changing the data, model or encoding code invalidates them.
    key = hashlib.sha256((repr(FILES) + inspect.getsource(encoder) + inspect.getsource(load)
                          + inspect.getsource(document_vectors) + ort.__version__ + np.__version__
                          + tokenizer_version).encode()).hexdigest()
    cache = CACHE / f"vectors-{key}.npy"
    if cache.exists():
        return np.load(cache, allow_pickle=False)
    order = sorted(range(len(texts)), key=lambda doc: len(texts[doc]))
    vectors = np.empty((len(texts), 384), dtype=np.float32)
    for start in range(0, len(order), 16):
        batch = order[start:start + 16]
        vectors[batch] = encode([texts[i] for i in batch])
    tmp = cache.with_suffix(".tmp")
    with tmp.open("wb") as stream:
        np.save(stream, vectors, allow_pickle=False)
    tmp.replace(cache)
    return vectors


def run():
    for name, (_, digest) in FILES.items():
        verify(CACHE / name, digest)
    texts, queries, qrels = load()
    encode = encoder()
    vectors = document_vectors(encode, texts)

    def dense(query):
        scores = vectors @ encode([query])[0]
        return np.argsort(-scores, kind="stable").tolist()

    lines = CACHE / "corpus.lines"
    lines.write_text("\n".join(texts) + "\n", encoding="utf-8")
    methods = ["bm25", "dense", "hybrid"]
    samples = {name: [] for name in methods}
    metrics = {name: [] for name in methods}
    losses = []
    with subprocess.Popen([str(CACHE / "bm25"), str(lines)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          text=True, encoding="utf-8") as process:
        def lexical(query):
            process.stdin.write(" ".join(query.split()) + "\n")
            process.stdin.flush()
            line = process.stdout.readline()
            if not line:
                raise RuntimeError(f"BM25 process exited: {process.wait()}")
            return list(map(int, line.split()))

        def retrieve(name, query):
            if name == "bm25":
                return lexical(query)
            if name == "dense":
                return dense(query)
            return reciprocal_rank_fusion(lexical(query), dense(query))

        try:
            for qid in list(qrels)[:5]:
                for method in methods:
                    retrieve(method, queries[qid])
            for i, (qid, relevant) in enumerate(qrels.items()):
                first = {}
                for method in methods[i % 3:] + methods[:i % 3]:
                    begin = perf_counter_ns()
                    ranking = retrieve(method, queries[qid])
                    samples[method].append((perf_counter_ns() - begin) / 1e6)
                    result, first[method] = quality(ranking, relevant)
                    metrics[method].append(result)
                delta = metrics["bm25"][-1]["ndcg@10"] - metrics["hybrid"][-1]["ndcg@10"]
                if delta > 0:
                    losses.append({"query_id": qid, "bm25_minus_hybrid_ndcg": delta, "first_relevant_rank": first})
        finally:
            process.stdin.close()
        if process.wait():
            raise RuntimeError("BM25 process failed")

    systems = {}
    for name in methods:
        systems[name] = {key: mean(x[key] for x in metrics[name]) for key in metrics[name][0]}
        systems[name].update({f"p{p}_ms": float(np.percentile(samples[name], p, method="inverted_cdf"))
                              for p in (50, 95)})
    result = {"dataset": "BEIR/SciFact:test", "documents": len(texts), "queries": len(qrels),
              "systems": systems, "hybrid_losses_vs_bm25": len(losses),
              "largest_losses": sorted(losses, key=lambda x: -x["bm25_minus_hybrid_ndcg"])[:5]}
    (CACHE / "results.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


if __name__ == "__main__":
    print(json.dumps(run(), indent=2))
