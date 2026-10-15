# Edge-graceful and antimagic labelings of all trees with at most 31 vertices

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.23405211.svg)](https://doi.org/10.5281/zenodo.23405211)

Code and data for the paper *Lee's edge-graceful tree conjecture for all trees with at most 31 vertices, and the zero vertex of an edge-graceful labeling* by Maksim Elisov (Samara National Research University) and Aleksandr Petrov (Lomonosov Moscow State University).

## Results

A labeling of a tree with n vertices assigns the labels 1, …, n−1 bijectively to the edges. The sum at a vertex is the sum of the labels of its edges. The labeling is *edge-graceful* if these sums are pairwise distinct modulo n (Lo 1985). It is *antimagic* if they are pairwise distinct as integers (Hartsfield and Ringel 1990).

- **Lee's conjecture for n ≤ 31.** Every tree of odd order n ≤ 31 is edge-graceful. This covers 46,673,446,467 trees.
- **Hartsfield–Ringel conjecture for trees with 3 ≤ n ≤ 31.** Every such tree is antimagic. This covers 63,853,448,503 trees. Edge-graceful labelings are antimagic, so for even n an antimagic labeling was searched directly.
- **Zero vertex.** In an edge-graceful labeling exactly one vertex has sum ≡ 0 (mod n). For odd n ≤ 27, every internal vertex v of every tree T is the zero vertex of some edge-graceful labeling of T, with these exceptions:
  - the ends of the double brooms D(k,m,2) and D(k,m,3), which can never be the zero vertex (2n − 7 pairs; Proposition 1.5 of the paper);
  - for n = 9, the two ends of D(2,2,4). For spine length 4 this is the only exception (Proposition 5.1).

  The double broom D(k,m,L) is a path c₀c₁…c_L (the spine) with k ≥ 1 leaves attached to c₀ and m ≥ 0 leaves attached to c_L. Its ends are c₀ and, if m ≥ 1, c_L.

Every labeling was written to a certificate and accepted by an independent checker. The number of trees of every order equals OEIS [A000055](https://oeis.org/A000055).

| n | trees | labeling | | n | trees | labeling |
|---:|---:|---|---|---:|---:|---|
| 3 | 1 | edge-graceful | | 18 | 123,867 | antimagic |
| 4 | 2 | antimagic | | 19 | 317,955 | edge-graceful |
| 5 | 3 | edge-graceful | | 20 | 823,065 | antimagic |
| 6 | 6 | antimagic | | 21 | 2,144,505 | edge-graceful |
| 7 | 11 | edge-graceful | | 22 | 5,623,756 | antimagic |
| 8 | 23 | antimagic | | 23 | 14,828,074 | edge-graceful |
| 9 | 47 | edge-graceful | | 24 | 39,299,897 | antimagic |
| 10 | 106 | antimagic | | 25 | 104,636,890 | edge-graceful |
| 11 | 235 | edge-graceful | | 26 | 279,793,450 | antimagic |
| 12 | 551 | antimagic | | 27 | 751,065,460 | edge-graceful |
| 13 | 1,301 | edge-graceful | | 28 | 2,023,443,032 | antimagic |
| 14 | 3,159 | antimagic | | 29 | 5,469,566,585 | edge-graceful |
| 15 | 7,741 | edge-graceful | | 30 | 14,830,871,802 | antimagic |
| 16 | 19,320 | antimagic | | 31 | 40,330,829,030 | edge-graceful |
| 17 | 48,629 | edge-graceful | | | | |

Zero-vertex runs: pairs (T, v) with v an internal vertex of a fixed copy of T. Automorphic vertices are counted separately; the column orbits counts the pairs up to automorphisms of T, which is r(n) − r(n−1) with r = OEIS A000081 (rooted trees).

| n | trees | pairs (T, v) | orbits | with a labeling | Prop. 1.5 | Prop. 5.1 (n = 9) |
|---:|---:|---:|---:|---:|---:|---:|
| 3 | 1 | 1 | 1 | 1 | 0 | 0 |
| 5 | 3 | 6 | 5 | 3 | 3 | 0 |
| 7 | 11 | 34 | 28 | 27 | 7 | 0 |
| 9 | 47 | 197 | 171 | 184 | 11 | 2 |
| 11 | 235 | 1,252 | 1,123 | 1,237 | 15 | 0 |
| 13 | 1,301 | 8,413 | 7,720 | 8,394 | 19 | 0 |
| 15 | 7,741 | 58,857 | 54,838 | 58,834 | 23 | 0 |
| 17 | 48,629 | 424,932 | 399,466 | 424,905 | 27 | 0 |
| 19 | 317,955 | 3,138,546 | 2,967,517 | 3,138,515 | 31 | 0 |
| 21 | 2,144,505 | 23,593,963 | 22,395,604 | 23,593,928 | 35 | 0 |
| 23 | 14,828,074 | 179,890,065 | 171,227,674 | 179,890,026 | 39 | 0 |
| 25 | 104,636,890 | 1,387,521,506 | 1,323,449,661 | 1,387,521,463 | 43 | 0 |
| 27 | 751,065,460 | 10,806,459,151 | 10,324,097,819 | 10,806,459,104 | 47 | 0 |

## Contents

| path | content |
|---|---|
| `src/egsearch.c`, `src/egsearch.h` | search for edge-graceful, antimagic and zero-vertex labelings (local search and depth-first search) |
| `src/egrun.c` | `egrun`: `gentreeg` with the search linked in through its `OUTPROC` hook; writes the certificate |
| `src/egverify.c` | the independent checker, built twice: `egcheck` (generates the trees in-process with `gentreeg`) and `egverify` (reads the text output of `gentreeg -p`) |
| `src/egsolve.c` | solves given trees again with other seeds |
| `src/zrcount.c` | exhaustive count of edge-graceful labelings with each zero vertex (small n) |
| `tools/run_parts.py` | driver: runs `egrun \| egcheck` over the parts `res/mod` of an order and aggregates the results |
| `tools/verify_part.py` | checks the certificate of one part (deposited for n ≤ 21, regenerated otherwise) and compares it with its hash |
| `tools/check_uniqueness.py` | independent check, in Python, that `gentreeg` lists no tree twice |
| `tools/py_recheck.py` | third checker, in Python: regenerates certificates and checks every record |
| `tools/merge_recheck.py` | merges the recheck of parts 2800–3099 of order 31 (`runs/n31_recheck`) into `runs/n31` |
| `tools/zr_analyze.py` | compares the zero-vertex exceptions with the double-star family |
| `tools/zr_counts.py` | runs `zrcount` for odd n ≤ 11 and checks the counts |
| `tools/hardness_regression.py` | regression of the search effort on the number of leaves, of degree-two vertices and the diameter (sample of order 27) |
| `tools/make_paper_tables.py` | produces every number and table of the paper from `runs/` |
| `tests/test_small.py` | tests on small orders, including brute force over all labelings for n ≤ 9 |
| `third_party/fetch_nauty.sh` | downloads nauty 2.9.3 and checks its SHA-256 |
| `runs/` | data of all runs (see below) |

## Building

You need a C11 compiler (clang or gcc), bash and curl. The Python scripts need Python 3.9 or newer; the tests also need `networkx`.

    bash build.sh              # or: CC=gcc bash build.sh

On the first run `build.sh` downloads nauty 2.9.3 into `third_party/`. The programs use `gentreeg.c` and the nauty library sources unchanged. The binaries go to `bin/`.

## Checking the results

The certificates of all trees with at most 21 vertices are deposited in `runs/nN/parts/*.cert` and can be checked without running the search. For larger orders the certificates are not stored. The search is deterministic, because the random generator is seeded from n, the part and the index of the tree, so every certificate can be regenerated bit for bit.

    python tools/verify_part.py 21 5           # order 21, part 5 of 20: deposited certificate
    python tools/verify_part.py 31 2900        # order 31, part 2900 of 4000: regenerated
    python tools/verify_part.py 19 1 --zero    # zero-vertex run, order 19, part 1 of 2: regenerated

For a deposited certificate the script runs `egcheck N RES MOD FILE`. Otherwise it pipes `egrun N RES MOD --cert -` into `egcheck N RES MOD -`; `--regenerate` forces this also for n ≤ 21. It reports the checker's verdict and whether the SHA-256 of the certificate equals the deposited hash. Equal hashes show that the certificate is the original one; that its labelings are correct is shown by the checker. In our tests, builds with `-O1` and with `-O3 -march=x86-64-v3` gave identical certificates.

To redo a whole order, or the zero-vertex run of an order, into a separate directory:

    python tools/run_parts.py --spec 21:20 --tag rerun                           # -> runs/rerun_n21
    python tools/run_parts.py --spec 21:8 --tag rerun_zr --egrun-args="--mode zr"

The second number is the number of parts. The paper uses 1 part for n ≤ 20 and 20, 16, 32, 32, 50, 50, 200, 200, 1000, 1500 and 4000 parts for n = 21, …, 31. Zero-vertex runs use 1 part for n ≤ 17 and 2, 8, 40, 120 and 800 parts for n = 19, 21, 23, 25 and 27.

Independent checks:

    python tools/check_uniqueness.py --nmax 20
    python tools/py_recheck.py --plan runs/checks/recheck_plan.json --out recheck.json
    python tools/zr_counts.py --orders 5 7 9 11 --out zr_counts.json
    python tools/hardness_regression.py --n 27 --sample 20000 --repeat 5 --out regression.json
    python tests/test_small.py

## Data (`runs/`)

| path | content |
|---|---|
| `runs/nN/SUMMARY.json` | order N: number of trees, solved, verified, effort statistics |
| `runs/nN/cert_sha256.txt` | SHA-256 and number of trees of the certificate of every part |
| `runs/nN/parts/*.done.json` | per part: number of trees, the checker's verdict, SHA-256 |
| `runs/nN/parts/*.summary.json` | per part: search statistics (iterations, stages) |
| `runs/nN/parts/*.cert` | the certificates, for n ≤ 21 |
| `runs/zr_nN/` | the same for the zero-vertex run of order N. `parts/*.hard.txt` lists the pairs without a certificate, all of which are exceptions in the theorem |
| `runs/n31_recheck/` | second computation of parts 2800–3099 of order 31 with the streaming checker; all 300 hashes agree with the first one |
| `runs/checks/uniqueness.json` | independent uniqueness check of the generated trees, n ≤ 26 |
| `runs/checks/recheck.json` | Python recheck of one part of every order 15 ≤ n ≤ 31 |
| `runs/checks/zr_counts.json` | exhaustive zero-vertex counts, odd n ≤ 11 |
| `runs/checks/hardness_regression.json` | regression of the search effort, sample of order 27 |

The certificates for n ≥ 22 (about 1.2 TB in total) are not stored. Regenerate them with `tools/verify_part.py` or `egrun`.

### Certificate format

A certificate starts with a 64-byte little-endian header: `"EGCERT1\0"`, n, mode, res, mod, number of trees, bytes per record, flags. Then follows one record per tree, in the order of `gentreeg -p n res/mod`. A record holds the labels of the edges {j, par[j]}, j = 2, …, n, in gentreeg's 1-based numbering, 5 bits each, least significant bit first.

| mode | records |
|---:|---|
| 1 | edge-graceful labelings |
| 2 | antimagic labelings |
| 3 | zero-vertex runs: one record per internal vertex, with residue 0 at that vertex; an all-zero record means that no labeling was found |

## License

- Code: MIT License (`LICENSE`).
- Data in `runs/`: [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
- nauty (Brendan McKay and Adolfo Piperno) is distributed under the Apache License 2.0. It is not part of this repository and is downloaded at build time.

## Citation

See `CITATION.cff`. The repository is archived on Zenodo under the DOI [10.5281/zenodo.23405211](https://doi.org/10.5281/zenodo.23405211), which always resolves to the latest version; every release also has its own DOI.
