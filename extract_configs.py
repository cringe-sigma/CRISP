#!/usr/bin/env python3
"""Extract per-config parameters from sweep scripts and victim solo cycles
from solo_*.txt logs, writing a self-contained ``configs.tsv`` into each
results sub-directory.

Each row contains:
  tag <TAB> bg_workers <TAB> defines <TAB> ratios... <TAB> notes

We also dump ``baseline.tsv`` listing victim -> solo median cycles.
"""
from __future__ import annotations
import csv
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
RESULTS = ROOT / "results"

# Map: results sub-dir -> sweep script that produced it.
SCRIPTS = {
    "cache_aggr": "run_cache_aggressive.sh",
    "cache_bus":  "run_cache_bus_combo.sh",
    "tim_mix":    "run_tim_mix.sh",
    "tim_mix2":   "run_tim_mix2.sh",
    "tim_mix3":   "run_tim_mix3.sh",
    "tim_others": "run_tim_others.sh",
}

# Extra build-time CFLAGS that every script keeps as DEFAULT_CFLAGS.
DEFAULT_CFLAGS = "-O0 -g -Wall -Wno-unused-result -Wno-unknown-pragmas -std=gnu11"


def parse_pipe_array(text: str, var: str):
    """Pick all entries inside a bash array of the form
        VAR=(
          "tag | defines [| bg]"
          ...
        )
    Returns list[(tag, defines, bg|None)].
    """
    m = re.search(rf"{var}\s*=\s*\(([^)]*)\)", text, re.S)
    if not m:
        return []
    body = m.group(1)
    entries = []
    for line in body.splitlines():
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        if not s.startswith('"'):
            continue
        s = s.strip().rstrip(",")
        if s.startswith('"') and s.endswith('"'):
            s = s[1:-1]
        parts = [p.strip() for p in s.split("|")]
        tag = parts[0] if parts else ""
        defs = parts[1] if len(parts) > 1 else ""
        bg = parts[2] if len(parts) > 2 else None
        entries.append((tag, defs, bg))
    return entries


def expand_vars(s: str, env: dict[str, str]) -> str:
    out = s
    for _ in range(4):
        prev = out
        for k, v in env.items():
            out = out.replace("$" + k, v)
        if out == prev:
            break
    return out


