#!/usr/bin/env python3
"""E1.b ¡ª Static memory-access estimate per benchmark.

For every benchmark with a built ELF in build/<name>/<name>, run
    aarch64-linux-gnu-objdump -d <elf>
and count load/store instructions per function symbol.  Combine with
the per-loop max iterations from loopbounds.json to derive a coarse
upper bound N_i^mem (paper ¡ì3.1).  Then clip with the L2 capacity
(platform_imx8mm.yaml/l2_kb * 1024 / 64) to obtain N_i^L2.

Output: data/processed/static_estimates.csv with columns
    bench, n_loops, max_iter, ld_count, st_count, mem_ops_static,
    n_mem_upper, n_l2_upper, alpha_l2_static
"""
from __future__ import annotations
import argparse
import csv
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.runner import start_step, end_step, ROOT, DATA_PROC, run_logged
from lib.platform import load as load_platform

BUILD_ROOT = ROOT.parent / "build"

LDST_RE = re.compile(
    r"\b(?:ldr|ldrb|ldrh|ldrsw|ldp|ldur|ldxr|ldaxr|ldnp"
    r"|str|strb|strh|stp|stur|stxr|stlxr|stnp|prfm)\b",
    re.I)


def find_objdump() -> str | None:
    for c in ("aarch64-linux-gnu-objdump", "objdump"):
        p = shutil.which(c)
        if p:
            return p
    return None


def count_ldst(elf: Path, objdump: str) -> tuple[int, int]:
    cp = run_logged([objdump, "-d", "--no-show-raw-insn", str(elf)],
                    check=False)
    if cp.returncode != 0:
        return 0, 0
    text = (cp.stdout or b"").decode("utf-8", "replace")
    ld = st = 0
    for line in text.splitlines():
        m = LDST_RE.search(line)
        if not m:
            continue
        op = m.group(0).lower()
        if op.startswith(("st", "prfm")):
            st += 1
        else:
            ld += 1
    return ld, st


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--loopbounds",
                    default=str(DATA_PROC / "loopbounds.json"))
    ap.add_argument("--out", default=str(DATA_PROC / "static_estimates.csv"))
    args = ap.parse_args()

    log = start_step("E1b_static_estimates",
                     params={"loopbounds": args.loopbounds,
                             "out": args.out},
                     inputs=[args.loopbounds, str(BUILD_ROOT)])
    plat = load_platform()
    l2_lines = (plat.get("l2_kb", 512) * 1024) // 64

    lb = json.loads(Path(args.loopbounds).read_text()) \
        if Path(args.loopbounds).exists() else {}

    objdump = find_objdump()
    if not objdump:
        print("[warn] no objdump found; ld/st counts will be 0", file=sys.stderr)

    rows: list[dict] = []
    for sub in sorted(BUILD_ROOT.iterdir()) if BUILD_ROOT.exists() else []:
        if not sub.is_dir():
            continue
        # On this build the per-benchmark artefact is an unlinked .o.
        candidates = [sub / sub.name, sub / f"{sub.name}.o"]
        elf = next((c for c in candidates if c.exists()), None)
        if elf is None:
            continue
        ld, st = count_ldst(elf, objdump) if objdump else (0, 0)
        info = lb.get(sub.name, {})
        max_iter = info.get("max_iter_global", 1) or 1
        n_loops = info.get("n_loops_total", 0)
        mem_ops = ld + st
        n_mem_upper = mem_ops * max_iter
        n_l2_upper = min(n_mem_upper, l2_lines)
        alpha_l2 = n_l2_upper / max(n_mem_upper, 1)
        rows.append({
            "bench": sub.name,
            "n_loops": n_loops,
            "max_iter": max_iter,
            "ld_count": ld,
            "st_count": st,
            "mem_ops_static": mem_ops,
            "n_mem_upper": n_mem_upper,
            "n_l2_upper": n_l2_upper,
            "alpha_l2_static": round(alpha_l2, 5),
        })

    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    with open(args.out, "w", newline="") as f:
        if rows:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            w.writeheader(); w.writerows(rows)
    print(f"[ok] {len(rows)} benchmarks -> {args.out}")
    end_step(outputs=[args.out], extra={"n_benchmarks": len(rows)})
    return 0


if __name__ == "__main__":
    sys.exit(main())
