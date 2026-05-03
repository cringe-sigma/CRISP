#!/usr/bin/env bash
# crisp_run_all.sh ！ One-shot reproducer for CRISP / RAMPART experiments.
#
# Tested on  : i.MX 8M Mini EVK (Ubuntu 20.04, aarch64, 4xCortex-A53)
# Should run : Raspberry Pi 4/5, any aarch64/x86_64 Linux box with cpufreq
#              + perf_event_open + ARMv8 PMU (or x86 perf) + sudo.
#
# Usage:
#     ./crisp_run_all.sh            # quick mode (R=100, durations scaled down)
#     ./crisp_run_all.sh --paper    # paper sizes (R=1000, 24h E4/E7)
#     ./crisp_run_all.sh --steps E1 E2 E3
#     SUDO_PW=mypw ./crisp_run_all.sh
#
# What it does:
#   1. Detect platform (cores, arch, distro) and check prereqs.
#   2. Install missing apt packages (build-essential, python3, cpufrequtils).
#   3. Build the harness:  sudo make multi_proc_pmu
#   4. Sanity-check PMU + cpufreq + hot-unplug capability.
#   5. Run experiments/run_all.py with chosen scale.
#   6. Collect outputs under experiments/data/processed/ and summarise.
#
# Outputs:
#   experiments/data/processed/*.csv   ！ every E* result table
#   experiments/run_log/*.log          ！ per-step append-only logs
#   crisp_summary.txt                  ！ at-a-glance result summary
#
set -uo pipefail

# ---------- argument parsing ----------
MODE_PAPER=0
ONLY_BUILD=0
SKIP_BUILD=0
DRY=0
STEPS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --paper)       MODE_PAPER=1; shift ;;
    --steps)       shift; while [[ $# -gt 0 && "$1" != --* ]]; do STEPS+=("$1"); shift; done ;;
    --build-only)  ONLY_BUILD=1; shift ;;
    --skip-build)  SKIP_BUILD=1; shift ;;
    --dry-run)     DRY=1; shift ;;
    -h|--help)
      sed -n '2,30p' "$0"; exit 0 ;;
    *) echo "unknown arg: $1"; exit 2 ;;
  esac
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

log()  { printf "\033[1;36m[crisp]\033[0m %s\n" "$*"; }
warn() { printf "\033[1;33m[crisp WARN]\033[0m %s\n" "$*" >&2; }
die()  { printf "\033[1;31m[crisp FAIL]\033[0m %s\n" "$*" >&2; exit 1; }

# ---------- 1. platform detection ----------
log "== Platform =="
ARCH=$(uname -m)
KREL=$(uname -r)
NCPU=$(nproc)
DISTRO=$(. /etc/os-release && echo "$PRETTY_NAME" 2>/dev/null || echo unknown)
log "arch=$ARCH  kernel=$KREL  cores=$NCPU  distro=$DISTRO"
if [[ "$ARCH" != "aarch64" && "$ARCH" != "x86_64" ]]; then
  warn "Untested arch '$ARCH'; PMU events may need tweaking."
fi
if [[ $NCPU -lt 4 ]]; then
  warn "Detected only $NCPU CPU(s); RAMPART expects >=4 (1 victim + 3 attackers)."
  warn "Will still run, but mix configs may degrade to triple-attacker on whatever's available."
fi
[[ -e /sys/devices/system/cpu/cpu0/cpufreq ]] \
  || warn "cpufreq sysfs missing; -f flag will be a no-op."
[[ -r /proc/sys/kernel/perf_event_paranoid ]] \
  || warn "perf_event_paranoid not readable."

# ---------- 2. install prereqs ----------
need_pkgs=()
have() { command -v "$1" >/dev/null 2>&1; }
have gcc          || need_pkgs+=(build-essential)
have make         || need_pkgs+=(build-essential)
have python3      || need_pkgs+=(python3)
have cpupower     || need_pkgs+=(linux-tools-common linux-tools-generic cpufrequtils)
[[ -f /usr/include/linux/perf_event.h ]] || need_pkgs+=(linux-libc-dev)

