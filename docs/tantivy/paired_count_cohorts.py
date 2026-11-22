"""Compare saved TOP_10 times on queries with matching full COUNT results.

Input: results.json at quickwit-oss/search-benchmark-game@fd3da9e0.
No engines are executed. Times are medians of per-query median durations, in us.
"""

import hashlib
import json
import sys
from pathlib import Path
from statistics import median


SNAPSHOT = "9c28927ba63ba02686a42bbdc66d00cfd0dbc35452a2afaa48f9f46c20fb9e83"
ENGINES = ("tantivy-0.26", "pisa-0.8.2")


def entries(rows: list[dict], tag: str) -> dict[str, dict]:
    selected = [row for row in rows if tag in row["tags"]]
    by_query = {row["query"]: row for row in selected}
    if len(by_query) != len(selected):
        raise ValueError(f"duplicate queries for {tag}")
    return by_query


def report(data: dict) -> None:
    print("tag\tmatched/all\tCOUNT range\tqueries\tTantivy p50 us\tPISA p50 us\tsum paired delta us")
    for tag in ("union", "intersection", "phrase"):
        left, right, lc, rc = [
            entries(data[command][engine], tag)
            for command in ("TOP_10", "COUNT") for engine in ENGINES
        ]
        if not left.keys() == right.keys() == lc.keys() == rc.keys():
            raise ValueError(f"query sets differ for {tag}")
        paired = []
        for q in left:
            count = lc[q]["count"]
            if count != rc[q]["count"] or count <= 0:
                continue
            a, b = left[q]["duration"], right[q]["duration"]
            if len(a) != 10 or len(b) != 10:
                raise ValueError("expected ten saved TOP_10 durations per engine")
            paired.append((count, median(a), median(b)))
        bounds = [(0, float("inf"))]
        if tag == "union":
            bounds += [(0, 10_000), (10_000, 100_000), (100_000, 1_000_000), (1_000_000, float("inf"))]
        for low, high in bounds:
            cohort = [(a, b) for n, a, b in paired if low <= n < high]
            prefix = f"{tag}\t{len(paired)}/{len(left)}\t[{low},{high})\t{len(cohort)}"
            if not cohort:
                print(f"{prefix}\tNA\tNA\tNA")
                continue
            a, b = zip(*cohort)
            print(f"{prefix}\t{median(a):g}\t{median(b):g}\t{sum(x-y for x, y in cohort):g}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: python paired_count_cohorts.py path/to/pinned/results.json")
    raw = Path(sys.argv[1]).read_bytes()
    if hashlib.sha256(raw).hexdigest() != SNAPSHOT:
        sys.exit("not the pinned results.json; download the raw file at fd3da9e0840003d548d8bd1daa4f358386811d7a")
    report(json.loads(raw)["results"])
