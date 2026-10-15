#!/usr/bin/env python3
"""Independent check that gentreeg lists every tree of order n exactly once.

For every tree printed by `gentreeg -p` a canonical string is computed here in
Python (AHU encoding about the center or the two centers; no nauty code) and
hashed to 64 bits. Isomorphic trees have equal strings, hence equal hashes, so
if no hash repeats there is no duplicated tree; with the number of trees equal
to A000055(n) the list is then complete. A repeated hash is reported (it would
need an explicit comparison; for 4e7 trees a 64-bit collision has probability
about 1e-4).

usage: python tools/check_uniqueness.py --nmax 24 --jobs 7 --out runs/checks/uniqueness.json
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
from array import array
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = ".exe" if os.name == "nt" else ""
GENTREEG = str(ROOT / "bin" / f"gentreeg{EXE}")
A000055 = [1, 1, 1, 1, 2, 3, 6, 11, 23, 47, 106, 235, 551, 1301, 3159, 7741, 19320, 48629,
           123867, 317955, 823065, 2144505, 5623756, 14828074, 39299897, 104636890, 279793450]
NBUCKET = 256


def canonical(n, par):
    """par[j] (j = 1..n, 1-based, par[1] = 0) -> canonical string of the free tree."""
    adj = [[] for _ in range(n + 1)]
    for j in range(2, n + 1):
        adj[j].append(par[j])
        adj[par[j]].append(j)
    deg = [len(a) for a in adj]
    layer = [v for v in range(1, n + 1) if deg[v] <= 1]
    left = n
    while left > 2:
        left -= len(layer)
        nxt = []
        for v in layer:
            for w in adj[v]:
                deg[w] -= 1
                if deg[w] == 1:
                    nxt.append(w)
        layer = nxt
    centers = layer if n > 1 else [1]

    def enc(root, avoid):
        # iterative post-order to avoid deep recursion
        order, stack, parent = [], [(root, avoid)], {root: avoid}
        while stack:
            v, p = stack.pop()
            order.append(v)
            for w in adj[v]:
                if w != p:
                    parent[w] = v
                    stack.append((w, v))
        code = {}
        for v in reversed(order):
            code[v] = "(" + "".join(sorted(code[w] for w in adj[v] if w != parent[v])) + ")"
        return code[root]

    if len(centers) == 1:
        return enc(centers[0], 0)
    a, b = centers                                        # bicentral: split the central edge
    return "|".join(sorted((enc(a, b), enc(b, a))))


def part_hashes(args):
    n, res, mod = args
    out = subprocess.run([GENTREEG, "-p", "-q", str(n), f"{res}/{mod}"], capture_output=True, text=True,
                         check=True).stdout
    buckets = [array("Q") for _ in range(NBUCKET)]
    count = 0
    for line in out.splitlines():
        if not line.strip():
            continue
        par = [0] + [int(x) for x in line.split()]
        h = int.from_bytes(hashlib.blake2b(canonical(n, par).encode(), digest_size=8).digest(), "little")
        buckets[h & (NBUCKET - 1)].append(h)
        count += 1
    return count, [b.tobytes() for b in buckets]


def check_order(n, mod, jobs):
    t0 = time.time()
    total, buckets = 0, [array("Q") for _ in range(NBUCKET)]
    with ProcessPoolExecutor(max_workers=jobs) as pool:
        for count, parts in pool.map(part_hashes, [(n, r, mod) for r in range(mod)]):
            total += count
            for i, raw in enumerate(parts):
                buckets[i].frombytes(raw)
    repeats = sum(len(b) - len(set(b)) for b in buckets)
    return {"n": n, "parts": mod, "trees": total, "A000055": A000055[n], "count_ok": total == A000055[n],
            "repeated_hashes": repeats, "unique_and_complete": total == A000055[n] and repeats == 0,
            "seconds": round(time.time() - t0, 1)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nmin", type=int, default=1)
    ap.add_argument("--nmax", type=int, default=24)
    ap.add_argument("--jobs", type=int, default=int(os.environ.get("RMQ_CPU", os.cpu_count() or 1)))
    ap.add_argument("--out", type=Path, default=ROOT / "runs" / "checks" / "uniqueness.json")
    a = ap.parse_args()
    a.out.parent.mkdir(parents=True, exist_ok=True)
    results = []
    for n in range(a.nmin, a.nmax + 1):
        mod = max(1, A000055[n] // 300000)
        r = check_order(n, mod, a.jobs)
        results.append(r)
        print(json.dumps(r), flush=True)
        a.out.write_text(json.dumps(results, indent=1))
        prog = os.environ.get("RMQ_PROGRESS_FILE")
        if prog:
            Path(prog).write_text(f"{n - a.nmin + 1}/{a.nmax - a.nmin + 1} n={n}")
    ok = all(r["unique_and_complete"] for r in results)
    print("ALL ORDERS UNIQUE AND COMPLETE" if ok else "PROBLEM FOUND")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
