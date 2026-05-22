#!/usr/bin/env python3
"""Run a board-side shield activation latency audit.

The paper's Theorem 1' needs an auditable upper bound for the time between
detecting a premise violation and installing the regulating budget.  The CRISP
artifact does not assume that this microbenchmark is the whole shield
implementation.  It records a conservative userspace proxy for the relevant
pieces so the deployment claim has an explicit latency input:

  checkpoint read + predicate evaluation + notification/signal path
  + scheduler handoff proxy + budget-update proxy.

Outputs:
  crisp_data/shield_latency_audit_raw.csv
  crisp_data/shield_latency_audit_summary.csv
"""
from __future__ import annotations

import argparse
import csv
import os
import platform
import statistics
import subprocess
import tempfile
from pathlib import Path


C_SOURCE = r'''
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t seen_signal = 0;
static volatile uint64_t sink = 0;

static inline uint64_t nsec_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void on_usr1(int signo) {
    (void)signo;
    seen_signal++;
}

static uint64_t measure_clock_pair(void) {
    uint64_t t0 = nsec_now();
    (void)nsec_now();
    uint64_t t1 = nsec_now();
    return t1 - t0;
}

static uint64_t measure_predicate(void) {
    uint64_t t0 = nsec_now();
    uint64_t local = sink;
    for (int i = 0; i < 64; ++i) {
        local += (uint64_t)(i * 17);
        if ((local & 0xffu) == 0x5au) {
            local ^= 0x9e3779b97f4a7c15ull;
        }
    }
    sink = local;
    uint64_t t1 = nsec_now();
    return t1 - t0;
}

static uint64_t measure_signal(void) {
    uint64_t t0 = nsec_now();
    raise(SIGUSR1);
    uint64_t t1 = nsec_now();
    return t1 - t0;
}

static uint64_t measure_yield(void) {
    uint64_t t0 = nsec_now();
    sched_yield();
    uint64_t t1 = nsec_now();
    return t1 - t0;
}

static uint64_t measure_affinity_update(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    uint64_t t0 = nsec_now();
    int rc = sched_setaffinity(0, sizeof(set), &set);
    uint64_t t1 = nsec_now();
    if (rc != 0) {
        fprintf(stderr, "sched_setaffinity failed: %s\n", strerror(errno));
        return 0;
    }
    return t1 - t0;
}

int main(int argc, char **argv) {
    int repeats = 10000;
    int cpu = 0;
    if (argc > 1) repeats = atoi(argv[1]);
    if (argc > 2) cpu = atoi(argv[2]);
    if (repeats <= 0) repeats = 10000;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_usr1;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    printf("sample,checkpoint_ns,predicate_ns,signal_ns,yield_ns,budget_update_ns,total_ns\n");
    for (int i = 0; i < repeats; ++i) {
        uint64_t checkpoint = measure_clock_pair();
        uint64_t predicate = measure_predicate();
        uint64_t signal = measure_signal();
        uint64_t yield = measure_yield();
        uint64_t budget = measure_affinity_update(cpu);
        uint64_t total = checkpoint + predicate + signal + yield + budget;
        printf("%d,%llu,%llu,%llu,%llu,%llu,%llu\n",
               i,
               (unsigned long long)checkpoint,
               (unsigned long long)predicate,
               (unsigned long long)signal,
               (unsigned long long)yield,
               (unsigned long long)budget,
               (unsigned long long)total);
    }
    return seen_signal < 0;
}
'''


def percentile(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    idx = min(len(ordered) - 1, max(0, int(round(q * (len(ordered) - 1)))))
    return ordered[idx]


def run_cmd(cmd: list[str], cwd: Path | None = None) -> str:
    return subprocess.check_output(cmd, cwd=cwd, text=True, stderr=subprocess.STDOUT)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-root", default="crisp_data", help="output directory")
    ap.add_argument("--repeats", type=int, default=10000)
    ap.add_argument("--cpu", type=int, default=0)
    ap.add_argument("--freq-khz", type=float, default=1600000.0)
    args = ap.parse_args()

    data_root = Path(args.data_root)
    data_root.mkdir(parents=True, exist_ok=True)
    raw_path = data_root / "shield_latency_audit_raw.csv"
    summary_path = data_root / "shield_latency_audit_summary.csv"

    with tempfile.TemporaryDirectory(prefix="crisp_shield_audit_") as td:
        td_path = Path(td)
        src = td_path / "shield_latency_audit.c"
        exe = td_path / "shield_latency_audit"
        src.write_text(C_SOURCE, encoding="utf-8")
        run_cmd(["gcc", "-O2", "-Wall", "-Wextra", str(src), "-o", str(exe)])
        out = run_cmd([str(exe), str(args.repeats), str(args.cpu)])

    raw_path.write_text(out, encoding="utf-8")
    rows = list(csv.DictReader(out.splitlines()))
    fields = [
        "checkpoint_ns",
        "predicate_ns",
        "signal_ns",
        "yield_ns",
        "budget_update_ns",
        "total_ns",
    ]

    summary_rows = []
    for field in fields:
        vals = [float(r[field]) for r in rows]
        worst_ns = max(vals) if vals else 0.0
        summary_rows.append({
            "board": "imx8mm" if args.freq_khz < 2000000 else "pi5",
            "component": field.replace("_ns", ""),
            "samples": len(vals),
            "min_ns": f"{min(vals):.3f}" if vals else "0",
            "mean_ns": f"{statistics.fmean(vals):.3f}" if vals else "0",
            "p99_ns": f"{percentile(vals, 0.99):.3f}",
            "max_ns": f"{worst_ns:.3f}",
            "max_cycles_at_freq": f"{worst_ns * args.freq_khz / 1_000_000.0:.3f}",
            "method": "userspace_proxy_clock_signal_yield_affinity",
            "kernel": platform.release(),
        })

    with summary_path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=list(summary_rows[0].keys()))
        writer.writeheader()
        writer.writerows(summary_rows)

    total = next(r for r in summary_rows if r["component"] == "total")
    print(
        "[shield-audit] wrote",
        raw_path,
        "and",
        summary_path,
        "total_max_ns=",
        total["max_ns"],
        "total_max_cycles=",
        total["max_cycles_at_freq"],
    )


if __name__ == "__main__":
    main()
