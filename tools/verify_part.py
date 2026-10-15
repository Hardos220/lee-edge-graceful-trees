#!/usr/bin/env python3
"""Regenerate the certificate of one part and check it against the deposited hash.

The search is deterministic, so the certificate of any part can be rebuilt bit
for bit and handed to the independent checker:

    egrun N RES MOD [--mode zr] --cert - --count C  |  egcheck N RES MOD - --expect-count C

The part is looked up in runs/nN (edge-graceful or antimagic runs) or, with
--zero, in runs/zr_nN (zero-vertex runs); its number of trees and SHA-256 are
taken from the deposited parts/part_RES_of_MOD.done.json.

usage: python tools/verify_part.py N RES [--zero]
example: python tools/verify_part.py 21 5        (order 21, part 5/20)
         python tools/verify_part.py 19 0 --zero

Exit status 0 if the checker accepts every record (for --zero: no invalid
record; the empty records are the pairs without a labeling) and the SHA-256
equals the deposited one.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = ".exe" if os.name == "nt" else ""
BIN = ROOT / "bin"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("n", type=int)
    ap.add_argument("res", type=int)
    ap.add_argument("--zero", action="store_true", help="zero-vertex run (runs/zr_nN)")
    a = ap.parse_args()

    run = ROOT / "runs" / (f"zr_n{a.n}" if a.zero else f"n{a.n}")
    found = sorted(run.glob(f"parts/part_{a.res:05d}_of_*.done.json"))
    if not found:
        sys.exit(f"no deposited part {a.res} in {run}")
    rec = json.loads(found[0].read_text())
    mod, count, sha = rec["mod"], rec["trees"], rec["cert_sha256"]

    with tempfile.TemporaryDirectory() as tmp:
        # --summary and --hard go to files: without them egrun prints its summary on stdout,
        # which here carries the certificate
        egrun = [str(BIN / f"egrun{EXE}"), str(a.n), str(a.res), str(mod), "--count", str(count),
                 "--summary", str(Path(tmp) / "summary.json"), "--hard", str(Path(tmp) / "hard.txt")]
        if a.zero:
            egrun += ["--mode", "zr"]
        egrun += ["--cert", "-"]
        check = [str(BIN / f"egcheck{EXE}"), str(a.n), str(a.res), str(mod), "-", "--expect-count", str(count)]
        print(f"egrun {a.n} {a.res} {mod}{' --mode zr' if a.zero else ''} --cert - | egcheck {a.n} {a.res} {mod} -",
              flush=True)
        p = subprocess.Popen(egrun, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        c = subprocess.run(check, stdin=p.stdout, capture_output=True, text=True)
        p.stdout.close()
        p.wait()
    verdict = json.loads(c.stdout.strip().splitlines()[-1])

    if a.zero:                                 # one record per internal vertex
        valid, invalid, empty = verdict["records_valid"], verdict["records_invalid"], verdict["records_empty"]
        accepts = invalid == 0
    else:                                      # one record per tree
        valid, invalid, empty = verdict["valid"], verdict["invalid"], verdict["unsolved"]
        accepts = c.returncode == 0
    same = verdict.get("sha256") == sha
    print(json.dumps({"n": a.n, "part": f"{a.res}/{mod}", "trees": count, "records_valid": valid,
                      "records_invalid": invalid, "records_empty": empty, "checker_accepts": accepts,
                      "sha256": verdict.get("sha256"), "sha256_deposited": sha, "sha256_identical": same},
                     indent=1))
    sys.exit(0 if accepts and same else 1)


if __name__ == "__main__":
    main()
