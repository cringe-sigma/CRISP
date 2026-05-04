"""perf stat output parser + driver.

`run_perf_stat` invokes:
    perf stat -x , -e <events> -- taskset -c <core> chrt -f 80 <prog>
and returns a dict {event_name: value}. Unsupported events show as 0.
"""
from __future__ import annotations
import os
import shutil
import subprocess
from pathlib import Path

from .runner import run_logged


def perf_path() -> str:
    p = shutil.which("perf")
    if not p:
        raise RuntimeError("perf not found in PATH")
    return p


def run_perf_stat(events: list[str], cmd: list[str], *, cpu: int,
                  rt_prio: int = 80, sudo_pw: str | None = None,
                  timeout: float | None = None) -> dict[str, float]:
    """Run `perf stat -x,` capturing one record per event."""
    ev = ",".join(events)
    pinned = ["taskset", "-c", str(cpu)]
    pinned += ["chrt", "-f", str(rt_prio)]
    pinned += list(cmd)
    full = ["perf", "stat", "-x", ",", "-e", ev, "--"] + pinned
    if sudo_pw:
        full = ["sudo", "-S", "--"] + full
    cp = run_logged(full, check=False,
                    input_data=(sudo_pw + "\n").encode() if sudo_pw else None)
    out = (cp.stderr or b"").decode("utf-8", "replace")
    counts: dict[str, float] = {e: 0.0 for e in events}
    for line in out.splitlines():
        # csv: <count>,<unit>,<event>,<runtime_ns>,<pct>,...
        parts = line.split(",")
        if len(parts) < 3:
            continue
        try:
            v = float(parts[0])
        except ValueError:
            continue
        ev_name = parts[2].strip()
        for e in events:
            if ev_name == e or ev_name.endswith("/" + e) or ev_name == e.strip("r"):
                counts[e] = v
                break
    return counts
