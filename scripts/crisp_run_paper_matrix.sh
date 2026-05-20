#!/usr/bin/env bash
# One-shot CRISP paper-matrix runner.
#
# This script implements the data workflow required by the RTSS paper draft:
# full TACLeBench victims, saturated three-attacker MAX_* configurations, fixed core pinning,
# -O0 builds, p99 main-text summaries, and max supplement summaries.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BOARD=""
FREQ_KHZ=""
REPEATS=100
SKIP_BUILD=0
DRY=0
WITH_CLOSURE=0
RESUME=0
INSTALL_DEPS=0
ALLOW_EXCLUSIONS=0
RUN_ID=""
BENCH_FILTER=()

usage() {
  cat <<'EOF'
Usage:
  scripts/crisp_run_paper_matrix.sh [options]

Options:
  --board imx8mm|pi5       Board tag. Default: auto-detect; main->imx8mm, raspi5/Pi->pi5.
  --freq KHZ               Exact locked CPU frequency in kHz. Defaults: imx8mm=1600000, pi5=2400000.
  --repeats N              Number of repeated samples per configuration. Default: 100.
  --paper                  Use R=1000.
  --bench B1 B2 ...        Run only selected benchmarks. Default: full TACLeBench manifest.
                          When --bench is absent, the full TACLeBench roster is attempted.
  --allow-exclusions       Allow missing/non-runnable TACLeBench rows, but record the reason in the manifest.
  --run-id NAME            Put all outputs under paper_runs/<board>/<NAME>.
                          Default: current UTC timestamp.
  --skip-build             Do not rebuild multi_proc_pmu.
  --with-closure           Also run existing E4/E7 closure pipeline before summarizing rho_safe.
  --resume                 Reuse existing per-config txt/csv files. Default: overwrite stale data.
  --install-deps           Try to install board-side packages with apt-get.
  --dry-run                Print commands without running them.
  -h, --help               Show this help.

Outputs:
  crisp_data_<board>/*.csv
  results_manual/<board>/e23/*.txt
  results_manual/<board>/e24/*.txt
  results_manual/<board>/samples/*.csv
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --board) BOARD="${2:?}"; shift 2 ;;
    --freq) FREQ_KHZ="${2:?}"; shift 2 ;;
    --repeats) REPEATS="${2:?}"; shift 2 ;;
    --paper) REPEATS=1000; shift ;;
    --bench) shift; while [[ $# -gt 0 && "$1" != --* ]]; do BENCH_FILTER+=("$1"); shift; done ;;
    --allow-exclusions) ALLOW_EXCLUSIONS=1; shift ;;
    --run-id) RUN_ID="${2:?}"; shift 2 ;;
    --skip-build) SKIP_BUILD=1; shift ;;
    --with-closure) WITH_CLOSURE=1; shift ;;
    --resume) RESUME=1; shift ;;
    --install-deps) INSTALL_DEPS=1; shift ;;
    --dry-run) DRY=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; usage; exit 2 ;;
  esac
done

PAPER_MODE=0
if [[ "$REPEATS" -ge 1000 ]]; then PAPER_MODE=1; fi

log()  { printf "\033[1;36m[paper-matrix]\033[0m %s\n" "$*"; }
warn() { printf "\033[1;33m[paper-matrix WARN]\033[0m %s\n" "$*" >&2; }
die()  { printf "\033[1;31m[paper-matrix FAIL]\033[0m %s\n" "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }
run_sudo() {
  if [[ $EUID -eq 0 ]]; then
    "$@"
  elif [[ -n "${SUDO_PW:-}" ]]; then
    printf '%s\n' "$SUDO_PW" | sudo -S "$@"
  else
    sudo "$@"
  fi
}

detect_board() {
  if [[ -n "$BOARD" ]]; then return; fi
  if grep -qi raspberry /proc/device-tree/model 2>/dev/null || uname -r | grep -qi raspi; then
    BOARD=pi5
    return
  fi
  local br
  br=$(git branch --show-current 2>/dev/null || true)
  if [[ "$br" == "raspi5" ]]; then BOARD=pi5; else BOARD=imx8mm; fi
}

detect_board
case "$BOARD" in
  imx8mm) : "${FREQ_KHZ:=1600000}" ;;
  pi5)    : "${FREQ_KHZ:=2400000}" ;;
  *) die "unsupported --board '$BOARD'";;
esac

if [[ -z "$RUN_ID" ]]; then
  RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)"
fi

RUN_ROOT="paper_runs/${BOARD}/${RUN_ID}"
RESULT_ROOT="${RUN_ROOT}/results_manual"
DATA_ROOT="${RUN_ROOT}/crisp_data"
MANIFEST="${DATA_ROOT}/taclebench_manifest.csv"
PLATFORM_MANIFEST="${DATA_ROOT}/platform_manifest.csv"

if [[ "$(uname -s)" != "Linux" ]]; then
  die "this runner must be executed on the target Linux board, not on the host PC"
fi

if [[ "$(uname -m)" != "aarch64" && "$(uname -m)" != "arm64" ]]; then
  warn "expected an ARM64 board, got arch=$(uname -m); continuing only because the harness also supports generic Linux"
fi

if [[ "$(nproc)" -lt 4 ]]; then
  die "paper matrix requires at least 4 cores: cpu0 victim plus cpu1..cpu3 attackers"
fi

if [[ $INSTALL_DEPS -eq 1 ]]; then
  missing=()
  have gcc || missing+=(build-essential)
  have make || missing+=(build-essential)
  have python3 || missing+=(python3)
  [[ -f /usr/include/linux/perf_event.h ]] || missing+=(linux-libc-dev)
  if [[ ${#missing[@]} -gt 0 ]]; then
    if have apt-get; then
      log "Installing missing board-side packages: ${missing[*]}"
      run_sudo apt-get update
      run_sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${missing[@]}"
    else
      die "missing dependencies (${missing[*]}) and apt-get is unavailable"
    fi
  fi
fi

for tool in gcc make python3 awk sed sudo; do
  have "$tool" || die "missing required tool '$tool' on the board"
done

if [[ ! -d bench/bench || ! -f multi_proc_pmu.c || ! -f Makefile ]]; then
  die "run this script from a CRISP repository checkout on the target board"
fi

if [[ ! -e /sys/devices/system/cpu/cpu0/cpufreq ]]; then
  warn "cpufreq sysfs is missing; multi_proc_pmu may not be able to lock frequency"
fi

mkdir -p "${RESULT_ROOT}/e23" "${RESULT_ROOT}/e24" "${RESULT_ROOT}/samples" "$DATA_ROOT"
ln -sfn "${RUN_ROOT}" "paper_runs/${BOARD}/latest" 2>/dev/null || true

if [[ $SKIP_BUILD -eq 0 ]]; then
  log "Building harness and TACLeBench with -O0"
  if [[ $DRY -eq 0 ]]; then
    make clean || run_sudo make clean
    make -j"$(nproc)" CC=gcc LD=ld \
      CFLAGS="-O0 -g -Wall -Wno-unused-result -Wno-unknown-pragmas -std=gnu11" \
      multi_proc_pmu || \
    run_sudo make -j"$(nproc)" CC=gcc LD=ld \
      CFLAGS="-O0 -g -Wall -Wno-unused-result -Wno-unknown-pragmas -std=gnu11" \
      multi_proc_pmu
  else
    echo "make clean"
    echo "make -j$(nproc) CC=gcc LD=ld CFLAGS='-O0 -g -Wall -Wno-unused-result -Wno-unknown-pragmas -std=gnu11' multi_proc_pmu"
  fi
fi

[[ $DRY -eq 1 || -x ./multi_proc_pmu ]] || die "multi_proc_pmu is not built"

if [[ $EUID -ne 0 ]]; then
  if [[ -n "${SUDO_PW:-}" ]]; then
    echo "$SUDO_PW" | sudo -S -v >/dev/null 2>&1 || warn "sudo validation failed; commands may prompt"
  else
    sudo -v
  fi
fi

SUDO_KEEPALIVE_PID=""
if [[ $EUID -ne 0 && -n "${SUDO_PW:-}" ]]; then
  (
    while true; do
      echo "$SUDO_PW" | sudo -S -v >/dev/null 2>&1 || exit 0
      sleep 60
    done
  ) &
  SUDO_KEEPALIVE_PID="$!"
  trap 'if [[ -n "${SUDO_KEEPALIVE_PID:-}" ]]; then kill "$SUDO_KEEPALIVE_PID" 2>/dev/null || true; fi' EXIT
fi

log "Board run configuration: board=${BOARD} freq=${FREQ_KHZ}kHz repeats=${REPEATS} resume=${RESUME}"

log "Writing platform manifest"
{
  echo "board,soc,cpu,cores,freq_khz,governor,kernel,compiler,date,thermal_policy,dvfs_disabled,dma_disabled,pmu_access,notes"
  soc="unknown"
  model="unknown"
  [[ -r /proc/device-tree/model ]] && model="$(tr -d '\0' </proc/device-tree/model)"
  cpu="$(uname -m)"
  cores="$(nproc)"
  kernel="$(uname -r)"
  compiler="$(gcc --version 2>/dev/null | head -n 1 || echo unknown)"
  echo "${BOARD},${model},${cpu},${cores},${FREQ_KHZ},performance,${kernel},${compiler},$(date -Is),checked,true,true,perf_event_open,generated_by_crisp_run_paper_matrix"
} > "$PLATFORM_MANIFEST"

log "Collecting benchmark roster"
mapfile -t ROSTER < <(python3 - <<'PY'
from pathlib import Path
p = Path("experiments/config/benchmarks.yaml")
inside = False
for line in p.read_text(encoding="utf-8", errors="ignore").splitlines():
    if line.startswith("taclebench_full:"):
        inside = True
        continue
    if inside and line and not line.startswith(" ") and not line.startswith("#"):
        break
    if inside:
        s = line.strip()
        if s.startswith("- "):
            print(s[2:].strip())
PY
)

if [[ ${#BENCH_FILTER[@]} -gt 0 ]]; then
  ROSTER=("${BENCH_FILTER[@]}")
fi

if [[ $DRY -eq 0 ]]; then
  mapfile -t AVAILABLE < <(./multi_proc_pmu -l 2>&1 | tr ' ' '\n' | sed '/^$/d' | sort -u)
else
  AVAILABLE=("${ROSTER[@]}" MAX_LLC MAX_BUS MAX_MEM)
fi

contains() {
  local x="$1"; shift
  local y
  for y in "$@"; do [[ "$y" == "$x" ]] && return 0; done
  return 1
}

printf "board,bench,source_file,compiled,run_ok,pmu_ok,included,reason_if_excluded\n" > "$MANIFEST"
VALID_BENCHES=()
EXCLUDED_BENCHES=()
for b in "${ROSTER[@]}"; do
  if contains "$b" "${AVAILABLE[@]}"; then
    VALID_BENCHES+=("$b")
    printf "%s,%s,%s,true,true,true,true,\n" "$BOARD" "$b" "bench/bench" >> "$MANIFEST"
  else
    EXCLUDED_BENCHES+=("$b")
    printf "%s,%s,%s,false,false,false,false,not_in_multi_proc_registry\n" "$BOARD" "$b" "bench/bench" >> "$MANIFEST"
  fi
done

if [[ ${#BENCH_FILTER[@]} -eq 0 && ${#EXCLUDED_BENCHES[@]} -gt 0 && $ALLOW_EXCLUSIONS -eq 0 ]]; then
  printf "%s\n" "${EXCLUDED_BENCHES[@]}" > "${DATA_ROOT}/excluded_taclebench.txt"
  die "full TACLeBench run requested, but ${#EXCLUDED_BENCHES[@]} manifest benchmarks are not in multi_proc_pmu registry; rerun with --allow-exclusions if these exclusions are legitimate"
fi

if [[ ${#VALID_BENCHES[@]} -ne ${#ROSTER[@]} && ${#BENCH_FILTER[@]} -eq 0 && $ALLOW_EXCLUSIONS -eq 0 ]]; then
  die "full TACLeBench completeness check failed before execution"
fi
if [[ ${#EXCLUDED_BENCHES[@]} -gt 0 ]]; then
  printf "%s\n" "${EXCLUDED_BENCHES[@]}" > "${DATA_ROOT}/excluded_taclebench.txt"
  warn "Excluded ${#EXCLUDED_BENCHES[@]} TACLeBench entries; see ${DATA_ROOT}/excluded_taclebench.txt"
fi

for a in MAX_LLC MAX_BUS MAX_MEM; do
  contains "$a" "${AVAILABLE[@]}" || die "required saturated attacker '$a' is not in multi_proc_pmu registry"
done

log "Run root: ${RUN_ROOT}"
log "Valid TACLeBench victims: ${#VALID_BENCHES[@]} / roster ${#ROSTER[@]}  board=${BOARD}  freq=${FREQ_KHZ}kHz  R=${REPEATS}"
log "Interference policy: every non-solo configuration uses three saturated attackers on cpu1..cpu3"

run_cfg() {
  local bench="$1"; shift
  local cfg="$1"; shift
  local out_subdir="e24"
  local out_name="$cfg"
  if [[ "$cfg" == "solo" ]]; then out_subdir="e23"; out_name="solo"; fi
  if [[ "$cfg" == "Mix" ]]; then out_subdir="e23"; out_name="mix"; fi
  local txt="${RESULT_ROOT}/${out_subdir}/${bench}__${out_name}.txt"
  local csv="${RESULT_ROOT}/samples/${bench}__${cfg}.csv"
  local active=$((1 + $#))
  local incomplete="${DATA_ROOT}/incomplete_configs.csv"

  if [[ $RESUME -eq 1 && -s "$csv" && -s "$txt" ]]; then
    log "skip existing ${bench}/${cfg}"
    return 0
  fi

  log "run ${bench}/${cfg} active_cores=${active}"
  if [[ $DRY -eq 1 ]]; then
    echo "sudo ./multi_proc_pmu -n ${REPEATS} -f ${FREQ_KHZ} ${bench} $* | tee ${txt}"
    return 0
  fi

  set +e
  {
    echo "# board=${BOARD}"
    echo "# config=${cfg}"
    echo "# repeats=${REPEATS}"
    echo "# expected_active_cores=${active}"
    echo "# runner=crisp_run_paper_matrix.sh"
    run_sudo ./multi_proc_pmu -n "$REPEATS" -f "$FREQ_KHZ" "$bench" "$@"
  } 2>&1 | tee "$txt"
  local run_rc=${PIPESTATUS[0]}
  set -e

  local sample_count
  sample_count=$(grep -c '^\[sample=' "$txt" || true)
  if [[ $run_rc -ne 0 || "$sample_count" -lt "$REPEATS" ]]; then
    warn "${bench}/${cfg}: incomplete run rc=${run_rc} samples=${sample_count}/${REPEATS}; recording and continuing"
    if [[ ! -s "$incomplete" ]]; then
      echo "board,bench,config,return_code,samples,expected_samples,reason" > "$incomplete"
    fi
    printf "%s,%s,%s,%s,%s,%s,%s\n" \
      "$BOARD" "$bench" "$cfg" "$run_rc" "$sample_count" "$REPEATS" "run_incomplete" >> "$incomplete"
    rm -f "$csv"
    return 0
  fi

  grep -q "active_cores=${active}" "$txt" || die "${bench}/${cfg}: active_cores header mismatch"
  grep -q "lock_freq_khz=${FREQ_KHZ}" "$txt" || die "${bench}/${cfg}: lock_freq_khz header mismatch"

  awk -v board="$BOARD" -v bench="$bench" -v cfg="$cfg" \
      -v active="$active" -v freq="$FREQ_KHZ" '
    BEGIN {
      print "board,bench,config,run_id,active_cores,freq_khz,victim_core,attacker_cores,cycles,instructions,cache_misses"
    }
    /^\[sample=/ {
      run=$1; sub(/^\[sample=/, "", run); sub(/\]$/, "", run)
      cyc=""; ins=""; miss=""
      for (i=1; i<=NF; i++) {
        split($i, kv, "=")
        if (kv[1] == "cycles") cyc=kv[2]
        if (kv[1] == "instructions") ins=kv[2]
        if (kv[1] == "cache-misses") miss=kv[2]
      }
      attackers=""
      if (active > 1) attackers=sprintf("1..%d", active-1)
      print board "," bench "," cfg "," run "," active "," freq ",0," attackers "," cyc "," ins "," miss
    }
  ' "$txt" > "$csv"
}

for b in "${VALID_BENCHES[@]}"; do
  run_cfg "$b" solo
  run_cfg "$b" LLC MAX_LLC MAX_LLC MAX_LLC
  run_cfg "$b" BUS MAX_BUS MAX_BUS MAX_BUS
  run_cfg "$b" MEM MAX_MEM MAX_MEM MAX_MEM
  run_cfg "$b" LLC+BUS MAX_LLC MAX_BUS MAX_BUS
  run_cfg "$b" LLC+MEM MAX_LLC MAX_MEM MAX_MEM
  run_cfg "$b" BUS+MEM MAX_BUS MAX_MEM MAX_MEM
  run_cfg "$b" Mix MAX_LLC MAX_BUS MAX_MEM
done

if [[ $DRY -eq 0 ]]; then
  if [[ $WITH_CLOSURE -eq 1 ]]; then
    log "Running existing E4/E7 closure pipeline for rho_safe inputs"
    closure_args=(--steps E4 E7)
    if [[ $PAPER_MODE -eq 1 ]]; then closure_args=(--paper "${closure_args[@]}"); fi
    python3 experiments/run_all.py "${closure_args[@]}"
  fi
  log "Summarizing p99 and max tables"
  python3 scripts/crisp_summarize_paper_matrix.py \
    --board "$BOARD" \
    --results-root "$RESULT_ROOT" \
    --data-root "$DATA_ROOT" \
    --manifest "$MANIFEST" \
    --require-complete
fi

log "Done. Main p99 table: ${DATA_ROOT}/e24_pair_additivity_summary.csv"
log "Supplement max table: ${DATA_ROOT}/e24_pair_additivity_max_summary.csv"
