#!/usr/bin/env python3
"""Third, fully independent check of certificates, written in Python.

For every item of the plan the certificate of one part is regenerated with
`egrun --cert -` (the search is deterministic), and every record is checked
here against the tree read from the text output of the stand-alone
`gentreeg -p`: the labels must be a bijection onto 1..n-1 and the vertex sums
pairwise distinct modulo n (edge-graceful) or as integers (antimagic). The
SHA-256 of the regenerated certificate is compared with the hash recorded by
the original run. Shares no code with egsearch/egrun/egverify.

usage: python tools/py_recheck.py --plan runs/checks/recheck_plan.json --jobs 7 --out runs/checks/recheck.json
plan: [{"n": 31, "res": 2666, "mod": 4000, "mode": "eg", "count": 10017311, "sha256": "..."}, ...]
"""
import argparse
import hashlib
import json
import os
import subprocess
import tempfile
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = ".exe" if os.name == "nt" else ""
BIN = ROOT / "bin"


def check_item(item):
    n, res, mod, mode = item["n"], item["res"], item["mod"], item["mode"]
    t0 = time.time()
    rb = (5 * (n - 1) + 7) // 8
    with tempfile.TemporaryDirectory() as tmp:
        cert = subprocess.Popen([str(BIN / f"egrun{EXE}"), str(n), str(res), str(mod), "--mode", mode,
                                 "--cert", "-", "--count", str(item["count"]),
                                 "--summary", os.path.join(tmp, "s.json")],
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        trees = subprocess.Popen([str(BIN / f"gentreeg{EXE}"), "-p", "-q", str(n), f"{res}/{mod}"],
                                 stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        sha = hashlib.sha256()
        header = cert.stdout.read(64)
        sha.update(header)
        ok_header = header[:7] == b"EGCERT1" and int.from_bytes(header[8:12], "little") == n
        valid = invalid = count = 0
        for line in trees.stdout:
            if not line.strip():
                continue
            par = [0] + [int(x) for x in line.split()]
            rec = cert.stdout.read(rb)
            if len(rec) != rb:
                invalid += 1
                break
            sha.update(rec)
            count += 1
            x = int.from_bytes(rec, "little")
            labels = [(x >> (5 * i)) & 31 for i in range(n - 1)]       # edge (j, par[j]), j = 2..n
            if sorted(labels) != list(range(1, n)):
                invalid += 1
                continue
            s = [0] * (n + 1)
            for j in range(2, n + 1):
                lab = labels[j - 2]
                s[j] += lab
                s[par[j]] += lab
            sums = s[1:]
            keys = [v % n for v in sums] if mode == "eg" else sums
            if len(set(keys)) == n and len(set(sums)) == n:
                valid += 1
            else:
                invalid += 1
        rest = cert.stdout.read()
        sha.update(rest)
        cert.wait()
        trees.wait()
    digest = sha.hexdigest()
    return {**item, "checked": count, "valid": valid, "invalid": invalid, "header_ok": ok_header,
            "trailing_bytes": len(rest), "sha256_regenerated": digest,
            "sha256_match": digest == item["sha256"],
            "ok": ok_header and valid == count == item["count"] and invalid == 0 and not rest
                  and digest == item["sha256"],
            "seconds": round(time.time() - t0, 1)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plan", type=Path, required=True)
    ap.add_argument("--jobs", type=int, default=int(os.environ.get("RMQ_CPU", os.cpu_count() or 1)))
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()
    plan = json.loads(a.plan.read_text())
    a.out.parent.mkdir(parents=True, exist_ok=True)
    results = []
    with ProcessPoolExecutor(max_workers=a.jobs) as pool:
        for r in pool.map(check_item, sorted(plan, key=lambda it: -it["count"])):
            results.append(r)
            print(json.dumps({k: r[k] for k in ("n", "res", "mod", "checked", "valid", "invalid", "sha256_match", "ok", "seconds")}), flush=True)
            a.out.write_text(json.dumps(results, indent=1))
            prog = os.environ.get("RMQ_PROGRESS_FILE")
            if prog:
                Path(prog).write_text(f"{len(results)}/{len(plan)} parts")
    ok = all(r["ok"] for r in results)
    print("ALL RECHECKED PARTS OK" if ok else "PROBLEM FOUND")
    raise SystemExit(0 if ok else 1)


if __name__ == "__main__":
    main()
