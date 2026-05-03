#!/usr/bin/env bash
# Wait for E4 (e4_resume.py) to finish, then launch E7 case study.
# Detached via setsid so SSH/VS Code disconnect won't kill it.
set -u
ROOT=/home/gjh/CRISP
LOG="$ROOT/experiments/run_log/E7_after_E4.log"
exec >>"$LOG" 2>&1
echo "[chain] $(date -Is) waiting for e4_resume.py to exit..."
while pgrep -f e4_resume.py >/dev/null 2>&1; do
  sleep 60
done
echo "[chain] $(date -Is) E4 ended; launching E7..."
cd "$ROOT" || exit 2
mkdir -p experiments/data/processed
SUDO_PW=gejiahao python3 experiments/07_case_study/e7_case_study.py \
  --duration-s 86400 \
  --R 100 \
  --inject-every 500 \
  --attackers MAX_LLC MAX_BUS MAX_MEM \
  --out-dir experiments/data/processed \
  >>"$ROOT/experiments/run_log/E7_case_study.log" 2>&1
ec=$?
echo "[chain] $(date -Is) E7 exited with $ec"
