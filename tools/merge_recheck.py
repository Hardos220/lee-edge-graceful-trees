#!/usr/bin/env python3
"""Merge a re-check run into the main run of the same order.

Used for n=31 parts 2800-3099: they were checked on another PC by the old
pipeline (egverify exited 0, i.e. all valid, but its JSON report was lost).
They were recomputed with  run_parts.py --out runs/n31_recheck .  A part is
merged only if the recomputed certificate has exactly the SHA-256 recorded
by the original run (the search is deterministic) and the independent
check accepted every tree; otherwise nothing is changed for that part.

  python tools/merge_recheck.py --n 31 --recheck runs/n31_recheck
"""
import argparse
import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, required=True)
    ap.add_argument("--recheck", type=Path, required=True)
    a = ap.parse_args()
    main_parts = ROOT / "runs" / f"n{a.n}" / "parts"
    merged = mismatched = 0
    for f in sorted((a.recheck / "parts").glob("*.done.json")):
        new = json.loads(f.read_text())
        orig_path = main_parts / f.name
        if not orig_path.exists():
            print(f"{f.name}: no original record, skipped")
            continue
        orig = json.loads(orig_path.read_text())
        v = new["verify"]
        ok = (v.get("returncode") == 0 and v.get("valid") == new["trees"] == orig["trees"]
              and new["cert_sha256"] == orig["cert_sha256"] == v.get("sha256"))
        if not ok:
            mismatched += 1
            print(f"{f.name}: MISMATCH orig sha {orig['cert_sha256'][:16]} new {new['cert_sha256'][:16] if new['cert_sha256'] else None} "
                  f"valid {v.get('valid')} trees {new['trees']}/{orig['trees']}")
            continue
        orig["verify_original"] = orig["verify"]
        orig["verify"] = v
        orig["rechecked"] = {"by": str(a.recheck), "sha256_identical": True}
        tmp = orig_path.with_suffix(".json.tmp")
        tmp.write_text(json.dumps(orig, indent=1))
        os.replace(tmp, orig_path)
        merged += 1
    print(f"merged {merged}, mismatched {mismatched}")
    sys.exit(1 if mismatched else 0)


if __name__ == "__main__":
    main()
