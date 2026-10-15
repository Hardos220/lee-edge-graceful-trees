#!/usr/bin/env python3
"""Which structural parameter explains the search effort for edge-graceful labelings?

Takes a sample of the trees of order N (every k-th tree of several parts of
gentreeg), solves each of them with bin/egsolve under R seeds, and fits the
logarithm of the median number of local-search iterations by least squares on
the number d2 of vertices of degree two, the number of leaves and the
diameter (standardized). The effort is counted in iterations, so the result
does not depend on the machine.

usage: python tools/hardness_regression.py --n 27 --sample 20000 --repeat 5 --out runs/checks/hardness_regression.json
"""
import argparse
import json
import math
import os
import re
import subprocess
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
EXE = ".exe" if os.name == "nt" else ""
BIN = ROOT / "bin"
MOD = {21: 20, 23: 32, 25: 50, 27: 200, 29: 1000, 31: 4000}


def sample(n, k, parts):
    """every k-th tree of the given parts; 0-based parents of vertices 1..n-1"""
    out = []
    for res in parts:
        txt = subprocess.run([str(BIN / f"gentreeg{EXE}"), "-p", "-q", str(n), f"{res}/{MOD[n]}"],
                             capture_output=True, text=True, check=True).stdout.split("\n")
        for line in txt[::k]:
            if line.strip():
                p1 = [int(x) for x in line.split()]
                out.append([p1[j] - 1 for j in range(1, n)])
    return out


def features(par):
    n = len(par) + 1
    adj = [[] for _ in range(n)]
    for j, p in enumerate(par, start=1):
        adj[j].append(p)
        adj[p].append(j)
    deg = [len(a) for a in adj]

    def far(s):
        dist = [-1] * n
        dist[s], todo = 0, [s]
        for v in todo:
            for w in adj[v]:
                if dist[w] < 0:
                    dist[w] = dist[v] + 1
                    todo.append(w)
        m = max(dist)
        return dist.index(m), m

    a, _ = far(0)
    _, diam = far(a)
    return deg.count(2), deg.count(1), diam


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=27)
    ap.add_argument("--sample", type=int, default=20000)
    ap.add_argument("--repeat", type=int, default=5)
    ap.add_argument("--out", type=Path, default=ROOT / "runs" / "checks" / "hardness_regression.json")
    a = ap.parse_args()

    parts = [0, MOD[a.n] // 4, MOD[a.n] // 2, 3 * MOD[a.n] // 4]
    count = sum(int(re.search(r">Z (\d+) trees", subprocess.run(
        [str(BIN / f"gentreeg{EXE}"), "-u", str(a.n), f"{r}/{MOD[a.n]}"],
        capture_output=True, text=True).stderr).group(1)) for r in parts)
    trees = sample(a.n, max(1, count // a.sample), parts)
    r = subprocess.run([str(BIN / f"egsolve{EXE}"), "--repeat", str(a.repeat)],
                       input="\n".join(" ".join(map(str, p)) for p in trees) + "\n",
                       capture_output=True, text=True, check=True).stdout.split("\n")
    med = [int(l.split(":")[0].split()[2]) for l in r if l.strip()]
    assert len(med) == len(trees)
    X = np.array([features(p) for p in trees], dtype=float)
    y = np.log(np.array(med, dtype=float) + 1.0)
    names = ["d2", "leaves", "diameter"]
    Z = (X - X.mean(0)) / X.std(0)
    corr = {nm: float(np.corrcoef(X[:, i], y)[0, 1]) for i, nm in enumerate(names)}

    def fit(cols):
        A = np.column_stack([np.ones(len(y))] + [Z[:, i] for i in cols])
        coef, *_ = np.linalg.lstsq(A, y, rcond=None)
        res = y - A @ coef
        return coef[1:].tolist(), float(1 - res.var() / y.var())

    single = {nm: fit([i])[1] for i, nm in enumerate(names)}
    coef, r2 = fit([0, 1, 2])
    report = {"n": a.n, "trees": len(trees), "repeat": a.repeat, "parts": parts,
              "corr_with_log_effort": corr, "r2_single": single,
              "standardized_coef_joint": dict(zip(names, coef)), "r2_joint": r2,
              "corr_d2_leaves": float(np.corrcoef(X[:, 0], X[:, 1])[0, 1]),
              "corr_d2_diameter": float(np.corrcoef(X[:, 0], X[:, 2])[0, 1]),
              "median_effort_quartiles": [float(q) for q in np.percentile(med, [25, 50, 75, 90, 99])]}
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(report, indent=1))
    print(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()
