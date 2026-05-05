#!/usr/bin/env python3
"""E3.2 - MemPol shield comparison.

Three-backend shield latency comparison (RAMPART hook / MemPol PMU
overflow / standing MemGuard).  The MemPol and MemGuard kernels are
not present on this image; the script measures RAMPART activation
latency and *records* literature values for the other two as defined
in the paper Table 4 footnote.

Activation-latency proxy: number of cycles between a synthetic
overshoot signal and the next multi_proc_pmu sample whose cycle count
returns below the bound.  Implemented purely as a microbench - it
calls multi_proc_pmu twice (with/without attackers) and reports the
delta as 'rampart_latency_us'.

Output: data/processed/e32_mempol_compare.csv
"""
from __future__ import annotations
import argparse, csv, os, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.runner import start_step, end_step, parse_args
from lib.common import run_pmu
from lib.platform import load as load_platform


def main() -> int:
    p = parse_args("E3.2 MemPol comparison")
    p.add_argument("--bench", default="fir2dim")
    p.add_argument("--attacker", default="MTH_MEM")
    p.add_argument("--R", type=int, default=20)
    p.add_argument("--trials", type=int, default=20)
    p.add_argument("--sudo-pw", default=os.environ.get("SUDO_PW"))
    args = p.parse_args()
    plat = load_platform()
    freq = plat.get("core_freq_khz", 1600000)

    log = start_step("E32_mempol_compare", params=vars(args))
    out = Path(args.out_dir) / "e32_mempol_compare.csv"
    rows = []
    for i in range(args.trials):
        r0 = run_pmu(args.bench, [], n=args.R, sudo_pw=args.sudo_pw, timeout=600)
        r1 = run_pmu(args.bench, [args.attacker]*3, n=args.R,
                     sudo_pw=args.sudo_pw, timeout=600)
        c0 = r0["metrics"].get("cycles", {}).get("median", 0)
        c1 = r1["metrics"].get("cycles", {}).get("median", 0)
        # Activation latency proxy: extra cycles per sample in microseconds.
        lat_us = max(c1 - c0, 0) / (freq * 1e3) * 1e6
        rows.append({"trial": i,
                     "rampart_latency_us": round(lat_us, 2),
                     "mempol_latency_us_lit": 4.2,
                     "memguard_latency_us_lit": 9.8})
    fields = ["trial","rampart_latency_us","mempol_latency_us_lit","memguard_latency_us_lit"]
    with out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=(list(rows[0].keys()) if rows else fields))
        w.writeheader()
        if rows:
            w.writerows(rows)
    end_step(outputs=[str(out)])
    return 0


if __name__ == "__main__":
    sys.exit(main())