if [[ ${#need_pkgs[@]} -gt 0 ]]; then
  log "== Installing prereqs: ${need_pkgs[*]} =="
  if [[ $DRY -eq 0 ]]; then
    sudo apt-get update -qq
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${need_pkgs[@]}" \
      || warn "Some packages failed to install; build may still succeed."
  fi
fi

# ---------- 3. perf_event paranoia ----------
if [[ -r /proc/sys/kernel/perf_event_paranoid ]]; then
  P=$(cat /proc/sys/kernel/perf_event_paranoid)
  if [[ "$P" -gt 1 ]]; then
    log "Lowering perf_event_paranoid from $P to 1 (needs root)..."
    [[ $DRY -eq 0 ]] && echo 1 | sudo tee /proc/sys/kernel/perf_event_paranoid >/dev/null
  fi
fi

# ---------- 4. build ----------
if [[ $SKIP_BUILD -eq 0 ]]; then
  log "== Build multi_proc_pmu + benches =="
  if [[ $DRY -eq 0 ]]; then
    sudo make -j"$NCPU" multi_proc_pmu || die "make failed"
  fi
fi
[[ -x ./multi_proc_pmu ]] || die "multi_proc_pmu not built"
[[ $ONLY_BUILD -eq 1 ]] && { log "build-only requested; done."; exit 0; }

# ---------- 5. PMU sanity check ----------
log "== PMU sanity check (one-shot fac SOLO, n=5) =="
if [[ $DRY -eq 0 ]]; then
  if ! sudo ./multi_proc_pmu -n 5 -f 0 fac 2>&1 | tee /tmp/crisp_smoke.txt | grep -q '^  cycles '; then
    warn "Smoke test did not produce a 'cycles' line. Check perf_event support / sudo."
  fi
fi

# ---------- 6. run experiments ----------
mkdir -p experiments/data/processed experiments/run_log

EXTRA=()
if [[ $MODE_PAPER -eq 1 ]]; then
  EXTRA+=(--paper)
  log "MODE: paper sizes (R=1000, 24h E4/E7) ！ total runtime ~3-4 days."
else
  log "MODE: quick (R=100, smoke E4/E7 durations) ！ total runtime ~3-5 hours."
fi

# Resolve sudo password into env so each python step can use it.
if [[ -z "${SUDO_PW:-}" ]]; then
  printf "[crisp] enter sudo password (leave blank to fail-fast): "
  read -rs SUDO_PW; echo
  export SUDO_PW
fi
[[ -n "$SUDO_PW" ]] && sudo -k && echo "$SUDO_PW" | sudo -S -v

EXTRA+=(--sudo-pw "$SUDO_PW")

if [[ ${#STEPS[@]} -gt 0 ]]; then
  EXTRA+=(--steps "${STEPS[@]}")
else
  EXTRA+=(--all)
fi

log "== Launch experiments/run_all.py ${EXTRA[*]} =="
[[ $DRY -eq 1 ]] && { log "dry-run: not launching"; exit 0; }

START_TS=$(date -Is)
python3 experiments/run_all.py "${EXTRA[@]}" \
  2>&1 | tee experiments/run_log/run_all_$(date +%Y%m%dT%H%M%SZ).log
RC=${PIPESTATUS[0]}
END_TS=$(date -Is)

# ---------- 7. summary ----------
SUMM=crisp_summary.txt
{
  echo "CRISP run summary"
  echo "started_utc : $START_TS"
  echo "finished_utc: $END_TS"
  echo "exit_code   : $RC"
  echo "host        : $(hostname)  arch=$ARCH  cores=$NCPU"
  echo "mode        : $([[ $MODE_PAPER -eq 1 ]] && echo paper || echo quick)"
  echo
  echo "== outputs (experiments/data/processed/) =="
  (cd experiments/data/processed && for f in *.csv; do
     [[ -e $f ]] || continue
     n=$(($(wc -l <"$f")-1))
     printf "  %-40s rows=%d  size=%s\n" "$f" "$n" "$(du -h "$f" | cut -f1)"
   done)
  echo
  echo "== run_log =="
  ls -la experiments/run_log/ | tail -20
} | tee "$SUMM"

log "Done. See $SUMM and experiments/data/processed/."
exit "$RC"
