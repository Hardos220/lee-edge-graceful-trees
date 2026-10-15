#!/usr/bin/env python3
"""Number of edge-graceful labelings with a prescribed zero vertex, small orders.

Runs `gentreeg -p -q N | zrcount N` (exhaustive over all (N-1)! labelings) for
odd N and checks against the paper:
  - a leaf is never the zero vertex (Lemma 2.2);
  - every count is divisible by phi(N) (multiplication by units, Lemma 2.1);
  - the pairs (T, v), v internal, with count 0 are exactly the double-star
    centers of Proposition 1.5 plus, for N = 9, the two centers of D(2,2,4).
Reports, per order, the minimum count over the remaining (non-exceptional)
pairs and where it is attained.

usage: python tools/zr_counts.py --orders 5 7 9 11 --out runs/checks/zr_counts.json
"""
import argparse
import json
import math
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from zr_analyze import adjacency, family_member  # noqa: E402

EXE = ".exe" if os.name == "nt" else ""


def phi(n):
    return sum(1 for a in range(1, n) if math.gcd(a, n) == 1)


def run_order(n):
    gen = subprocess.Popen([str(ROOT / "bin" / f"gentreeg{EXE}"), "-p", "-q", str(n)], stdout=subprocess.PIPE)
    out = subprocess.run([str(ROOT / "bin" / f"zrcount{EXE}"), str(n)], stdin=gen.stdout,
                         capture_output=True, text=True, check=True).stdout
    gen.wait()
    trees, pairs, other_zero = 0, 0, []
    best = None
    for line in out.splitlines():
        left, right = line.split(":")
        p1 = [int(x) for x in left.split()]
        counts = [int(x) for x in right.split()]
        par = [p1[j] - 1 for j in range(1, n)]            # 0-based parent of vertex j = 1..n-1
        adj = adjacency(par)
        trees += 1
        for v in range(n):
            c = counts[v]
            assert c % phi(n) == 0, (n, line)
            if len(adj[v]) == 1:
                assert c == 0, ("leaf is a zero vertex", n, line)
                continue
            pairs += 1
            fam = family_member(adj, v) is not None
            if fam:
                assert c == 0, ("family pair has a labeling", n, line, v)
            elif c == 0:
                other_zero.append((par, v))
            elif best is None or c < best[0]:
                best = (c, par, v)
    assert len(other_zero) == (2 if n == 9 else 0), (n, other_zero)
    return {"n": n, "trees": trees, "internal_pairs": pairs, "phi": phi(n),
            "other_exceptions": len(other_zero), "min_count": best[0],
            "min_count_per_unit_class": best[0] // phi(n), "argmin_parents": best[1], "argmin_vertex": best[2]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--orders", type=int, nargs="+", default=[5, 7, 9, 11])
    ap.add_argument("--out", type=Path, default=ROOT / "runs" / "checks" / "zr_counts.json")
    a = ap.parse_args()
    a.out.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    for n in a.orders:
        r = run_order(n)
        rows.append(r)
        print(json.dumps(r), flush=True)
        a.out.write_text(json.dumps(rows, indent=1))
        prog = os.environ.get("RMQ_PROGRESS_FILE")
        if prog:
            Path(prog).write_text(f"{len(rows)}/{len(a.orders)} n={n}")


if __name__ == "__main__":
    main()
