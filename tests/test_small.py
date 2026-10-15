#!/usr/bin/env python3
"""Independent checks of the pipeline on small orders (runs in ~1 minute).

1. gentreeg emits every tree exactly once: canonical forms (AHU encoding about
   the centre) are pairwise distinct and their number equals A000055(n) and
   the networkx count, n <= 14.
2. Brute force over all (n-1)! labelings, n <= 9: every tree of odd order has
   an edge-graceful labeling, no tree of even order has one, and every tree
   except K2 has an antimagic labeling.  The number of labelings is printed
   for reference.
3. egrun + egverify on all trees, n <= 17 (odd: EG, even: AM).
4. egrun in EG mode proves impossibility for every tree of even order <= 10.
5. egverify and egcheck reject corrupted certificates.
7. Streaming (egrun --cert - | egcheck -) gives exactly the file bytes; the
   SHA-256 reported by the checkers equals Python hashlib.
6. egrun output is deterministic (identical SHA-256 on a rerun).
8. Mode zr (residue 0 at each internal vertex): impossible pairs equal brute
   force for n <= 9 and form the double-star family (2n-7 pairs); the checker
   rejects a tampered zr record.
9. Hardness mode (--repeat/--top) produces a sorted top list.
"""
import hashlib
import itertools
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "bin"
EXE = ".exe" if os.name == "nt" else ""
GENTREEG, EGRUN, EGVERIFY, EGCHECK = (str(BIN / f"{x}{EXE}") for x in ("gentreeg", "egrun", "egverify", "egcheck"))
A000055 = [1, 1, 1, 1, 2, 3, 6, 11, 23, 47, 106, 235, 551, 1301, 3159, 7741, 19320, 48629]

failures = []


def check(cond, msg):
    print(("  ok   " if cond else "  FAIL ") + msg, flush=True)
    if not cond:
        failures.append(msg)


def trees(n, part="0/1"):
    out = subprocess.run([GENTREEG, "-p", "-q", str(n), part], capture_output=True, text=True).stdout
    return [list(map(int, line.split())) for line in out.splitlines() if line.strip()]


def edges_of(par):            # gentreeg parent array, 1-based -> 0-based edge list
    return [(j - 1, par[j - 1] - 1) for j in range(2, len(par) + 1)]


def canon(n, edges):
    """AHU canonical string of a free tree (minimum over its centres)."""
    adj = [[] for _ in range(n)]
    for a, b in edges:
        adj[a].append(b)
        adj[b].append(a)
    deg = [len(x) for x in adj]
    layer = [v for v in range(n) if deg[v] <= 1]
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
    centres = layer if n > 1 else [0]

    def enc(v, p):
        return "(" + "".join(sorted(enc(w, v) for w in adj[v] if w != p)) + ")"
    return min(enc(c, -1) for c in centres)


def count_labelings(n, edges):
    eg = am = 0
    for perm in itertools.permutations(range(1, n)):
        s = [0] * n
        for (a, b), l in zip(edges, perm):
            s[a] += l
            s[b] += l
        if len(set(s)) == n:
            am += 1
            if len({x % n for x in s}) == n:
                eg += 1
    return eg, am


def run_egrun(n, extra, tmp, part=(0, 1)):
    cert = Path(tmp) / f"t{n}_{part[0]}.cert"
    summ = Path(tmp) / f"t{n}_{part[0]}.json"
    subprocess.run([EGRUN, str(n), str(part[0]), str(part[1]), "--cert", str(cert),
                    "--summary", str(summ)] + extra, capture_output=True)
    return cert, json.loads(summ.read_text())


def verify(n, cert, part="0/1"):
    g = subprocess.Popen([GENTREEG, "-p", "-q", str(n), part], stdout=subprocess.PIPE)
    r = subprocess.run([EGVERIFY, str(cert)], stdin=g.stdout, capture_output=True, text=True)
    g.stdout.close()
    g.wait()
    return r.returncode, (json.loads(r.stdout) if r.stdout.strip() else {})


