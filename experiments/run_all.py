#!/usr/bin/env python3
"""run_all.py - convenience launcher.

Runs each E* step in dependency order.  Defaults are the user's
"scaled-down" mode (R=100 instead of paper's R=1000; reps=20 instead
of 100). Use --paper to revert to paper-sized runs (very expensive).

Usage:
    python3 experiments/run_all.py --steps E1 E2 E3 E5 E6 E8 E9
    python3 experiments/run_all.py --all --sudo-pw "$SUDO_PW"
"""
from __future__ import annotations
import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PY = sys.executable

STEPS: list[tuple[str, list[list[str]]]] = [
    ("E1", [
        [PY, str(ROOT / "01_phase1/extract_loopbounds.py")],
        [PY, str(ROOT / "01_phase1/static_estimates.py")],
    ]),
    ("E2.1", [[PY, str(ROOT / "02_phase2/e21_stress_validate.py")]]),
    ("E2.2", [[PY, str(ROOT / "02_phase2/e22_adversary_search.py")]]),
    ("E2.3", [[PY, str(ROOT / "02_phase2/e23_five_config_sweep.py")]]),
    ("E2.4", [[PY, str(ROOT / "02_phase2/e24_lemma1a.py")]]),
    ("E2.5", [[PY, str(ROOT / "02_phase2/e25_lemma2.py")]]),
    ("E3",   [[PY, str(ROOT / "03_bounding/interference_bound.py")]]),
    ("E3.1", [[PY, str(ROOT / "03_bounding/e31_polyrhythm_compare.py")]]),
    ("E3.2", [[PY, str(ROOT / "03_bounding/e32_mempol_compare.py")]]),
    ("E3.3", [[PY, str(ROOT / "03_bounding/e33_additive_violations.py")]]),
    ("E4",   [[PY, str(ROOT / "04_24h/e4_safety_histogram.py")]]),
    ("E5",   [[PY, str(ROOT / "05_ablation/e5_ablation.py")]]),
    ("E6",   [[PY, str(ROOT / "06_scheduling/e6_schedule.py")]]),
    ("E7",   [[PY, str(ROOT / "07_case_study/e7_case_study.py")]]),
    ("E8",   [[PY, str(ROOT / "08_rtc/e8_rtc.py")]]),
    ("E9",   [[PY, str(ROOT / "09_zephyr/e9_zephyr.py")]]),
]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--steps", nargs="*", default=None)
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--paper", action="store_true",
                    help="use paper sizes (R=1000); default is 10x scaled")
    ap.add_argument("--sudo-pw", default=os.environ.get("SUDO_PW"))
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    selected: list[str] = []
    if args.all or not args.steps:
        selected = [s for s, _ in STEPS]
    else:
        selected = args.steps

    extra: list[str] = []
    if args.paper:
        extra += ["--R", "1000"]
    if args.sudo_pw:
        os.environ["SUDO_PW"] = args.sudo_pw
        extra += ["--sudo-pw", args.sudo_pw]

    rc = 0
    for label, cmds in STEPS:
        if label not in selected: continue
        for cmd in cmds:
            full = cmd + [a for a in extra if not (a in cmd)]
            print("=" * 8, label, "=" * 8, " ".join(full), file=sys.stderr)
            if args.dry_run:
                continue
            r = subprocess.run(full)
            if r.returncode != 0:
                print(f"[err] {label} returned {r.returncode}", file=sys.stderr)
                rc = max(rc, r.returncode)
                if r.returncode > 1:
                    return rc
    return rc


if __name__ == "__main__":
    sys.exit(main())
