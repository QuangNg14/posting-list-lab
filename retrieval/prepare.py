"""Download and verify the three inputs needed for the offline SciFact benchmark."""

import hashlib
import shutil
import urllib.request
from pathlib import Path

CACHE = Path(__file__).resolve().parents[1] / "build" / "retrieval"
MODEL = ("https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/"
         "1110a243fdf4706b3f48f1d95db1a4f5529b4d41/")
FILES = {
    "scifact.zip": (
        "https://public.ukp.informatik.tu-darmstadt.de/thakur/BEIR/datasets/scifact.zip",
        "536e14446a0ba56ed1398ab1055f39fe852686ecad24a6306c80c490fa8e0165"),
    "model.onnx": (MODEL + "onnx/model_quint8_avx2.onnx",
                   "b941bf19f1f1283680f449fa6a7336bb5600bdcd5f84d10ddc5cd72218a0fd21"),
    "tokenizer.json": (MODEL + "tokenizer.json",
                       "be50c3628f2bf5bb5e3a7f17b1f74611b2561a3a27eeab05e5aa30f411572037"),
}


def verify(path: Path, expected: str) -> None:
    with path.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != expected:
            raise ValueError(f"SHA-256 mismatch: {path}")


if __name__ == "__main__":
    CACHE.mkdir(parents=True, exist_ok=True)
    for name, (url, digest) in FILES.items():
        path = CACHE / name
        if not path.exists():
            tmp = path.with_suffix(".part")
            try:
                with urllib.request.urlopen(url, timeout=90) as source, tmp.open("wb") as target:
                    shutil.copyfileobj(source, target)
                verify(tmp, digest)
                tmp.replace(path)
            finally:
                tmp.unlink(missing_ok=True)
        else:
            verify(path, digest)
        print(f"Verified {name}")
