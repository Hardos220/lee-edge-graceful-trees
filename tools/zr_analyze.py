#!/usr/bin/env python3
"""Compare the (tree, vertex) pairs without an edge-graceful labeling having
sum 0 at the vertex (mode zr) with the explicit family

    D(k, m, L): two stars K_{1,k} (centre v) and K_{1,m} (centre w), k >= 1,
    m >= 0, whose centres are joined by a path with L edges whose inner
    vertices have degree 2;  L in {2, 3};  bad vertices: the centres
    (a centre with no leaves is a leaf of the tree and is not counted).

Necessity (proof): in an edge-graceful labeling every leaf has residue equal
to its edge label, so the internal vertices get exactly {0} u {labels of the
edges not incident to a leaf}.  For L = 2 the middle vertex u has sum
p1 + p2, which is neither p1 nor p2, hence 0 - so no centre is 0.  For L = 3,
a centre at 0 forces p1+p2 = p3 and p2+p3 = p1, i.e. 2*p2 = 0, impossible for
odd n.  The computation checks that no other pair is impossible.

usage: python tools/zr_analyze.py runs/zr_n25        (a run_parts output dir)
       python tools/zr_analyze.py --hard FILE --n N  (one egrun --hard file)
"""
import argparse
import collections
import re
from pathlib import Path


def adjacency(par):
    n = len(par) + 1
    adj = [[] for _ in range(n)]
    for j, p in enumerate(par, start=1):
        adj[j].append(p)
        adj[p].append(j)
    return adj


def family_member(adj, v):
    """(k, m, L) if v is a centre of a double star D(k, m, L) with L in {2,3}, else None."""
    n = len(adj)
    if len(adj[v]) < 2:
        return None
    big = [w for w in adj[v] if len(adj[w]) > 1]
    if len(big) != 1:
        return None
    k = len(adj[v]) - 1
    prev, cur, L = v, big[0], 1
    while len(adj[cur]) == 2:                    # walk along the degree-2 path
        nxt = adj[cur][0] if adj[cur][0] != prev else adj[cur][1]
        prev, cur, L = cur, nxt, L + 1
    if len(adj[cur]) == 1:                       # path ends in a leaf: m = 0, w is that leaf
        m, Lw = 0, L
    else:
        others = [w for w in adj[cur] if w != prev]
        if any(len(adj[w]) != 1 for w in others):
            return None
        m, Lw = len(others), L
    covered = 1 + k + (Lw - 1) + 1 + m
    if covered != n:
        return None
    # with m = 0 the tree is also D(k, 1, L-1); report the representation with L in {2,3}
    if m == 0 and Lw in (3, 4):
        return (k, 1, Lw - 1)
    return (k, m, Lw) if Lw in (2, 3) else None


def read_pairs(hard_files):
    pairs, unsolved = [], []
    for f in hard_files:
        for line in Path(f).read_text().splitlines():
            m = re.match(r"(\d+) (IMPOSSIBLE|unsolved) \d+ \d+ \d+ v=(\d+) : (.*)", line)
            if not m:
                continue
            par = list(map(int, m.group(4).split()))
            (pairs if m.group(2) == "IMPOSSIBLE" else unsolved).append((f, int(m.group(1)), int(m.group(3)), par))
    return pairs, unsolved


def family_size(n):
    """Number of (tree, centre) pairs of the family for odd order n.
    D(k, m, 2): k + m = n - 3; D(k, m, 3): k + m = n - 4 with m >= 1 (m = 0 is D(k, 1, 2)).
    Each centre with at least one leaf counts; for k = m both centres count."""
    pairs = set()
    for L, total, mmin in ((2, n - 3, 0), (3, n - 4, 1)):
        for k in range(1, total + 1):
            m = total - k
            if m < mmin:
                continue
            tree = (L, min(k, m), max(k, m))
            pairs.add((tree, k))                   # the centre with k leaves
            if k == m:
                pairs.add((tree, "twin"))          # equal stars: the other centre too
    return len(pairs)


def summarize_run(run):
    """Records checked by egcheck in a run_parts output directory."""
    import json
    tot = collections.Counter()
    for f in sorted(run.glob("parts/*.done.json")):
        v = json.loads(f.read_text())["verify"]
        tot["parts"] += 1
        tot["checker_rc_nonzero"] += v.get("returncode") not in (0, 1)
        for k in ("trees", "records", "records_valid", "records_invalid", "records_empty"):
            tot[k] += v.get(k, 0)
    return tot


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run", nargs="?", type=Path)
    ap.add_argument("--hard", type=Path)
    ap.add_argument("--n", type=int)
    a = ap.parse_args()
    files = [a.hard] if a.hard else sorted(a.run.glob("parts/*.hard.txt"))
    if a.run:
        t = summarize_run(a.run)
        print(f"checked by egcheck: parts {t['parts']}, trees {t['trees']:,}, records {t['records']:,}: "
              f"valid {t['records_valid']:,}, INVALID {t['records_invalid']}, empty {t['records_empty']}")
    pairs, unsolved = read_pairs(files)
    inside, outside = collections.Counter(), []
    n = None
    for f, idx, v, par in pairs:
        n = len(par) + 1
        fm = family_member(adjacency(par), v)
        if fm:
            inside[fm[2]] += 1
        else:
            outside.append((f, idx, v, par))
    print(f"impossible pairs: {len(pairs)}  (in family: {sum(inside.values())}, by L: {dict(inside)}; outside: {len(outside)})")
    if n:
        print(f"family size for n={n}: {family_size(n)} pairs")
    for f, idx, v, par in outside[:20]:
        print(f"  OUTSIDE FAMILY: {Path(f).name} tree {idx} vertex {v} par {par}")
    print(f"unsolved (budget exhausted, not decided): {len(unsolved)}")
    for f, idx, v, par in unsolved[:20]:
        fm = family_member(adjacency(par), v)
        print(f"  unsolved: {Path(f).name} tree {idx} vertex {v} family={fm}")


if __name__ == "__main__":
    main()
