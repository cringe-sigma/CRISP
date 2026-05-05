#!/usr/bin/env python3
"""E2.1 - Stress-task throughput validation.

Each of the three channel attackers (LLC/BUS/MEM, mapped via
config/benchmarks.yaml -> channel_attackers) is run alone for a fixed
number of repetitions and the achieved channel rate is reported.

For LLC: rate = cache-misses / cycles  (proxy for L2 refill rate)
For BUS: rate = cache-misses / cycles  (BUS_ACCESS unavailable in
         multi_proc_pmu's built-in event group; we fall back to
         cache-misses which on A53 dominates BUS traffic)
For MEM: rate = cache-misses / cycles

The acceptance criterion (paper ¡ì0): each channel saturates >= 85% of
the strongest observed rate across the channel-attacker family.

Output: data/processed/e21_stress_validation.csv
        figs/e21_stress_validation.svg
"""
from __future__ import annotations
import argparse
import csv
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.runner import (start_step, end_step, run_logged, DATA_PROC, FIG_DIR,
                         parse_args)
from lib.common import run_pmu, parse_benchmarks_yaml
from lib.svg_plot import bar
from lib.platform import load as load_platform


CHANNEL_FAMILY = {
    "LLC": ["CACHE", "MTH_CACHE", "PR_CACHE"],
    "BUS": ["BUS",   "MTH_BUS",   "PR_MEMBUS"],
    "MEM": ["MEM",   "MTH_MEM",   "PR_ROWBUF"],
}


def main() -> int:
    p = parse_args("E2.1 stress validation")
    p.add_argument("--reps-stress", type=int, default=1,
                   help="N (multi_proc_pmu samples) per stress run; "
                        "paper uses 5 (scaled 5x down by user request)")
    p.add_argument("--sudo-pw", default=os.environ.get("SUDO_PW"),
                   help="sudo password (default: $SUDO_PW)")
    args = p.parse_args()

    plat = load_platform()
    cores = plat.get("attacker_cores", [2, 3])
    bm = parse_benchmarks_yaml()
    ch_pick = bm.get("channel_attackers", {})

    log = start_step("E21_stress_validation",
                     params={"reps": args.reps_stress,
                             "channel_attackers": ch_pick,
                             "victim_core": plat.get("victim_core", 1)})
    rows: list[dict] = []
    for ch, fam in CHANNEL_FAMILY.items():
        for atk in fam:
            # one attacker on victim core (so multi_proc_pmu measures the
            # attacker itself), three idle co-runners not relevant here.
            r = run_pmu(victim=atk, attackers=[],
                        n=args.reps_stress, sudo_pw=args.sudo_pw,
                        timeout=600)
            cyc = r["metrics"].get("cycles", {}).get("median", 0)
            inst = r["metrics"].get("instructions", {}).get("median", 0)
            miss = r["metrics"].get("cache_misses", {}).get("median", 0)
            rate = miss / cyc if cyc else 0
            rows.append({
                "channel": ch, "attacker": atk,
                "n": args.reps_stress, "rc": r["rc"],
                "cycles_med": cyc, "inst_med": inst, "misses_med": miss,
                "rate_miss_per_cycle": round(rate, 6),
                "is_pick": atk == ch_pick.get(ch, ""),
            })
            print(f"  {ch} / {atk}  rate={rate:.4g}  rc={r['rc']}",
                  file=sys.stderr)

    # acceptance check
    by_ch: dict[str, list[dict]] = {}
    for row in rows:
        by_ch.setdefault(row["channel"], []).append(row)
    accept: dict[str, dict] = {}
    for ch, lst in by_ch.items():
        peak = max(r["rate_miss_per_cycle"] for r in lst)
        pick = next((r for r in lst if r["is_pick"]), lst[0])
        ratio = pick["rate_miss_per_cycle"] / peak if peak else 0
        accept[ch] = {
            "peak": peak, "picked": pick["attacker"],
            "picked_rate": pick["rate_miss_per_cycle"],
            "ratio": ratio, "ok": ratio >= 0.85,
        }
        print(f"[acc] {ch}: pick={pick['attacker']}  "
              f"rate={pick['rate_miss_per_cycle']:.3g}  "
              f"peak={peak:.3g}  ratio={ratio:.2%}  "
              f"ok={accept[ch]['ok']}", file=sys.stderr)

    out = Path(args.out_dir) / "e21_stress_validation.csv"
    fields = ["channel","attacker","rate_miss_per_cycle","is_pick","theta"]
    with out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=(list(rows[0].keys()) if rows else fields))
        w.writeheader()
        if rows:
            w.writerows(rows)
    fig = FIG_DIR / "e21_stress_validation.svg"
    bar(values=[r["rate_miss_per_cycle"] for r in rows],
        labels=[f"{r['channel']}/{r['attacker']}" for r in rows],
        path=str(fig),
        title="E2.1 stress task throughput (cache-misses / cycle)",
        ylabel="rate", colors=["#d62728" if r["is_pick"] else "#9aa1aa"
                                for r in rows])

    end_step(outputs=[str(out), str(fig)], extra={"acceptance": accept})
    return 0


if __name__ == "__main__":
    sys.exit(main())