def main():
    import networkx as nx

    print("1. gentreeg completeness and uniqueness")
    for n in range(1, 15):
        ts = trees(n)
        forms = {canon(n, edges_of(p)) for p in ts}
        nxc = sum(1 for _ in nx.nonisomorphic_trees(n)) if n >= 2 else 1
        check(len(ts) == len(forms) == A000055[n] == nxc,
              f"n={n}: {len(ts)} trees, {len(forms)} distinct, A000055={A000055[n]}, networkx={nxc}")

    print("2. brute force over all labelings")
    for n in range(2, 10):
        res = [count_labelings(n, edges_of(p)) for p in trees(n)]
        n_eg = sum(1 for eg, _ in res if eg > 0)
        n_am = sum(1 for _, am in res if am > 0)
        if n % 2:
            check(n_eg == len(res), f"n={n}: {n_eg}/{len(res)} trees edge-graceful; "
                                    f"min #EG labelings {min(r[0] for r in res)}")
        else:
            check(n_eg == 0, f"n={n}: no tree of even order is edge-graceful")
        check(n_am == len(res) - (n == 2), f"n={n}: {n_am}/{len(res)} trees antimagic"
              + (" (K2 excluded)" if n == 2 else ""))

    with tempfile.TemporaryDirectory() as tmp:
        print("3. egrun + egverify on all trees")
        for n in range(3, 18):
            cert, s = run_egrun(n, [], tmp)
            rc, v = verify(n, cert)
            check(rc == 0 and s["solved"] == s["trees"] == v["valid"] == A000055[n],
                  f"n={n} ({s['mode']}): solved {s['solved']}, verified {v.get('valid')}")

        print("4. impossibility proofs (EG on even order)")
        for n in (4, 6, 8, 10):
            _, s = run_egrun(n, ["--mode", "eg", "--s2-tries", "0"], tmp)
            check(s["impossible"] == s["trees"] == A000055[n], f"n={n}: {s['impossible']} proved impossible")

        print("5. corrupted certificates are rejected")
        n = 15
        cert, s = run_egrun(n, [], tmp)
        rb = (5 * (n - 1) + 7) // 8
        data = bytearray(cert.read_bytes())

        def unpack(rec):
            x = int.from_bytes(rec, "little")
            return [(x >> (5 * i)) & 31 for i in range(n - 1)]

        def pack(lab):
            return sum(v << (5 * i) for i, v in enumerate(lab)).to_bytes(rb, "little")

        def variant(name, d):
            p = Path(tmp) / f"bad_{name}.cert"
            p.write_bytes(d)
            rc, _ = verify(n, p)
            rc2 = subprocess.run([EGCHECK, str(n), "0", "1", str(p)], capture_output=True).returncode
            check(rc != 0 and rc2 != 0, f"rejects: {name} (egverify rc={rc}, egcheck rc={rc2})")

        d = bytearray(data); d[64 + 100 * rb] ^= 1; variant("bit flip", d)
        d = bytearray(data); r = unpack(d[64 + 7 * rb:64 + 8 * rb]); r[0], r[1] = r[1], r[0]
        d[64 + 7 * rb:64 + 8 * rb] = pack(r); variant("two labels swapped", d)
        d = bytearray(data); r = unpack(d[64:64 + rb]); r[0] = r[1]; d[64:64 + rb] = pack(r)
        variant("repeated label", d)
        variant("truncated", data[:-rb])
        variant("trailing record", data + data[-rb:])
        d = bytearray(data); d[64 + 3 * rb:64 + 4 * rb] = bytes(rb); variant("empty record", d)
        rc, _ = verify(n, cert)
        check(rc == 0, "accepts the original certificate")

        print("6. determinism")
        c1, _ = run_egrun(23, [], tmp, part=(5, 400))
        h1 = hashlib.sha256(c1.read_bytes()).hexdigest()
        c2, _ = run_egrun(23, [], tmp, part=(5, 400))
        check(h1 == hashlib.sha256(c2.read_bytes()).hexdigest(), "identical certificates on rerun")

        print("7. streaming == file, SHA-256")
        n, part = 25, (3, 500)
        cert, s = run_egrun(n, [], tmp, part=part)
        data = cert.read_bytes()
        cnt = ["--count", str(s["trees"])]
        r = subprocess.run([EGRUN, str(n), str(part[0]), str(part[1]), "--cert", "-"] + cnt
                           + ["--summary", str(Path(tmp) / "st.json")], capture_output=True)
        check(r.stdout == data, f"stream bytes == file bytes ({len(data)} bytes)")
        g = subprocess.Popen([EGRUN, str(n), str(part[0]), str(part[1]), "--cert", "-"] + cnt
                             + ["--summary", str(Path(tmp) / "st2.json")], stdout=subprocess.PIPE)
        c = subprocess.run([EGCHECK, str(n), str(part[0]), str(part[1]), "-", "--expect-count", str(s["trees"])],
                           stdin=g.stdout, capture_output=True, text=True)
        g.stdout.close()
        g.wait()
        v = json.loads(c.stdout)
        check(c.returncode == 0 and v["valid"] == s["trees"], f"egcheck on the stream: {v['valid']} valid")
        _, v2 = verify(n, cert, part=f"{part[0]}/{part[1]}")
        check(v["sha256"] == hashlib.sha256(data).hexdigest() == v2["sha256"],
              "SHA-256: egcheck == egverify == hashlib")

        print("8. mode zr: residue 0 at a prescribed internal vertex")
        sys.path.insert(0, str(ROOT / "tools"))
        from zr_analyze import family_member, adjacency, family_size
        for n in (5, 7, 9):
            brute = set()
            for idx, p1 in enumerate(trees(n)):
                par = [x - 1 for x in p1]
                edges = edges_of(p1)
                deg = [0] * n
                for a, b in edges:
                    deg[a] += 1
                    deg[b] += 1
                zero_ok = set()
                for perm in itertools.permutations(range(1, n)):
                    s_ = [0] * n
                    for (a, b), l in zip(edges, perm):
                        s_[a] += l
                        s_[b] += l
                    r_ = [x % n for x in s_]
                    if len(set(r_)) == n:
                        zero_ok.add(r_.index(0))
                brute |= {(idx, v) for v in range(n) if deg[v] >= 2 and v not in zero_ok}
            hard = Path(tmp) / f"zr{n}.hard"
            cert, s = run_egrun(n, ["--mode", "zr", "--hard", str(hard)], tmp)
            prog = set()
            for line in hard.read_text().splitlines():
                if "IMPOSSIBLE" in line:
                    f = line.split()
                    prog.add((int(f[0]), int(f[5][2:])))
            check(brute == prog, f"n={n}: impossible (tree, vertex) pairs: brute force {len(brute)} == program {len(prog)}")
            rc, v = verify(n, cert)
            check(v.get("records_invalid") == 0 and v.get("records_empty") == len(brute),
                  f"n={n}: checker: {v.get('records_valid')} valid zero-vertex records, {v.get('records_empty')} empty")
            fam = sum(1 for idx, vv in brute if family_member(adjacency([x - 1 for x in trees(n)[idx][1:]]), vv))
            check(fam == family_size(n) == 2 * n - 7, f"n={n}: double-star family {fam} pairs (2n-7 = {2 * n - 7})")
        n = 11
        cert, s = run_egrun(n, ["--mode", "zr"], tmp)
        data = bytearray(cert.read_bytes())
        rb = (5 * (n - 1) + 7) // 8
        # move the zero to another vertex: swap the labels of two edges in a solved record
        k = next(i for i in range((len(data) - 64) // rb) if any(data[64 + i * rb:64 + (i + 1) * rb]))
        rec = int.from_bytes(data[64 + k * rb:64 + (k + 1) * rb], "little")
        labs = [(rec >> (5 * i)) & 31 for i in range(n - 1)]
        labs[0], labs[-1] = labs[-1], labs[0]
        data[64 + k * rb:64 + (k + 1) * rb] = sum(x << (5 * i) for i, x in enumerate(labs)).to_bytes(rb, "little")
        bad = Path(tmp) / "zr_bad.cert"
        bad.write_bytes(data)
        rc, v = verify(n, bad)
        check(v.get("records_invalid", 0) >= 1, "rejects a zr record with two labels swapped")

        print("9. hardness mode (--repeat/--top)")
        top = Path(tmp) / "top.txt"
        _, s = run_egrun(17, ["--repeat", "5", "--top", "10", "--top-file", str(top)], tmp)
        lines = top.read_text().splitlines()
        means = [float(x.split()[0]) for x in lines]
        check(len(lines) == 10 and means == sorted(means, reverse=True) and s.get("repeat") == 5,
              f"top-10 hardest trees of order 17 by mean work: {means[0]:.1f} ... {means[-1]:.1f}")

    print(f"\n{'ALL TESTS PASSED' if not failures else f'{len(failures)} FAILURE(S)'}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
