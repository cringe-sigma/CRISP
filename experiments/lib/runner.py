"""Shared CLI/run-logging utilities for experiments E1..E9.

Every experiment script imports `start_step` at the top and uses
`run_logged(cmd)` instead of `subprocess.run()`. This guarantees each
command, its full argv, stdout, stderr, exit code, wall-clock and the
sha256 of every input/output file is appended to the per-step log under
`experiments/run_log/<step_id>__<UTC-ISO>.log`. The same record is also
mirrored to `experiments/run_log/manifest.jsonl` for machine-readable
post-mortem.
"""
from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import json
import os
import shlex
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOG_DIR = ROOT / "run_log"
DATA_RAW = ROOT / "data" / "raw"
DATA_PROC = ROOT / "data" / "processed"
FIG_DIR = ROOT / "figs"
LOG_DIR.mkdir(parents=True, exist_ok=True)
DATA_RAW.mkdir(parents=True, exist_ok=True)
DATA_PROC.mkdir(parents=True, exist_ok=True)
FIG_DIR.mkdir(parents=True, exist_ok=True)

_STATE = {"step": None, "log_path": None, "manifest": LOG_DIR / "manifest.jsonl"}


def _now_iso() -> str:
    return _dt.datetime.utcnow().strftime("%Y%m%dT%H%M%SZ")


def sha256(path: str | os.PathLike) -> str:
    p = Path(path)
    if not p.exists() or not p.is_file():
        return ""
    h = hashlib.sha256()
    with p.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def start_step(step_id: str, params: dict | None = None,
               inputs: list[str] | None = None) -> Path:
    """Begin a step.  Creates a timestamped per-step log and records
    parameters + sha256 of every declared input. Returns the log path.
    """
    ts = _now_iso()
    log = LOG_DIR / f"{step_id}__{ts}.log"
    _STATE["step"] = step_id
    _STATE["log_path"] = log
    rec = {
        "step": step_id,
        "started_utc": ts,
        "host": os.uname().nodename,
        "cwd": str(Path.cwd()),
        "argv": sys.argv,
        "params": params or {},
        "inputs": [{"path": p, "sha256": sha256(p)} for p in (inputs or [])],
    }
    with log.open("w") as f:
        f.write(f"# step={step_id}\n# started_utc={ts}\n")
        f.write(f"# argv={shlex.join(sys.argv)}\n")
        f.write(f"# params={json.dumps(rec['params'], ensure_ascii=False)}\n")
        for ip in rec["inputs"]:
            f.write(f"# input  {ip['sha256']}  {ip['path']}\n")
        f.write("# ---- begin step ----\n")
    with _STATE["manifest"].open("a") as f:
        f.write(json.dumps({"event": "start", **rec}) + "\n")
    return log


def end_step(outputs: list[str] | None = None, status: str = "ok",
             extra: dict | None = None) -> None:
    log = _STATE["log_path"]
    if log is None:
        return
    rec_out = [{"path": p, "sha256": sha256(p)} for p in (outputs or [])]
    with log.open("a") as f:
        f.write("# ---- end step ----\n")
        f.write(f"# status={status}\n")
        for o in rec_out:
            f.write(f"# output {o['sha256']}  {o['path']}\n")
        f.write(f"# finished_utc={_now_iso()}\n")
    with _STATE["manifest"].open("a") as f:
        f.write(json.dumps({"event": "end", "step": _STATE["step"],
                            "status": status, "outputs": rec_out,
                            "extra": extra or {},
                            "finished_utc": _now_iso()}) + "\n")


def run_logged(cmd, *, check: bool = True, capture: bool = True,
               cwd: str | None = None, env: dict | None = None,
               input_data: bytes | None = None,
               echo: bool = True) -> subprocess.CompletedProcess:
    """Run a shell command and append the full record to the step log."""
    log = _STATE["log_path"]
    cmd_list = cmd if isinstance(cmd, list) else shlex.split(cmd)
    pretty = shlex.join(cmd_list)
    t0 = time.time()
    if echo:
        sys.stderr.write(f"[run] {pretty}\n")
    try:
        cp = subprocess.run(cmd_list, check=False,
                            capture_output=capture,
                            cwd=cwd, env=env, input=input_data)
    except FileNotFoundError as e:
        cp = subprocess.CompletedProcess(cmd_list, 127,
                                         stdout=b"", stderr=str(e).encode())
    dt = time.time() - t0
    if log is not None:
        with log.open("ab") as f:
            f.write(f"\n$ {pretty}\n".encode())
            if cp.stdout:
                f.write(b"--- stdout ---\n"); f.write(cp.stdout)
                if not cp.stdout.endswith(b"\n"):
                    f.write(b"\n")
            if cp.stderr:
                f.write(b"--- stderr ---\n"); f.write(cp.stderr)
                if not cp.stderr.endswith(b"\n"):
                    f.write(b"\n")
            f.write(f"--- exit={cp.returncode} wall={dt:.3f}s ---\n".encode())
    if check and cp.returncode != 0:
        raise subprocess.CalledProcessError(cp.returncode, cmd_list,
                                            cp.stdout, cp.stderr)
    return cp


def parse_args(description: str = "") -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=description)
    p.add_argument("--out-dir", default=str(DATA_PROC),
                   help="processed-data output directory")
    p.add_argument("--raw-dir", default=str(DATA_RAW),
                   help="raw-data output directory")
    p.add_argument("--reps", type=int, default=None,
                   help="override repetition count (R)")
    p.add_argument("--dry-run", action="store_true",
                   help="print plan only, do not invoke board")
    return p
