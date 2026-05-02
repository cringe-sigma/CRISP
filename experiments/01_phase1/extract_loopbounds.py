#!/usr/bin/env python3
"""E1.a ¡ª Extract loopbound annotations from TACLeBench / EEMBC sources.

Scans every .c/.h file under bench/bench/<name> for the macro
`_Pragma("loopbound min X max Y")` (TACLeBench convention) and any
`#pragma loopbound min X max Y` siblings, recording the maximum bound
per loop.  Output: data/processed/loopbounds.json.

Usage:
    python3 experiments/01_phase1/extract_loopbounds.py [--bench NAME]
"""
from __future__ import annotations
import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.runner import start_step, end_step, ROOT, DATA_PROC

BENCH_ROOT = ROOT.parent / "bench" / "bench"
RE_PRAGMA = re.compile(
    r"""(?:_Pragma\s*\(\s*"loopbound\s+min\s+(\d+)\s+max\s+(\d+)"\s*\)
        |\#\s*pragma\s+loopbound\s+min\s+(\d+)\s+max\s+(\d+))""",
    re.X)


def scan_file(path: Path) -> list[tuple[int, int]]:
    out: list[tuple[int, int]] = []
    try:
        text = path.read_text(errors="replace")
    except OSError:
        return out
    for m in RE_PRAGMA.finditer(text):
        a, b, c, d = m.groups()
        lo = int(a or c); hi = int(b or d)
        out.append((lo, hi))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bench", default=None,
                    help="restrict to a single benchmark dir")
    ap.add_argument("--out", default=str(DATA_PROC / "loopbounds.json"))
    args = ap.parse_args()

    log = start_step("E1a_extract_loopbounds",
                     params={"bench": args.bench, "out": args.out},
                     inputs=[str(BENCH_ROOT)])
    result: dict = {}
    if not BENCH_ROOT.exists():
        print(f"[warn] {BENCH_ROOT} not found", file=sys.stderr)
    else:
        for sub in sorted(BENCH_ROOT.iterdir()):
            if not sub.is_dir():
                continue
            if args.bench and sub.name != args.bench:
                continue
            entries: list[dict] = []
            for src in sub.rglob("*.c"):
                bounds = scan_file(src)
                if bounds:
                    entries.append({"file": str(src.relative_to(BENCH_ROOT)),
                                     "bounds": bounds,
                                     "n_loops": len(bounds),
                                     "max_iter": max(b[1] for b in bounds)})
            result[sub.name] = {
                "n_files_with_bounds": len(entries),
                "n_loops_total": sum(e["n_loops"] for e in entries),
                "max_iter_global": max(
                    (e["max_iter"] for e in entries), default=0),
                "loops": entries,
            }
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out).write_text(json.dumps(result, indent=2))
    n_b = len(result)
    n_l = sum(v["n_loops_total"] for v in result.values())
    print(f"[ok] {n_b} benchmarks, {n_l} annotated loops -> {args.out}")
    end_step(outputs=[args.out],
             extra={"n_benchmarks": n_b, "n_loops": n_l})
    return 0


if __name__ == "__main__":
    sys.exit(main())
