#!/usr/bin/env python3
"""E5 - Ablation table (offline; reads e3_bounds.csv).

Reports five rows per bench with cumulative bound:
    1) full-isolation
    2) +Opt-1 (channel-type exclusion: drop full_iso 3x penalty)
    3) +Opt-1+Opt-2 (stall discount: 0.92 * sum)
    4) RAMPART-additive (gamma * (Csolo + sum_k Delta_k))
    5) RAMPART-full (+ A_i)

Output: data/processed/e5_ablation.csv
"""
from __future__ import annotations
import argparse, csv, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.runner import start_step, end_step, parse_args
from lib.platform import load as load_platform


def main() -> int:
    p = parse_args("E5 ablation")
    p.add_argument("--bounds", default=str(
        Path(__file__).resolve().parents[1] / "data/processed/e3_bounds.csv"))
    args = p.parse_args()
    plat = load_platform(); gamma = float(plat.get("gamma_pmu", 1.02))

    log = start_step("E5_ablation", params=vars(args))
    out = Path(args.out_dir) / "e5_ablation.csv"
    rows = []
    with open(args.bounds) as f:
        for r in csv.DictReader(f):
            # Support both schemas:
            #   full:       bench, c_solo, d_llc, d_bus, d_mem, A_i, ...
            #   simplified: bench, solo,   mix,   rampart_full
            if "c_solo" in r:
                c = float(r["c_solo"])
                d_llc = float(r["d_llc"]); d_bus = float(r["d_bus"]); d_mem = float(r["d_mem"])
                d = d_llc + d_bus + d_mem
                A = float(r["A_i"])
            else:
                c = float(r["solo"])
                d_total = max(float(r["mix"]) - c, 0.0)
                # Split equally across 3 channels in absence of breakdown
                d_llc = d_bus = d_mem = d_total / 3.0
                d = d_total
                # Approximate A_i = rampart_full/gamma - c - 0.92*d
                A = max(float(r.get("rampart_full", 0)) / gamma - c - 0.92 * d, 0.0)
            opt0 = gamma * (c + 3 * max(d_llc, d_bus, d_mem))
            opt1 = gamma * (c + d)            # + type exclusion
            opt2 = gamma * (c + 0.92 * d)     # + stall discount (0.92)
            opt3 = gamma * (c + 0.92 * d)     # rampart-additive (=opt2)
            opt4 = gamma * (c + 0.92 * d + A)
            rows.append({"bench": r["bench"],
                         "full_iso": round(opt0/c, 4),
                         "opt1": round(opt1/c, 4),
                         "opt2": round(opt2/c, 4),
                         "rampart_add": round(opt3/c, 4),
                         "rampart_full": round(opt4/c, 4)})
    with out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader(); w.writerows(rows)
    # Aggregate medians
    def med(k):
        v = sorted(r[k] for r in rows)
        return v[len(v)//2] if v else 0
    print("medians:", {k: med(k) for k in
                        ("full_iso","opt1","opt2","rampart_add","rampart_full")},
          file=sys.stderr)
    end_step(outputs=[str(out)])
    return 0


if __name__ == "__main__":
    sys.exit(main())
