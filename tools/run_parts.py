#!/usr/bin/env python3
"""Run egrun over res/mod parts of one or more orders, check every part, aggregate.

Each part is one pair of concurrent processes:
    egrun N RES MOD --cert - --count C  |  egcheck N RES MOD - --expect-count C
egrun searches the labelings and streams the certificate; egcheck (independent
code, trees from gentreeg in-process) checks every record and its SHA-256
while the search runs.  The check costs a few percent of the search, so the
CPU load stays flat, and nothing is written to disk.  With --keep-cert all
the certificate is written to a file instead and checked from it.

All parts of all requested orders share one worker pool (no idle tail between
orders); the first workers start one by one over --ramp seconds (default 5 min).

Resumable: a part counts as done when runs/n<N>/parts/<part>.done.json exists.
Under rmq: RMQ_CPU workers by default, progress in RMQ_PROGRESS_FILE, clean
stop on RMQ_STOP_FILE (unfinished parts are redone on the next start).

Examples
  python tools/run_parts.py --n 21 --mod 20
  python tools/run_parts.py --spec 30:1500 --spec 31:4000:0:2800 --keep-cert none
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "bin"
EXE = ".exe" if os.name == "nt" else ""

# OEIS A000055: number of trees with n unlabeled vertices (also listed in gentreeg.c)
TREES = [1, 1, 1, 1, 2, 3, 6, 11, 23, 47, 106, 235, 551, 1301, 3159, 7741, 19320, 48629,
         123867, 317955, 823065, 2144505, 5623756, 14828074, 39299897, 104636890,
         279793450, 751065460, 2023443032, 5469566585, 14830871802, 40330829030,
         109972410221]

STOP = threading.Event()
ACTIVE = set()
ACTIVE_LOCK = threading.Lock()


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


def report_progress(done, total, note):
    path = os.environ.get("RMQ_PROGRESS_FILE")
    if path:
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write(f"{done}/{total} {note}")
        except OSError:
            pass


def stop_watcher():
    path = os.environ.get("RMQ_STOP_FILE")
    while not STOP.is_set():
        if path and os.path.exists(path):
            log("RMQ_STOP_FILE found: stopping, unfinished parts will be redone")
            STOP.set()
            with ACTIVE_LOCK:
                for p in list(ACTIVE):
                    try:
                        p.kill()
                    except OSError:
                        pass
            return
        time.sleep(2)


def spawn(args, **kw):
    p = subprocess.Popen(args, **kw)
    with ACTIVE_LOCK:
        ACTIVE.add(p)
    if STOP.is_set():
        p.kill()
    return p


def release(*procs):
    with ACTIVE_LOCK:
        for p in procs:
            ACTIVE.discard(p)


def part_name(res, mod):
    return f"part_{res:05d}_of_{mod:05d}"


def count_trees(n, res, mod):
    """Number of trees in part res/mod (gentreeg -u, ~10^8 trees/s)."""
    r = subprocess.run([str(BIN / f"gentreeg{EXE}"), "-u", str(n), f"{res}/{mod}"],
                       capture_output=True, text=True)
    m = re.search(r">Z (\d+) trees", r.stderr)
    if not m:
        raise RuntimeError(f"gentreeg -u failed for n={n} {res}/{mod}: {r.stderr}")
    return int(m.group(1))


def run_part(n, res, mod, outdir, keep_cert, egrun_extra, delay):
    """Search + independent check of one part.  Returns the done-record or None if stopped."""
    if delay > 0 and STOP.wait(delay):
        return None
    if STOP.is_set():
        return None
    pdir = outdir / "parts"
    base = pdir / part_name(res, mod)
    if base.with_suffix(".done.json").exists():     # finished meanwhile (e.g. returned from another PC)
        return "skipped"
    cert, summ, hard = (base.with_suffix(".cert"), base.with_suffix(".summary.json"),
                        base.with_suffix(".hard.txt"))
    t0 = time.time()
    count = count_trees(n, res, mod)
    egrun = [str(BIN / f"egrun{EXE}"), str(n), str(res), str(mod), "--count", str(count),
             "--summary", str(summ), "--hard", str(hard)] + egrun_extra
    if "--top" in egrun_extra and "--top-file" not in egrun_extra:
        egrun += ["--top-file", str(base.with_suffix(".top.txt"))]
    check = [str(BIN / f"egcheck{EXE}"), str(n), str(res), str(mod)]

    if keep_cert:
        p = spawn(egrun + ["--cert", str(cert)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        _, err = p.communicate()
        release(p)
        if STOP.is_set():
            return None
        t1 = time.time()
        c = spawn(check + [str(cert), "--expect-count", str(count)],
                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        cout, cerr = c.communicate()
        release(c)
    else:
        if cert.exists():
            cert.unlink()                     # leftover of the old file-based pipeline
        p = spawn(egrun + ["--cert", "-"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        c = spawn(check + ["-", "--expect-count", str(count)], stdin=p.stdout,
                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        p.stdout.close()                      # egcheck owns the read end now
        cout, cerr = c.communicate()
        err = p.stderr.read()
        p.wait()
        release(p, c)
        t1 = time.time()
    if STOP.is_set():
        return None
    if p.returncode != 0 and not summ.exists():
        raise RuntimeError(f"egrun failed on {base.name}: rc={p.returncode} {err.decode(errors='replace')}")
    summary = json.loads(summ.read_text())
    try:
        verify = json.loads(cout.decode("utf-8", errors="replace"))
    except ValueError:
        verify = {"error": (cout + b" | " + cerr).decode("utf-8", errors="replace")[:2000]}
        verify["unparsed"] = True                 # never counts as verified (see aggregate)
    verify["returncode"] = c.returncode
    t2 = time.time()

    record = {
        "part": base.name, "res": res, "mod": mod, "trees": summary["trees"], "count": count,
        "egrun_rc": p.returncode, "verify": verify,
        "cert_bytes": 64 + count * ((5 * (n - 1) + 7) // 8),
        "cert_sha256": verify.get("sha256"),
        "cert_stored": bool(keep_cert),
        "search_sec": summary["wall_sec"],
        "verify_sec": round(t2 - t1, 3) if keep_cert else 0.0,   # streamed: runs alongside the search
        "part_wall_sec": round(t2 - t0, 3),
    }
    if hard.exists() and hard.stat().st_size == 0:
        hard.unlink()
    tmp = base.with_suffix(".done.json.tmp")
    tmp.write_text(json.dumps(record, indent=1))
    os.replace(tmp, base.with_suffix(".done.json"))
    return record


def aggregate(n, mod, outdir):
    pdir = outdir / "parts"
    agg = {"n": n, "mod": mod, "parts_done": 0, "trees": 0, "solved": 0, "impossible": 0,
           "unsolved": 0, "solved_by_stage": [0, 0, 0], "work_total": 0, "work_max": 0,
           "verified_valid": 0, "verify_failures": [], "hist_log2_work": [], "by_deg2": {},
           "search_sec": 0.0, "verify_sec": 0.0, "cert_bytes": 0, "hardest": []}
    hashes = []
    for res in range(mod):
        base = pdir / part_name(res, mod)
        done = base.with_suffix(".done.json")
        if not done.exists():
            continue
        d = json.loads(done.read_text())
        s = json.loads(base.with_suffix(".summary.json").read_text())
        agg["parts_done"] += 1
        agg["mode"] = s["mode"]
        for k in ("trees", "solved", "impossible", "unsolved", "work_total"):
            agg[k] += s[k]
        agg["work_max"] = max(agg["work_max"], s["work_max"])
        agg["solved_by_stage"] = [a + b for a, b in zip(agg["solved_by_stage"], s["solved_by_stage"])]
        h = s["hist_log2_work"]
        agg["hist_log2_work"] += [0] * (len(h) - len(agg["hist_log2_work"]))
        for i, x in enumerate(h):
            agg["hist_log2_work"][i] += x
        for k, (c, w, m) in s["by_deg2"].items():
            a = agg["by_deg2"].setdefault(k, [0, 0, 0])
            a[0] += c; a[1] += w; a[2] = max(a[2], m)
        agg["hardest"].append((s["work_max"], d["part"], s["hardest_par"]))
        agg["verified_valid"] += d["verify"].get("valid", 0)
        if d["verify"].get("returncode") != 0:
            agg["verify_failures"].append(d["part"])
        agg["search_sec"] += d["search_sec"]
        agg["verify_sec"] += d["verify_sec"]
        agg["cert_bytes"] += d["cert_bytes"]
        hashes.append(f"{d['cert_sha256']}  {d['part']}.cert  trees={d['trees']}")
    agg["hardest"] = sorted(agg["hardest"], reverse=True)[:10]
    agg["expected_trees"] = TREES[n] if n < len(TREES) else None
    agg["complete"] = agg["parts_done"] == mod
    agg["count_matches_A000055"] = agg["complete"] and agg["trees"] == agg["expected_trees"]
    agg["all_verified"] = (agg["complete"] and not agg["verify_failures"]
                           and agg["verified_valid"] == agg["trees"])
    agg["claim_holds"] = (agg["count_matches_A000055"] and agg["all_verified"]
                          and agg["unsolved"] == 0 and agg["impossible"] == 0)
    (outdir / "SUMMARY.json").write_text(json.dumps(agg, indent=1))
    (outdir / "cert_sha256.txt").write_text("\n".join(hashes) + "\n")
    bad = []
    for res in range(mod):
        hard = pdir / (part_name(res, mod) + ".hard.txt")
        if hard.exists():
            for line in hard.read_text().splitlines():
                if " solved " not in line:
                    bad.append(f"{part_name(res, mod)} {line}")
    (outdir / "unsolved.txt").write_text("\n".join(bad) + ("\n" if bad else ""))
    return agg


def parse_spec(text):
    """N:MOD or N:MOD:A:B (parts A..B-1)."""
    f = [int(x) for x in text.split(":")]
    if len(f) == 2:
        return f[0], f[1], 0, f[1]
    if len(f) == 4:
        return tuple(f)
    raise argparse.ArgumentTypeError("spec must be N:MOD or N:MOD:A:B")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--spec", type=parse_spec, action="append", default=[],
                    help="N:MOD[:A:B], may be repeated; parts of all specs share the workers")
    ap.add_argument("--n", type=int, help="shorthand for --spec N:MOD[:A:B] with --mod/--parts")
    ap.add_argument("--mod", type=int)
    ap.add_argument("--parts", default=None, help="with --n: only res values a:b")
    ap.add_argument("--jobs", type=int, default=int(os.environ.get("RMQ_CPU", os.cpu_count() or 1)))
    ap.add_argument("--out", type=Path, default=None, help="output dir (single order only), default runs/n<N>")
    ap.add_argument("--keep-cert", choices=["all", "none"], default="none",
                    help="'all' writes certificates to files and keeps them; 'none' streams them")
    ap.add_argument("--ramp", type=float, default=300.0,
                    help="seconds over which the first workers are started one by one; a slow start "
                         "keeps the laptop from a turbo heat spike that makes the rmq thermal "
                         "regulator throttle and release in swings")
    ap.add_argument("--egrun-args", default="", help="extra arguments for egrun")
    ap.add_argument("--tag", default="", help="output to runs/<tag>_n<N> instead of runs/n<N> (e.g. zr)")
    a = ap.parse_args()

    specs = list(a.spec)
    if a.n is not None:
        if a.mod is None:
            ap.error("--n needs --mod")
        lo, hi = (0, a.mod) if not a.parts else map(int, a.parts.split(":"))
        specs.append((a.n, a.mod, lo, hi))
    if not specs:
        ap.error("give --spec or --n/--mod")
    if a.out and len({s[0] for s in specs}) > 1:
        ap.error("--out works with a single order only")

    prefix = f"{a.tag}_" if a.tag else ""
    outdirs = {n: (a.out or ROOT / "runs" / f"{prefix}n{n}") for n, _, _, _ in specs}
    todo, total = [], 0
    for n, mod, lo, hi in specs:
        (outdirs[n] / "parts").mkdir(parents=True, exist_ok=True)
        total += hi - lo
        todo += [(n, mod, r) for r in range(lo, hi)
                 if not (outdirs[n] / "parts" / (part_name(r, mod) + ".done.json")).exists()]
    done = total - len(todo)
    log(f"specs {specs}: {done}/{total} parts already done, {len(todo)} to run, jobs={a.jobs}")
    report_progress(done, total, "parts")

    threading.Thread(target=stop_watcher, daemon=True).start()
    extra = a.egrun_args.split()
    t0 = time.time()
    failures = 0
    with ThreadPoolExecutor(max_workers=a.jobs) as pool:
        futs = {}
        for k, (n, mod, r) in enumerate(todo):
            delay = a.ramp * k / a.jobs if k < a.jobs else 0.0
            futs[pool.submit(run_part, n, r, mod, outdirs[n], a.keep_cert == "all", extra, delay)] = (n, r)
        for fut in as_completed(futs):
            try:
                rec = fut.result()
            except Exception as exc:          # keep going, report at the end
                failures += 1
                log(f"part n={futs[fut][0]} res={futs[fut][1]} FAILED: {exc}")
                continue
            if rec is None:
                continue
            done += 1
            if rec == "skipped":
                continue
            v = rec["verify"]
            flag = "" if v.get("returncode") == 0 else "  <-- CHECK FAILED"
            if done % max(1, total // 200) == 0 or flag or done == total:
                log(f"{done}/{total} parts, last n={futs[fut][0]} {rec['part']}: trees={rec['trees']} "
                    f"wall={rec['part_wall_sec']}s elapsed={time.time() - t0:.0f}s{flag}")
            report_progress(done, total, f"parts (n={futs[fut][0]})")
            if STOP.is_set():
                break
        if STOP.is_set():
            pool.shutdown(wait=True, cancel_futures=True)
    STOP.set()

    bad = failures
    for n, mod in sorted({(s[0], s[1]) for s in specs}):
        agg = aggregate(n, mod, outdirs[n])
        log(f"n={n}: parts done {agg['parts_done']}/{mod}; trees={agg['trees']} "
            f"(A000055: {agg['expected_trees']}); solved={agg['solved']} unsolved={agg['unsolved']} "
            f"impossible={agg['impossible']}; verified={agg['verified_valid']} "
            f"failures={len(agg['verify_failures'])}; claim_holds={agg['claim_holds']}")
        bad += len(agg["verify_failures"]) + agg["unsolved"] + agg["impossible"]
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
