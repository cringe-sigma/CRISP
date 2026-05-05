#!/usr/bin/env python3
"""Apply a tail-aware safety factor to e3_bounds.csv.

Derives a per-bench safety factor rho_safe from observed long-run c_obs
(default source: data/processed/e7_case_study.csv) and writes
e3_bounds_safe.csv with an extra column ``rampart_full_safe``.

Formula (per bench i):
    rho_emp_i = max_obs_i / rampart_full_i              # how badly the old
                                                          bound was beaten
    rho_safe_i = max(rho_min, ceil_q * rho_emp_i)        # add headroom
    rampart_full_safe_i = rampart_full_i * rho_safe_i

If a bench has no observed samples, fall back to ``--rho-default``.

Usage:
    python3 apply_safety_factor.py \
        --bounds data/processed/e3_bounds.csv \
        --observed data/processed/e7_case_study.csv \
        --rho-default 1.05 \
        --rho-min 1.02 \
        --headroom 1.02 \
        --out data/processed/e3_bounds_safe.csv
"""
from __future__ import annotations
import argparse, csv, sys
from collections import defaultdict
from pathlib import Path


def percentile(xs, q):
    if not xs:
        return 0.0
    s = sorted(xs)
    k = max(0, min(len(s) - 1, int(round(q * (len(s) - 1)))))
    return s[k]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bounds", required=True)
    ap.add_argument("--observed", default="",
                    help="CSV with columns 'victim' and 'c_obs' (e.g. e7_case_study.csv)")
    ap.add_argument("--rho-default", type=float, default=1.05)
    ap.add_argument("--rho-min",     type=float, default=1.02)
    ap.add_argument("--headroom",    type=float, default=1.02,
                    help="multiplicative cushion on top of empirical max")
    ap.add_argument("--quantile",    type=float, default=1.0,
                    help="quantile of c_obs to use (1.0 = max, 0.9999 = p99.99)")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    obs: dict[str, list[float]] = defaultdict(list)
    if a.observed and Path(a.observed).is_file():
        with open(a.observed) as f:
            rd = csv.DictReader(f)
            # accept either 'victim' or 'bench' column
            vk = "victim" if "victim" in rd.fieldnames else "bench"
            for r in rd:
                try:
                    obs[r[vk]].append(float(r["c_obs"]))
                except (KeyError, ValueError):
                    continue

    with open(a.bounds) as f:
        rows = list(csv.DictReader(f))
    if not rows:
        print("[err] bounds csv empty"); return 1

    out_fields = list(rows[0].keys()) + [
        "n_obs", "obs_qmax", "rho_emp", "rho_safe", "rampart_full_safe"]
    n_safe_changed = 0
    for r in rows:
        b = r["bench"]
        ramp = float(r["rampart_full"])
        xs = obs.get(b, [])
        q = percentile(xs, a.quantile) if xs else 0.0
        rho_emp = (q / ramp) if (xs and ramp > 0) else 0.0
        rho_safe = max(a.rho_min, a.rho_default,
                       a.headroom * rho_emp if rho_emp > 0 else 0.0)
        rampart_safe = ramp * rho_safe
        r["n_obs"] = len(xs)
        r["obs_qmax"] = round(q, 3)
        r["rho_emp"] = round(rho_emp, 4)
        r["rho_safe"] = round(rho_safe, 4)
        r["rampart_full_safe"] = round(rampart_safe, 3)
        if rho_safe > a.rho_default + 1e-9:
            n_safe_changed += 1

    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    with open(a.out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=out_fields)
        w.writeheader(); w.writerows(rows)
    print(f"[ok] {a.out}  benches={len(rows)}  tail_uplifted={n_safe_changed}"
          f"  default_rho={a.rho_default}  min_rho={a.rho_min}"
          f"  headroom={a.headroom}  quantile={a.quantile}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
