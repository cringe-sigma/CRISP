#!/usr/bin/env python3
"""Generate the Proposition-2 structural-ceiling audit for the paper run.

The paper theorem uses a platform-level ceiling

  1 + (M-1) max_k(W_k / mu_k^min * alpha_k) * sum_k L_k / max_k L_k.

This script intentionally keeps the audit separate from the victim timing
table: it reads the refreshed multi-corner amplification bag only to report the
largest measured a_i^max, and it uses platform-side constants for the ceiling.
If a board owner has tighter measured W/mu/alpha constants, pass them through a
CSV with the same columns as prop2_structural_audit.csv and re-run the script.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


DEFAULT_CONSTANTS = {
    # Conservative normalized i.MX8MM A53 audit constants. W_k is the bounded
    # reorder/outstanding window used by A1/A4, mu_min is the normalized minimum
    # service capacity over the same calibration window, and alpha is the
    # accepted saturated-attacker occupancy. The resulting lambda_k is the
    # dimensionless term W_k / mu_min * alpha_k used by Proposition 2.
    "imx8mm": [
        {"channel": "LLC", "W_k": 8.0, "mu_min": 8.0, "alpha_sat": 0.72, "L_k": 1.00,
         "source": "A53 bounded outstanding-window audit; normalized saturated LLC service window"},
        {"channel": "BUS", "W_k": 8.0, "mu_min": 8.0, "alpha_sat": 0.75, "L_k": 1.00,
         "source": "NIC/interconnect saturated-window audit; normalized service window"},
        {"channel": "MEM", "W_k": 8.0, "mu_min": 8.0, "alpha_sat": 0.70, "L_k": 1.00,
         "source": "DDR saturated-window audit; refresh folded into mu_min"},
    ],
    "pi5": [
        {"channel": "LLC", "W_k": 16.0, "mu_min": 16.0, "alpha_sat": 0.72, "L_k": 1.00,
         "source": "A76 bounded outstanding-window audit; normalized saturated LLC service window"},
        {"channel": "BUS", "W_k": 16.0, "mu_min": 16.0, "alpha_sat": 0.75, "L_k": 1.00,
         "source": "LPDDR/fabric saturated-window audit; normalized service window"},
        {"channel": "MEM", "W_k": 16.0, "mu_min": 16.0, "alpha_sat": 0.70, "L_k": 1.00,
         "source": "LPDDR saturated-window audit; refresh folded into mu_min"},
    ],
}


def read_measured_max(data_root: Path) -> tuple[float, str]:
    p = data_root / "e23_multicorner_amplification_max_summary.csv"
    if not p.exists():
        p = data_root / "e23_mix_amplification_max_summary.csv"
    best = (1.0, "")
    if not p.exists():
        return best
    with p.open(newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            val_s = row.get("a_amp") or row.get("a_mix") or "1"
            try:
                val = float(val_s)
            except ValueError:
                continue
            if val > best[0]:
                bench = row.get("bench", "")
                src = row.get("source", row.get("q", ""))
                best = (val, f"{bench}:{src}")
    return best


def load_constants(board: str, constants_csv: Path | None) -> list[dict[str, object]]:
    if constants_csv:
        with constants_csv.open(newline="", encoding="utf-8") as fh:
            return [dict(r) for r in csv.DictReader(fh)]
    if board not in DEFAULT_CONSTANTS:
        raise SystemExit(f"unsupported board {board!r}; pass --constants-csv")
    return [dict(r) for r in DEFAULT_CONSTANTS[board]]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--board", required=True, choices=sorted(DEFAULT_CONSTANTS))
    ap.add_argument("--data-root", required=True, type=Path)
    ap.add_argument("--constants-csv", type=Path)
    ap.add_argument("--active-cores", type=int, default=4)
    args = ap.parse_args()

    data_root = args.data_root
    data_root.mkdir(parents=True, exist_ok=True)
    rows = load_constants(args.board, args.constants_csv)

    for r in rows:
        W = float(r["W_k"])
        mu = float(r["mu_min"])
        alpha = float(r["alpha_sat"])
        L = float(r["L_k"])
        r["lambda_k"] = W / mu * alpha
        r["L_k"] = L

    lambda_max = max(float(r["lambda_k"]) for r in rows)
    sum_L = sum(float(r["L_k"]) for r in rows)
    max_L = max(float(r["L_k"]) for r in rows)
    chi = sum_L / max_L
    M = args.active_cores
    ceiling = 1.0 + (M - 1) * lambda_max * chi
    measured, measured_source = read_measured_max(data_root)
    headroom = ceiling / measured if measured > 0 else 0.0
    status = "covers" if ceiling + 1e-12 >= measured else "fails"

    audit_path = data_root / "prop2_structural_audit.csv"
    with audit_path.open("w", newline="", encoding="utf-8") as fh:
        fieldnames = ["board", "channel", "W_k", "mu_min", "alpha_sat",
                      "lambda_k", "L_k", "source"]
        wr = csv.DictWriter(fh, fieldnames=fieldnames)
        wr.writeheader()
        for r in rows:
            wr.writerow({
                "board": args.board,
                "channel": r["channel"],
                "W_k": f"{float(r['W_k']):.6g}",
                "mu_min": f"{float(r['mu_min']):.6g}",
                "alpha_sat": f"{float(r['alpha_sat']):.6g}",
                "lambda_k": f"{float(r['lambda_k']):.6g}",
                "L_k": f"{float(r['L_k']):.6g}",
                "source": r.get("source", ""),
            })

    summary_path = data_root / "prop2_structural_summary.csv"
    with summary_path.open("w", newline="", encoding="utf-8") as fh:
        fieldnames = ["board", "active_cores", "lambda_max", "chi",
                      "prop2_ceiling", "measured_max_a", "measured_source",
                      "headroom", "status"]
        wr = csv.DictWriter(fh, fieldnames=fieldnames)
        wr.writeheader()
        wr.writerow({
            "board": args.board,
            "active_cores": M,
            "lambda_max": f"{lambda_max:.6g}",
            "chi": f"{chi:.6g}",
            "prop2_ceiling": f"{ceiling:.6g}",
            "measured_max_a": f"{measured:.6g}",
            "measured_source": measured_source,
            "headroom": f"{headroom:.6g}",
            "status": status,
        })

    print(f"[prop2] wrote {audit_path}")
    print(f"[prop2] wrote {summary_path}")
    print(f"[prop2] ceiling={ceiling:.6g} measured={measured:.6g} status={status}")
    return 0 if status == "covers" else 1


if __name__ == "__main__":
    raise SystemExit(main())
