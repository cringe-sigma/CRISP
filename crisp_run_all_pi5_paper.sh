#!/usr/bin/env bash
# Raspberry Pi 5 paper-data one-shot runner.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec bash "$ROOT/scripts/crisp_run_paper_matrix.sh" --board pi5 --freq 2400000 "$@"