def parse_script(path: Path):
    """Return list[(tag, full_defines, bg_workers, source_phase)]."""
    text = path.read_text()
    rows = []

    # Generic constant defines used by phase-B/E (CACHE_DEFS, BUS_DEFS, ...).
    consts = {}
    for var in ["CACHE_DEFS", "BUS_DEFS", "MEM_DEFS", "POINTER_DEFS",
                "PIPELINE_DEFS", "CACHE_BEST", "CACHE_RW10", "BEST_MEM",
                "ALL_DEFS", "ALL_BEST", "ALL_TOP"]:
        m = re.search(rf"^{var}\s*=\s*['\"](.+?)['\"]\s*$", text, re.M)
        if m:
            consts[var] = m.group(1)

    # Find all *_VARIANTS / *_CONFIGS / MIXES / TOPOLOGIES / TOP_TOPOS / CROSS_TOPOS arrays.
    # Arrays whose entries are: tag | bg_list      (defines = inherited consts)
    BG_ONLY = {"MIXES", "TOPOLOGIES", "TOP_TOPOS", "CROSS_TOPOS"}
    # Arrays whose entries are: tag | defines      (bg = same name x3)
    DEF_ONLY = {"MEM_VARIANTS", "CACHE_VARIANTS", "BUS_VARIANTS",
                "POINTER_VARIANTS", "PIPELINE_VARIANTS"}
    # Arrays with: tag | defines | bg
    THREE = {"SINGLE_CONFIGS", "MULTI_CONFIGS", "CONFIGS"}

    arr_pattern = re.compile(
        r"^(SINGLE_CONFIGS|MULTI_CONFIGS|CONFIGS|MIXES|TOPOLOGIES|TOP_TOPOS|CROSS_TOPOS|MEM_VARIANTS|CACHE_VARIANTS|BUS_VARIANTS|POINTER_VARIANTS|PIPELINE_VARIANTS)\s*=\s*\(",
        re.M)
    for m in arr_pattern.finditer(text):
        var = m.group(1)
        entries = parse_pipe_array(text, var)
        for tag, defs, bg in entries:
            tag = tag.strip()
            if not tag:
                continue
            if var in BG_ONLY:
                bg_workers = defs.strip() if defs else None
                # In phase B/E the harness was built once with ALL_DEFS / ALL_BEST.
                full_defs = consts.get("ALL_DEFS",
                              consts.get("ALL_BEST",
                                consts.get("ALL_TOP", ""))).strip()
            elif var in DEF_ONLY:
                full_defs = expand_vars(defs, consts).strip()
                # Determine implicit bg by array prefix.
                stem = var.replace("_VARIANTS", "")
                bg_workers = " ".join([stem] * 3)
            elif var in THREE:
                full_defs = expand_vars(defs, consts).strip()
                # SINGLE_CONFIGS in run_cache_aggressive defaults to a single CACHE bg.
                if var == "SINGLE_CONFIGS":
                    bg_workers = bg.strip() if bg else "CACHE"
                else:
                    bg_workers = bg.strip() if bg else ""
            else:
                full_defs = expand_vars(defs, consts).strip()
                bg_workers = bg.strip() if bg else None
            rows.append((tag, full_defs, bg_workers, var))

    # Heuristics: capture loops like phase A in run_tim_mix3.sh that build
    # tags as A_p${ps}_i${it}.
    if path.name == "run_tim_mix3.sh":
        m = re.search(r"PAGES=\(([^)]+)\)", text)
        n = re.search(r"ITERS=\(([^)]+)\)", text)
        if m and n:
            pages = m.group(1).split()
            iters = n.group(1).split()
            for ps in pages:
                for it in iters:
                    tag = f"A_p{ps}_i{it}"
                    defs = (f"-DMEM_SIZE_MB=8 -DMEM_PAGE_SIZE={ps} "
                            f"-DMEM_OP_RATIO=2521 -DMEM_ITER={it}")
                    rows.append((tag, defs, "MEM MEM MEM", "PHASE_A_LOOP"))
        m = re.search(r"SIZES=\(([^)]+)\)", text)
        if m:
            sizes = m.group(1).split()
            # We don't know which page/iter ended up "best"; record placeholder.
            for sz in sizes:
                tag_glob = f"B_sz{sz}_p<PS>_i<IT>"
                defs = (f"-DMEM_SIZE_MB={sz} -DMEM_PAGE_SIZE=<PS> "
                        f"-DMEM_OP_RATIO=2521 -DMEM_ITER=<IT>")
                rows.append((tag_glob, defs, "MEM MEM MEM", "PHASE_B_LOOP"))

    return rows


def parse_solo(d: Path) -> dict[str, str]:
    """Read solo_<victim>.txt files and return {victim: median_cycles}."""
    out = {}
    for f in sorted(d.glob("solo_*.txt")):
        # File naming variants:
        #   solo_<victim>.txt          (most scripts)
        #   solo_<victim>_<hash>.txt   (tim_mix2 phase A only)
        m = re.match(r"solo_([a-zA-Z0-9_]+?)(?:_[0-9a-f]{6})?\.txt$", f.name)
        if not m:
            continue
        victim = m.group(1)
        try:
            text = f.read_text()
        except UnicodeDecodeError:
            continue
        m2 = re.search(r"^\s*cycles\s+\S+\s+\S+\s+\S+\s+(\S+)", text, re.M)
        if m2:
            out[victim] = m2.group(1)
    return out


def parse_summary(d: Path):
    fp = d / "summary.csv"
    if not fp.exists():
        return None, None, []
    rows = list(csv.reader(fp.read_text().splitlines()))
    if not rows:
        return None, None, []
    hdr = rows[0]
    return hdr[0], hdr[1:], rows[1:]


