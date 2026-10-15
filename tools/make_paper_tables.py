#!/usr/bin/env python3
"""Generates the numbers and tables of the paper from the run data.

Writes paper/generated/{numbers.tex, tab_main.tex, tab_deg2.tex, tab_zero.tex}.
Every number quoted in paper/main.tex comes from here, so rerun this script
(and recompile) whenever runs/ changes.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from zr_analyze import adjacency, family_member, family_size, read_pairs, summarize_run  # noqa: E402

OUT = ROOT / "paper" / "generated"
A000055 = [1, 1, 1, 1, 2, 3, 6, 11, 23, 47, 106, 235, 551, 1301, 3159, 7741, 19320, 48629,
           123867, 317955, 823065, 2144505, 5623756, 14828074, 39299897, 104636890,
           279793450, 751065460, 2023443032, 5469566585, 14830871802, 40330829030]
WORDS = {3: "Three", 5: "Five", 9: "Nine", 21: "TwentyOne", 25: "TwentyFive", 27: "TwentySeven",
         29: "TwentyNine", 30: "Thirty", 31: "ThirtyOne"}


def fmt(x):
    """12345678 -> 12\\,345\\,678"""
    s = f"{x:,}"
    return s.replace(",", "\\,")


def sci(x, digits=2):
    m, e = f"{x:.{digits}e}".split("e")
    return f"{m}\\times 10^{{{int(e)}}}"


def write_table(name, spec, header, rows):
    """A complete tabular: an \\input inside a tabular breaks once endfloat moves the table."""
    body = [r"\begin{tabular}{" + spec + "}", r"\toprule", header, r"\midrule"] + rows + [r"\bottomrule", r"\end{tabular}"]
    (OUT / name).write_text("\n".join(body) + "\n", encoding="utf-8")


def load_main():
    rows = {}
    for d in ROOT.glob("runs/n*"):
        m = re.fullmatch(r"n(\d+)", d.name)
        if m and (d / "SUMMARY.json").exists():
            rows[int(m.group(1))] = json.loads((d / "SUMMARY.json").read_text())
    return dict(sorted(rows.items()))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    main_rows = load_main()
    macros = {}

    # ---- Table 1: verification of both conjectures
    lines = []
    for n, s in main_rows.items():
        if not (3 <= n <= 31):
            continue
        assert s["claim_holds"] and s["trees"] == A000055[n], f"n={n} not complete"
        prop = "EG" if s["mode"] == "eg" else "AM"
        mean = s["work_total"] / s["trees"]
        lines.append(f"{n} & {prop} & {fmt(s['trees'])} & {mean:.1f} & {fmt(s['work_max'])} \\\\")
    write_table("tab_main.tex", "rlrrr", r"$n$ & type & trees & mean work & max work \\", lines)

    odd = [n for n in main_rows if 3 <= n <= 31 and n % 2 == 1]
    allv = [n for n in main_rows if 3 <= n <= 31]
    macros["NtreesOdd"] = fmt(sum(A000055[n] for n in odd))
    macros["NtreesOddSci"] = sci(sum(A000055[n] for n in odd))
    macros["NtreesAll"] = fmt(sum(A000055[n] for n in allv))
    macros["NtreesAllSci"] = sci(sum(A000055[n] for n in allv))
    for n in (21, 25, 29, 30, 31):
        macros[f"Ntrees{WORDS[n]}"] = fmt(A000055[n])
        macros[f"Ntrees{WORDS[n]}Sci"] = sci(A000055[n])
    macros["WorkMaxOdd"] = fmt(max(main_rows[n]["work_max"] for n in odd))
    macros["MeanWorkThirtyOne"] = f"{main_rows[31]['work_total'] / main_rows[31]['trees']:.1f}"

    # ---- Table 2: mean work by number of degree-2 vertices (edge-graceful runs)
    lines = []
    for n in sorted(x for x in odd if x >= 21):
        by = {int(k): v for k, v in main_rows[n]["by_deg2"].items()}
        def cell(k):
            return f"{by[k][1] / by[k][0]:.1f}" if k in by and by[k][0] else "--"
        mean = main_rows[n]["work_total"] / main_rows[n]["trees"]
        lines.append(f"{n} & {mean:.1f} & {cell(0)} & {cell(5)} & {cell(10)} & {cell(15)} & {cell(20)} \\\\")
    write_table("tab_deg2.tex", "rrrrrrr",
                r"$n$ & all & $d_2=0$ & $d_2=5$ & $d_2=10$ & $d_2=15$ & $d_2=20$ \\", lines)
    by31 = {int(k): v for k, v in main_rows[31]["by_deg2"].items()}
    ratios = [(by31[k + 1][1] / by31[k + 1][0]) / (by31[k][1] / by31[k][0]) for k in range(0, 20)
              if k in by31 and k + 1 in by31]
    macros["DegTwoRatioMin"] = f"{min(ratios):.2f}"
    macros["DegTwoRatioMax"] = f"{max(ratios):.2f}"

    # ---- Table 3: zero vertex
    lines = []
    zmax = 3
    tot_pairs = tot_trees = 0
    for d in sorted(ROOT.glob("runs/zr_n*"), key=lambda p: int(p.name[4:])):
        n = int(d.name[4:])
        t = summarize_run(d)
        if t["trees"] != A000055[n]:
            continue                                   # order not finished yet
        imp, uns = read_pairs(sorted(d.glob("parts/*.hard.txt")))
        bad = imp + uns
        fam = sum(1 for _, _, v, par in bad if family_member(adjacency(par), v))
        outside = len(bad) - fam
        assert t["records_invalid"] == 0 and t["records_empty"] == len(bad)
        assert fam == family_size(n) and (outside == 0 or n == 9), f"zero vertex: unexpected pairs at n={n}"
        zmax = max(zmax, n)
        tot_pairs += t["records"]
        tot_trees += t["trees"]
        lines.append(f"{n} & {fmt(t['trees'])} & {fmt(t['records'])} & {fmt(t['records_valid'])} & "
                     f"{family_size(n)} & {outside} & {len(imp)} \\\\")
    write_table("tab_zero.tex", "rrrrrrr",
                r"$n$ & trees & pairs & certified & Prop.~\ref{prop:double} & other & proved by search \\", lines)
    macros["ZeroMaxN"] = str(zmax)
    macros["ZeroPairsTotal"] = fmt(tot_pairs)
    macros["ZeroPairsTotalSci"] = sci(tot_pairs)
    macros["ZeroTreesTotal"] = fmt(tot_trees)

    # ---- antimagic fallback stage: largest fraction of trees of an even order that needed it
    frac = max(main_rows[n]["solved_by_stage"][1] / main_rows[n]["trees"] for n in allv if n % 2 == 0)
    macros["AMFallbackMaxPct"] = f"{100 * frac:.3f}"

    # ---- independent checks (runs/checks/*.json), if present
    chk = ROOT / "runs" / "checks"
    if (chk / "uniqueness.json").exists():
        u = json.loads((chk / "uniqueness.json").read_text())
        assert all(r["unique_and_complete"] for r in u)
        macros["UniqNmax"] = str(max(r["n"] for r in u))
        macros["UniqTrees"] = fmt(sum(r["trees"] for r in u))
    if (chk / "recheck.json").exists():
        rc = json.loads((chk / "recheck.json").read_text())
        assert all(r["ok"] for r in rc)
        macros["RecheckParts"] = str(len(rc))
        macros["RecheckRecords"] = fmt(sum(r["checked"] for r in rc))
        macros["RecheckNmin"] = str(min(r["n"] for r in rc))
    if (chk / "zr_counts.json").exists():
        zc = json.loads((chk / "zr_counts.json").read_text())
        assert all(r["other_exceptions"] == (2 if r["n"] == 9 else 0) for r in zc)
        mins = [str(r["min_count_per_unit_class"]) for r in zc]
        ns = [str(r["n"]) for r in zc]
        macros["ZeroCountNmax"] = str(max(r["n"] for r in zc))
        macros["ZeroCountMins"] = (", ".join(mins[:-1]) + " and " + mins[-1] + " for $n=" +
                                   ", ".join(ns[:-1]) + "$ and~" + ns[-1])
    # ---- robustness of the hardest trees: re-solve with 50 other seeds (bin/egsolve)
    import subprocess
    lines_in = []
    for n in (25, 27, 29, 31):
        for work, part, par in main_rows[n]["hardest"][:10]:
            lines_in.append(" ".join(map(str, par)))
    exe = ROOT / "bin" / ("egsolve.exe" if (ROOT / "bin" / "egsolve.exe").exists() else "egsolve")
    out = subprocess.run([str(exe), "--repeat", "50"], input="\n".join(lines_in) + "\n",
                         capture_output=True, text=True, check=True).stdout.split("\n")
    stats = [list(map(int, l.split(":")[0].split()[1:4])) for l in out if l.strip()]
    macros["HardTreesCount"] = str(len(stats))
    macros["HardMedianMin"] = str(min(s_[1] for s_ in stats))
    macros["HardMedianMax"] = str(max(s_[1] for s_ in stats))
    macros["HardWorstOfFifty"] = str(max(s_[2] for s_ in stats))
    path = " ".join(str(i) for i in range(30))                    # P_31 rooted at an end
    o = subprocess.run([str(exe), "--repeat", "50"], input=path + "\n", capture_output=True, text=True, check=True).stdout
    macros["PathMedianThirtyOne"] = o.split(":")[0].split()[2]

    with open(OUT / "numbers.tex", "w", encoding="utf-8") as f:
        f.write("% generated by tools/make_paper_tables.py -- do not edit\n")
        for k, v in macros.items():
            f.write(f"\\newcommand{{\\{k}}}{{{v}}}\n")
    for k, v in macros.items():
        print(f"{k} = {v}")


if __name__ == "__main__":
    main()
