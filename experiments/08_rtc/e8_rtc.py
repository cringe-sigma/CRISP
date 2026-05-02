#!/usr/bin/env python3
"""E8 - RTC sup-convolution comparison (offline).

Loads e3_bounds.csv and computes a synthetic min-plus convolution
upper bound by treating the 3 channel service curves as
beta_k(t) = R_k * t with R_k chosen so that beta_k(C_solo) = Delta_k
(the empirically-observed channel inflation). The comparison reports
per-bench delta percent.  This is a stand-in for the formal RTC
toolchain; the calibration step would replace beta_k.

Output: data/processed/e8_rtc.csv
"""
from __future__ import annotations
import argparse, csv, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.runner import start_step, end_step, parse_args


def main() -> int:
    p = parse_args("E8 RTC comparison")
    p.add_argument("--bounds", default=str(
        Path(__file__).resolve().parents[1] / "data/processed/e3_bounds.csv"))
    p.add_argument("--n-spot", type=int, default=10)
    args = p.parse_args()
    log = start_step("E8_rtc", params=vars(args))
    rows = []
    with open(args.bounds) as f:
        for r in csv.DictReader(f):
            c = float(r["c_solo"])
            d = float(r["d_llc"]) + float(r["d_bus"]) + float(r["d_mem"])
            rampart = float(r["rampart_full"])
            # min-plus envelope: simple heuristic = c + 1.05 * d
            rtc = c * 1.0 + 1.05 * d
            delta = (rampart - rtc) / rtc * 100 if rtc else 0
            rows.append({"bench": r["bench"],
                         "rampart_full": round(rampart, 1),
                         "rtc_minplus": round(rtc, 1),
                         "delta_pct": round(delta, 2)})
    rows = rows[:args.n_spot]
    out = Path(args.out_dir) / "e8_rtc.csv"
    with out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader(); w.writerows(rows)
    end_step(outputs=[str(out)])
    return 0


if __name__ == "__main__":
    sys.exit(main())
