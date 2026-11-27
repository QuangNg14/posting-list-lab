"""Rebuild BM25, run every judged query, and enforce the checked-in relevance floors."""

import argparse
import json
import math
import os
import shlex
import subprocess
from pathlib import Path

from benchmark import run
from prepare import CACHE

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--thresholds", type=Path, default=HERE / "thresholds.json")
    args = parser.parse_args()
    floors = json.loads(args.thresholds.read_text(encoding="utf-8"))
    systems = {"bm25", "dense", "hybrid"}
    metrics = {"recall@10", "ndcg@10", "mrr"}
    if set(floors) != systems or any(set(row) != metrics for row in floors.values()):
        parser.error("thresholds must cover all three relevance metrics for every system")
    if any(not 0 <= v <= 1 for row in floors.values() for v in row.values()):
        parser.error("every floor must be between 0 and 1")
    subprocess.run([*shlex.split(os.environ.get("CXX", "c++")), "-std=c++17", "-O3", "-DNDEBUG",
                    "search.cpp", "retrieval/bm25.cpp", "-o", str(CACHE / "bm25")], cwd=HERE.parent, check=True)
    report = run()
    if (report["documents"], report["queries"]) != (5183, 300):
        raise ValueError("incomplete SciFact test split")
    passed = True
    for system, checks in floors.items():
        for metric, minimum in checks.items():
            value = report["systems"][system][metric]
            ok = math.isfinite(value) and minimum <= value <= 1
            print(f"{'PASS' if ok else 'FAIL'} {system} {metric}: {value:.4f} (minimum {minimum:.4f})")
            passed &= ok
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
