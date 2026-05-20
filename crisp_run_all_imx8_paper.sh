#!/usr/bin/env bash
# i.MX 8M Mini paper-data one-shot runner.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec bash "$ROOT/scripts/crisp_run_paper_matrix.sh" --board imx8mm --freq 1600000 "$@"
