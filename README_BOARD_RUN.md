# Board-only execution note

The paper-data scripts in this checkout are intended to be executed on the
target board, not on the host PC.

## i.MX 8M Mini main branch

Run on the i.MX 8M Mini board:

```bash
git clone https://github.com/cringe-sigma/CRISP.git
cd CRISP
git checkout main

bash crisp_run_all_imx8_paper.sh --install-deps --paper --with-closure
```

Quick smoke run:

```bash
bash crisp_run_all_imx8_paper.sh --repeats 100 --bench fac jfdctint
```

If a small number of TACLeBench programs cannot be built or registered on the
board, rerun with an explicit exclusion policy:

```bash
bash crisp_run_all_imx8_paper.sh --install-deps --paper --with-closure --allow-exclusions
```

Expected outputs:

```text
paper_runs/imx8mm/<run-id>/crisp_data/platform_manifest.csv
paper_runs/imx8mm/<run-id>/crisp_data/taclebench_manifest.csv
paper_runs/imx8mm/<run-id>/crisp_data/e24_pair_additivity_summary.csv
paper_runs/imx8mm/<run-id>/crisp_data/e24_pair_additivity_max_summary.csv
paper_runs/imx8mm/<run-id>/crisp_data/e23_mix_amplification_summary.csv
paper_runs/imx8mm/<run-id>/crisp_data/rho_safe_closure_summary.csv
paper_runs/imx8mm/<run-id>/results_manual/e23/*.txt
paper_runs/imx8mm/<run-id>/results_manual/e24/*.txt
paper_runs/imx8mm/<run-id>/results_manual/samples/*.csv
paper_runs/imx8mm/latest -> <run-id>
```

Do not run this workflow on a desktop host and copy the results into the
artifact. PMU readings, locked frequency, active-core isolation, and saturated
attacker behavior must all come from the target board.

When `--bench` is not specified, the script attempts the full TACLeBench roster
from `experiments/config/benchmarks.yaml`. Individual exclusions are acceptable
only if they are recorded in `taclebench_manifest.csv`; without
`--allow-exclusions`, missing registry entries fail the run. After execution,
any included victim lacking one of the required eight configurations fails the
summary step.

The experiment policy is:

```text
solo      = victim only
LLC       = victim + MAX_LLC + MAX_LLC + MAX_LLC
BUS       = victim + MAX_BUS + MAX_BUS + MAX_BUS
MEM       = victim + MAX_MEM + MAX_MEM + MAX_MEM
LLC+BUS   = victim + MAX_LLC + MAX_BUS + MAX_BUS
LLC+MEM   = victim + MAX_LLC + MAX_MEM + MAX_MEM
BUS+MEM   = victim + MAX_BUS + MAX_MEM + MAX_MEM
Mix       = victim + MAX_LLC + MAX_BUS + MAX_MEM
```

Thus every non-solo row is a saturated 4-core run: one measured victim plus
three attackers.
