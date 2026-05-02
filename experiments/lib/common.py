"""Common helpers shared across experiments."""
from __future__ import annotations
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BENCH_DIR = ROOT / "bench" / "bench"
BUILD_DIR = ROOT / "build"
MULTI_PROC_PMU = ROOT / "multi_proc_pmu"


def list_local_benches() -> list[str]:
    if not BUILD_DIR.exists():
        return []
    return sorted(p.name for p in BUILD_DIR.iterdir() if p.is_dir())


def parse_benchmarks_yaml() -> dict:
    from .platform import _parse_simple_yaml
    p = ROOT / "experiments" / "config" / "benchmarks.yaml"
    return _parse_simple_yaml(p.read_text())


_CYCLES_LINE = re.compile(
    r"^\s*cycles\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s*$", re.M)
_INST_LINE = re.compile(
    r"^\s*instructions\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s*$", re.M)
_MISS_LINE = re.compile(
    r"^\s*cache-misses\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s*$", re.M)


def parse_pmu_output(text: str) -> dict:
    """Parse multi_proc_pmu stdout, returning min/max/avg/median per metric."""
    out: dict = {}
    for label, pat in (("cycles", _CYCLES_LINE),
                        ("instructions", _INST_LINE),
                        ("cache_misses", _MISS_LINE)):
        m = pat.search(text)
        if m:
            try:
                out[label] = {
                    "min": float(m.group(1)),
                    "max": float(m.group(2)),
                    "avg": float(m.group(3)),
                    "median": float(m.group(4)),
                }
            except ValueError:
                pass
    return out


def run_pmu(victim: str, attackers: list[str], *, n: int = 8,
            freq_khz: int = 1600000, sudo_pw: str | None = None,
            timeout: float | None = None) -> dict:
    """Invoke multi_proc_pmu and return parsed metrics + raw stdout."""
    cmd = [str(MULTI_PROC_PMU), "-n", str(n), "-f", str(freq_khz),
           victim] + list(attackers)
    if sudo_pw is not None:
        cmd = ["sudo", "-S", "--"] + cmd
    cp = subprocess.run(cmd,
                        input=(sudo_pw + "\n").encode() if sudo_pw else None,
                        capture_output=True, timeout=timeout, check=False)
    txt = cp.stdout.decode("utf-8", "replace")
    txt += "\n--STDERR--\n" + cp.stderr.decode("utf-8", "replace")
    parsed = parse_pmu_output(txt)
    return {"cmd": cmd, "rc": cp.returncode, "raw": txt,
            "metrics": parsed, "n": n, "freq_khz": freq_khz,
            "victim": victim, "attackers": list(attackers)}