def resolve_tim_others_winners(rows, summary_path: Path):
    """Replicate run_tim_others.sh phase-E winner picking and substitute
    ALL_BEST into CROSS_TOPOS rows with the actual concatenated defines."""
    if not summary_path.exists():
        return rows
    csv_rows = list(csv.reader(summary_path.read_text().splitlines()))
    if not csv_rows:
        return rows
    hdr = csv_rows[0]
    idx = {h: i for i, h in enumerate(hdr)}
    data = csv_rows[1:]

    def best_by(prefix, victims):
        best = None
        for r in data:
            if not r[0].startswith(prefix):
                continue
            try:
                x = max(float(r[idx[v]]) for v in victims)
            except (ValueError, KeyError):
                continue
            if best is None or x > best[1]:
                best = (r[0], x)
        return best[0] if best else None

    winners = {
        "CACHE": best_by("CA_", ["binarysearch", "insertsort"]),
        "BUS":   best_by("BA_", ["fmref", "adpcm_dec"]),
        "POINTER": best_by("PA_", hdr[1:]),
        "PIPELINE": best_by("PL_", hdr[1:]),
    }
    by_tag = {tag: defs for tag, defs, _bg, _src in rows}
    bits = []
    for k in ["CACHE", "BUS", "POINTER", "PIPELINE"]:
        t = winners[k]
        if t and t in by_tag:
            bits.append(by_tag[t])
    bits.append("-DMEM_SIZE_MB=8 -DMEM_PAGE_SIZE=16384 -DMEM_OP_RATIO=2521 -DMEM_ITER=16")
    all_best = " ".join(bits)

    out = []
    for tag, defs, bg, src in rows:
        if src == "CROSS_TOPOS":
            note = "; winners: " + ", ".join(
                f"{k}={winners[k]}" for k in ["CACHE", "BUS", "POINTER", "PIPELINE"])
            defs = all_best + note
        out.append((tag, defs, bg, src))
    return out



def write_configs(d: Path, script: Path):
    rows = parse_script(script)
    if script.name == "run_tim_others.sh":
        rows = resolve_tim_others_winners(rows, d / "summary.csv")
    by_tag = {tag: (defs, bg, src) for tag, defs, bg, src in rows}

    # Read summary CSV to align with actual measured tags.
    cfg_col, victims, data_rows = parse_summary(d)
    out = d / "configs.tsv"
    with out.open("w") as f:
        cols = ["tag", "phase", "bg_workers", "defines"]
        if victims:
            cols += [f"ratio_{v}" for v in victims]
        f.write("\t".join(cols) + "\n")
        # If we have summary rows, use them; else dump every script row.
        if data_rows:
            for r in data_rows:
                tag = r[0]
                ratios = r[1:]
                defs = ""
                bg = ""
                src = ""
                if tag in by_tag:
                    defs, bg, src = by_tag[tag]
                else:
                    # Try fuzzy: e.g. B_sz16_p2048_i2 matches glob B_sz16_p<PS>_i<IT>
                    for k, (dv, bv, sv) in by_tag.items():
                        if "<PS>" in k and "<IT>" in k:
                            mk = re.match(
                                r"B_sz(\d+)_p<PS>_i<IT>", k)
                            mt = re.match(
                                r"B_sz(\d+)_p(\d+)_i(\d+)", tag)
                            if mk and mt and mk.group(1) == mt.group(1):
                                defs = (dv.replace("<PS>", mt.group(2))
                                          .replace("<IT>", mt.group(3)))
                                bg = bv; src = sv
                                break
                row = [tag, src, bg or "", defs or ""]
                row += list(ratios)
                f.write("\t".join(row) + "\n")
        else:
            for tag, defs, bg, src in rows:
                f.write("\t".join([tag, src, bg or "", defs]) + "\n")
    print(f"wrote {out} ({sum(1 for _ in out.open())-1} rows)")

    # Baseline solo cycles
    solo = parse_solo(d)
    if solo:
        bp = d / "baseline.tsv"
        with bp.open("w") as f:
            f.write("victim\tsolo_cycles_median\n")
            for v in sorted(solo):
                f.write(f"{v}\t{solo[v]}\n")
        print(f"wrote {bp} ({len(solo)} victims)")


def write_global_default(d: Path):
    """Write a top-level note with DEFAULT_CFLAGS and harness invocation."""
    info = d / "harness.txt"
    info.write_text(
        "harness binary       : ./multi_proc_pmu\n"
        "harness flags (typ.) : -n 100 -f 1600000 (locked CPU freq 1.6 GHz)\n"
        f"DEFAULT_CFLAGS       : {DEFAULT_CFLAGS}\n"
        "victim cpu           : cpu0\n"
        "attacker cpus        : cpu1..cpuN (one per positional bg bench)\n"
        "Each row in summary.csv gives ratio = pair_median / solo_median\n"
        "for cycles. configs.tsv adds the exact -D defines used to BUILD\n"
        "the harness for that row, and the bg worker list.\n"
    )
    print(f"wrote {info}")


def main():
    for sub, script_name in SCRIPTS.items():
        d = RESULTS / sub
        s = ROOT / script_name
        if not d.exists():
            print(f"skip {sub}: no results dir")
            continue
        if not s.exists():
            print(f"skip {sub}: missing script {script_name}")
            continue
        write_configs(d, s)
        write_global_default(d)


if __name__ == "__main__":
    main()
