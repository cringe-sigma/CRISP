#!/usr/bin/env python3
"""Summarize CRISP paper-matrix logs into p99 and max CSV tables."""

import argparse
import csv
import math
from pathlib import Path
from typing import Dict, List, Optional, Tuple


CONFIGS_SINGLE = ("LLC", "BUS", "MEM")
PAIRS = (("LLC", "BUS"), ("LLC", "MEM"), ("BUS", "MEM"))
CONFIGS_ALL = ("solo", "LLC", "BUS", "MEM", "LLC+BUS", "LLC+MEM", "BUS+MEM", "Mix")


def read_manifest(path: Path) -> List[str]:
    benches = []  # type: List[str]
    if not path.exists():
        return benches
    with path.open(newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            if row.get("included", "").lower() == "true":
                benches.append(row["bench"])
    return benches


def read_cycles(path: Path) -> List[int]:
    if not path.exists():
        return []
    vals = []  # type: List[int]
    with path.open(newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            try:
                vals.append(int(row["cycles"]))
            except (KeyError, TypeError, ValueError):
                continue
    return vals


def q_value(vals: List[int], q: str) -> Optional[int]:
    if not vals:
        return None
    s = sorted(vals)
    if q == "max":
        return s[-1]
    if q == "p99":
        rank = max(1, math.ceil(0.99 * len(s)))
        return s[rank - 1]
    raise ValueError(q)


def sample_path(results_root: Path, bench: str, cfg: str) -> Path:
    return results_root / "samples" / f"{bench}__{cfg}.csv"


def log_path(results_root: Path, bench: str, cfg: str) -> Path:
    sub = "e23" if cfg in ("solo", "Mix") else "e24"
    suffix = "mix" if cfg == "Mix" else cfg
    return results_root / sub / f"{bench}__{suffix}.txt"


def fmt(x: object) -> str:
    if x is None:
        return ""
    if isinstance(x, float):
        return f"{x:.10g}"
    return str(x)


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def write_csv(path: Path, rows: List[Dict[str, object]], fields: List[str]) -> None:
    ensure_dir(path.parent)
    with path.open("w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=fields)
        w.writeheader()
        for row in rows:
            w.writerow({k: fmt(row.get(k)) for k in fields})


def summarize_closure(board: str, repo_root: Path, data_root: Path) -> None:
    rows = []  # type: List[Dict[str, object]]
    e4 = repo_root / "experiments" / "data" / "processed" / "e4_safety_histogram.csv"
    e7 = repo_root / "experiments" / "data" / "processed" / "e7_case_study.csv"

    if e4.exists():
        ratios = []  # type: List[float]
        violations = 0
        with e4.open(newline="", encoding="utf-8") as fh:
            for row in csv.DictReader(fh):
                try:
                    ratios.append(float(row.get("ratio", "")))
                except ValueError:
                    pass
                if row.get("violation", "").lower() == "true":
                    violations += 1
        rows.append({
            "board": board,
            "source": "E4",
            "records": len(ratios),
            "violations": violations,
            "deadline_misses": "",
            "shield_recovered": "",
            "max_ratio": max(ratios) if ratios else "",
            "rho_safe_min": max(ratios) if ratios else "",
            "source_path": str(e4),
        })

    if e7.exists():
        ratios = []
        misses = 0
        recovered = 0
        with e7.open(newline="", encoding="utf-8") as fh:
            for row in csv.DictReader(fh):
                try:
                    ratios.append(float(row["c_obs"]) / max(float(row["rampart_full"]), 1.0))
                except (KeyError, ValueError):
                    pass
                if row.get("deadline_miss", "").lower() == "true":
                    misses += 1
                if row.get("shield_recovered", "").lower() == "true":
                    recovered += 1
        rows.append({
            "board": board,
            "source": "E7",
            "records": len(ratios),
            "violations": "",
            "deadline_misses": misses,
            "shield_recovered": recovered,
            "max_ratio": max(ratios) if ratios else "",
            "rho_safe_min": max(ratios) if ratios else "",
            "source_path": str(e7),
        })

    write_csv(data_root / "rho_safe_closure_summary.csv", rows, [
        "board", "source", "records", "violations", "deadline_misses",
        "shield_recovered", "max_ratio", "rho_safe_min", "source_path",
    ])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--board", required=True)
    ap.add_argument("--results-root", required=True)
    ap.add_argument("--data-root", required=True)
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--require-complete", action="store_true")
    args = ap.parse_args()

    board = args.board
    results_root = Path(args.results_root)
    data_root = Path(args.data_root)
    manifest = Path(args.manifest)
    benches = read_manifest(manifest)

    per = {}  # type: Dict[Tuple[str, str], Dict[str, object]]
    missing = []  # type: List[str]
    sample_counts = []  # type: List[int]

    for bench in benches:
        for cfg in CONFIGS_ALL:
            vals = read_cycles(sample_path(results_root, bench, cfg))
            if not vals:
                missing.append(f"{bench},{cfg}")
                continue
            sample_counts.append(len(vals))
            per[(bench, cfg)] = {
                "n": len(vals),
                "p99": q_value(vals, "p99"),
                "max": q_value(vals, "max"),
            }

    single_rows = []  # type: List[Dict[str, object]]
    pair_p99_rows = []  # type: List[Dict[str, object]]
    pair_max_rows = []  # type: List[Dict[str, object]]
    mix_rows = []  # type: List[Dict[str, object]]

    for bench in benches:
        solo = per.get((bench, "solo"))
        if not solo:
            continue

        best = None
        for ch in CONFIGS_SINGLE:
            cur = per.get((bench, ch))
            if not cur:
                continue
            c_solo = int(solo["p99"])
            c_cur = int(cur["p99"])
            slowdown = c_cur / max(c_solo, 1)
            delta = c_cur - c_solo
            row = {
                "board": board,
                "bench": bench,
                "q": "p99",
                "best_channel": ch,
                "C_solo": c_solo,
                "C_best": c_cur,
                "slowdown_best": slowdown,
                "delta_best": delta,
                "source_solo": str(log_path(results_root, bench, "solo")),
                "source_best": str(log_path(results_root, bench, ch)),
            }
            if best is None or slowdown > best["slowdown_best"]:
                best = row
        if best:
            single_rows.append(best)

        for q, rows in (("p99", pair_p99_rows), ("max", pair_max_rows)):
            c_solo = int(solo[q])
            for a, b in PAIRS:
                cfg = f"{a}+{b}"
                va = per.get((bench, a))
                vb = per.get((bench, b))
                vp = per.get((bench, cfg))
                if not (va and vb and vp):
                    continue
                c_a = int(va[q])
                c_b = int(vb[q])
                c_pair = int(vp[q])
                delta_a = c_a - c_solo
                delta_b = c_b - c_solo
                delta_pair = c_pair - c_solo
                dpa = max(0, delta_a)
                dpb = max(0, delta_b)
                additive = delta_pair / max(dpa + dpb, 1)
                dominant = delta_pair / max(dpa, dpb, 1)
                denom_near_zero = (dpa + dpb) <= max(1, int(0.001 * max(c_solo, 1)))
                verdict = "noise_sensitive" if denom_near_zero else ("violation" if additive > 1 else "OK")
                rows.append({
                    "board": board,
                    "bench": bench,
                    "pair": cfg,
                    "q": q,
                    "sample_count": min(int(va["n"]), int(vb["n"]), int(vp["n"]), int(solo["n"])),
                    "C_solo": c_solo,
                    "C_a": c_a,
                    "C_b": c_b,
                    "C_pair": c_pair,
                    "slowdown_a": c_a / max(c_solo, 1),
                    "slowdown_b": c_b / max(c_solo, 1),
                    "slowdown_pair": c_pair / max(c_solo, 1),
                    "delta_a": delta_a,
                    "delta_b": delta_b,
                    "delta_pair": delta_pair,
                    "delta_pos_a": dpa,
                    "delta_pos_b": dpb,
                    "additive_violation": additive,
                    "dominant_amplification": dominant,
                    "verdict": verdict,
                    "source_solo": str(log_path(results_root, bench, "solo")),
                    "source_a": str(log_path(results_root, bench, a)),
                    "source_b": str(log_path(results_root, bench, b)),
                    "source_pair": str(log_path(results_root, bench, cfg)),
                })

        mix = per.get((bench, "Mix"))
        if mix:
            c_solo = int(solo["p99"])
            c_mix = int(mix["p99"])
            strongest = 0
            for ch in CONFIGS_SINGLE:
                cur = per.get((bench, ch))
                if cur:
                    strongest = max(strongest, max(0, int(cur["p99"]) - c_solo))
            delta_mix = c_mix - c_solo
            mix_rows.append({
                "board": board,
                "bench": bench,
                "q": "p99",
                "sample_count": min(int(solo["n"]), int(mix["n"])),
                "C_solo": c_solo,
                "C_mix": c_mix,
                "slowdown_mix": c_mix / max(c_solo, 1),
                "delta_mix": delta_mix,
                "strongest_single_delta": strongest,
                "a_mix": max(0, delta_mix) / max(strongest, 1),
                "prop2_ceiling": "",
                "ceiling_headroom": "",
                "source_solo": str(log_path(results_root, bench, "solo")),
                "source_mix": str(log_path(results_root, bench, "Mix")),
            })

    pair_p99_rows.sort(key=lambda r: float(r["additive_violation"]), reverse=True)
    pair_max_rows.sort(key=lambda r: float(r["additive_violation"]), reverse=True)
    single_rows.sort(key=lambda r: float(r["slowdown_best"]), reverse=True)
    mix_rows.sort(key=lambda r: float(r["a_mix"]), reverse=True)

    pair_fields = [
        "board", "bench", "pair", "q", "sample_count", "C_solo", "C_a", "C_b", "C_pair",
        "slowdown_a", "slowdown_b", "slowdown_pair", "delta_a", "delta_b", "delta_pair",
        "delta_pos_a", "delta_pos_b", "additive_violation", "dominant_amplification",
        "verdict", "source_solo", "source_a", "source_b", "source_pair",
    ]
    write_csv(data_root / "e24_single_poison_summary.csv", single_rows, [
        "board", "bench", "q", "best_channel", "C_solo", "C_best",
        "slowdown_best", "delta_best", "source_solo", "source_best",
    ])
    write_csv(data_root / "e24_pair_additivity_summary.csv", pair_p99_rows, pair_fields)
    write_csv(data_root / "e24_pair_additivity_max_summary.csv", pair_max_rows, pair_fields)
    write_csv(data_root / "e23_mix_amplification_summary.csv", mix_rows, [
        "board", "bench", "q", "sample_count", "C_solo", "C_mix", "slowdown_mix",
        "delta_mix", "strongest_single_delta", "a_mix", "prop2_ceiling",
        "ceiling_headroom", "source_solo", "source_mix",
    ])
    summarize_closure(board, Path.cwd(), data_root)

    report = data_root / "artifact_consistency_report.txt"
    ensure_dir(report.parent)
    p99_violations = [r for r in pair_p99_rows if r["verdict"] == "violation"]
    strongest_p99 = max(p99_violations, key=lambda r: float(r["additive_violation"]), default=None)
    strongest_max = max(pair_max_rows, key=lambda r: float(r["additive_violation"]), default=None)
    with report.open("w", encoding="utf-8") as fh:
        fh.write(f"board={board}\n")
        fh.write(f"valid_benchmark_count={len(benches)}\n")
        fh.write(f"missing_config_count={len(missing)}\n")
        fh.write(f"sample_count_min={min(sample_counts) if sample_counts else 0}\n")
        fh.write(f"sample_count_max={max(sample_counts) if sample_counts else 0}\n")
        fh.write(f"p99_additive_violations={len(p99_violations)}\n")
        fh.write(f"strongest_p99={strongest_p99}\n")
        fh.write(f"strongest_max={strongest_max}\n")
        if missing:
            fh.write("missing_configs=\n")
            for item in missing:
                fh.write(f"  {item}\n")

    if args.require_complete and missing:
        print("ERROR: incomplete TACLeBench matrix; see", report)
        return 2

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
