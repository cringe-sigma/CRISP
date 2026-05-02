# RAMPART ÊµÑé¸´ÏÖÊÖ²á£¨REPRO.md£©

> ÅäÌ× [`experiment-plan.md`](../experiment-plan.md)£»±¾»úÎª i.MX 8M Mini EVK£¬ÒÑ±àÒë `multi_proc_pmu` Óë `build/<bench>/`£¬Æ½Ì¨ÃèÊö¼û [`config/platform_imx8mm.yaml`](config/platform_imx8mm.yaml)¡£
>
> **ÅÜÁ¿Ëõ·Å£¨ÓÃ»§ÒªÇó 2026-05-01£©**£ºÂÛÎÄ R=1000 ¡ú ÕâÀïÄ¬ÈÏ R=100£»ÂÛÎÄ R=100 ¡ú ÕâÀïÄ¬ÈÏ R=20£»ÆäËûÈýÔª×é²ÉÑùÊý 200/500 ²»±ä¡£ÈçÐè»¹Ô­ÎªÂÛÎÄ¹æÄ££¬¸øËùÓÐ E2/E3/E4/E7 ½Å±¾¼Ó `--R 1000` »ò `--paper`¡£
> **»·¾³±äÁ¿**£º`export SUDO_PW=gejiahao` ºóËùÓÐ½Å±¾ÎÞÐè½»»¥¡£

## 0. ¹²ÓÃÇ°ÖÃ£¨Ò»´ÎÐÔ£©

| # | ÃüÁî | ËµÃ÷ / ÆÚÍû²ú³ö |
|---|---|---|
| 0.1 | `make multi_proc_pmu` | ÔÚ²Ö¿â¸ù²úÉú `multi_proc_pmu` ¿ÉÖ´ÐÐÎÄ¼þ |
| 0.2 | `make benches` »ò²Î¿¼ [`Makefile`](../Makefile) | ÔÚ `build/<bench>/<bench>.o` ÏÂÉú³ÉÈ«²¿ ELF/.o |
| 0.3 | `python3 experiments/lib/platform.py \| head` | ×Ô¼ìÆ½Ì¨ yaml ½âÎö |
| 0.4 | `export SUDO_PW=gejiahao` | ¹Ø±Õ sudo ½»»¥ |
| 0.5 | `cd experiments && python3 run_all.py --dry-run --all` | ´òÓ¡ 16 ¸ö²½ÖèµÄÃüÁîÐÐ£¨²»Ö´ÐÐ£© |

Ã¿¸ö½Å±¾»áÔÚ [`run_log/<step>__<UTC>.log`](run_log) Ð´Èë£ºargv¡¢²ÎÊý JSON¡¢ÊäÈëÎÄ¼þ sha256¡¢×Ó½ø³Ì stdout/stderr/exitcode/wall¡¢Êä³öÎÄ¼þ sha256£»Í¬Ê±×·¼Ó [`run_log/manifest.jsonl`](run_log/manifest.jsonl)£¬±ãÓÚ `jq` ¸´ÅÌ¡£

---

## E1 ¡ª Phase-1 ¾²Ì¬Ô¤É¸

| ²½Öè | ÃüÁî | Ô¤ÆÚÊäÈë | Êä³ö | Ð£Ñé |
|---|---|---|---|---|
| E1.a | `python3 experiments/01_phase1/extract_loopbounds.py` | `bench/bench/*` | `data/processed/loopbounds.json` | `jq 'keys\|length' data/processed/loopbounds.json` ¡Ý 8£¨½ö bench ×Ó¼¯ÒÑ´ø _Pragma loopbound£© |
| E1.b | `python3 experiments/01_phase1/static_estimates.py` | `loopbounds.json` + `build/*/*.o` | `data/processed/static_estimates.csv` | `wc -l` ¡Ý 70£»ÁÐº¬ `alpha_l2_static` |

ÅÜÁ¿£º< 1 ·ÖÖÓ£¨Ö÷»ú¶Ë£©¡£

---

## E2 ¡ª Phase-2 PMC ²É¼¯

> ¹²ÓÃ²ÎÊý£º`--R 100`£¨ÒÑÊÇÄ¬ÈÏ£»`--paper` ¸Ä»Ø 1000£©£¬`--freq-khz` Ä¬ÈÏÈ¡ `platform_imx8mm.yaml` µÄ 1.6 GHz¡£  
> È«²¿½Å±¾¶¼»á°Ñ multi_proc_pmu Ô­Ê¼ stdout Ð´Èë `data/raw/e2x/...`¡£

### E2.1 stress ÑéÖ¤

```
python3 experiments/02_phase2/e21_stress_validate.py
```
- Êä³ö£º`data/processed/e21_stress_validation.csv`¡¢`figs/e21_stress_validation.svg`
- Ð£Ñé£ºÃ¿¸ö channel£¨LLC/BUS/MEM£©ËùÑ¡ `is_pick` ÐÐµÄ `rate_miss_per_cycle / peak >= 0.85`£¨½Å±¾ stderr »á´òÓ¡ `[acc]` ÐÐ£©¡£

### E2.2 adversary search£¨Algorithm 1£¬ËõË®£©

```
python3 experiments/02_phase2/e22_adversary_search.py \
    --R 100 --max-rounds 2 --epsilon 0.02
```
- Êä³ö£º`data/processed/e22_adversary_optimal_theta.csv`
- Ã¿ÐÐÒ»¸ö (bench, channel) -> theta_star + alpha_max + lipschitz£»ÂÛÎÄÒªÇó L ¡Ü 0.42£¨A53£©¡£

### E2.3 ÎåÅäÖÃ PMC È«É¨

```
python3 experiments/02_phase2/e23_five_config_sweep.py --R 100
```
- ÒÀÀµ£ºE2.2 Êä³ö£¨ÎÞÔò fallback µ½ `benchmarks.yaml::channel_attackers`£©
- Êä³ö£º`data/raw/e23/<bench>__<config>.txt`¡¢`data/processed/e23_pmc_aggregated.csv`¡¢`e23_feature_vectors.csv`¡¢`e23_amplification_scalars.csv`
- ¹À¼Æ°åÊ±£¨53 bench ¡Á 5 config ¡Á R=100£©£ºÔ¼ 8 h¡£

### E2.4 Lemma 1a

```
python3 experiments/02_phase2/e24_lemma1a.py --R 100 --n-pairs 159 --seed 42
```
- Êä³ö£º`data/processed/e24_lemma1a.csv`£¬ÁÐÎ² `violation` Ó¦È« False¡£

### E2.5 Lemma 2

```
python3 experiments/02_phase2/e25_lemma2.py --R 100 --n-triples 500 --seed 42
```
- Êä³ö£º`data/processed/e25_lemma2.csv`£¬×îÄ©»ã×Ü `violations`/500 Ó¦Îª 0¡£

---

## E3 ¡ª Bound ¼ÆËã / Spot Checks / ¼ÓÐÔ·´Àý

| ²½Öè | ÃüÁî | ÊäÈë | Êä³ö |
|---|---|---|---|
| E3 | `python3 experiments/03_bounding/interference_bound.py` | `e23_pmc_aggregated.csv` + `e23_amplification_scalars.csv` | `data/processed/e3_bounds.csv`¡¢`figs/e3_bounds_cdf.svg` |
| E3.1 | `python3 experiments/03_bounding/e31_polyrhythm_compare.py --R 100 --n-benches 10 --seed 42` | `e22_adversary_optimal_theta.csv` | `data/processed/e31_polyrhythm_compare.csv`£¨Íâ²¿ polyrhythm È±Ê§Ê±ÓÃ simulated baseline£© |
| E3.2 | `python3 experiments/03_bounding/e32_mempol_compare.py --bench fir2dim --R 20 --trials 20` | (ÎÞ) | `data/processed/e32_mempol_compare.csv`£¨`mempol_latency_us_lit` ÊÇÂÛÎÄ³£Á¿£© |
| E3.3 | `python3 experiments/03_bounding/e33_additive_violations.py --R 100 --max-triples 20` | `e3_bounds.csv` | `data/processed/e33_additive_violations.csv`¡¢`figs/e33_additive_scatter.svg` |

E3 ÀëÏß£¬Ãë¼¶£»E3.3 Ä¬ÈÏÃ¿ victim ³é 20 ¸öÈýÔª×é£¨Ô¼ 53¡Á20 ¡Ö 1060 ´Î multi_proc_pmu£¬3-5 h °åÊ±£©¡£ÂÛÎÄ 24 h È«Ã¶¾ÙÇë¼Ó `--max-triples 0 --time-budget-s 86400`¡£

---

## E4 ¡ª 24h Safety Histogram

```
python3 experiments/04_24h/e4_safety_histogram.py \
    --duration-s 60 --R 100  # smoke
python3 experiments/04_24h/e4_safety_histogram.py \
    --duration-s 86400 --R 100 \
    --case-study fir2dim fmref cosf test3 jfdctint fac
```
- Êä³ö£º`data/processed/e4_safety_histogram.csv`¡¢`figs/e4_safety_histogram.svg`
- Ð£Ñé£º`violations` ×Ö¶Î¼ÆÊýÎª 0£¨Lemma 1b ÔÚÏß¼àÊÓÃüÖÐ 0£©¡£

---

## E5 ¡ª Ablation

```
python3 experiments/05_ablation/e5_ablation.py
```
- ÀëÏß£¬Ãë¼¶£»Êä³ö `data/processed/e5_ablation.csv`£¬stderr ´òÓ¡ 5 ÐÐ medians¡£
- ÂÛÎÄÆÚÍû£¨i.MX£©ÖÐÎ» full_iso ¡Ö 1.21 / rampart_full ¡Ö 1.04¡£

---

## E6 ¡ª µ÷¶ÈÆÀ¹À

```
python3 experiments/06_scheduling/e6_schedule.py \
    --n-sets 200 --m-cores 4 --seed 42
```
- Ö÷»úÀëÏß£¬Ô¼ 30 s£¨greedy ILP Ìæ´ú Gurobi£»½Ó PuLP ¼û½Å±¾×¢ÊÍ£©¡£
- Êä³ö£º`data/processed/e6_scheduling.csv`¡¢`figs/e6_schedulability.svg`¡¢`figs/e6_ilp_runtime.svg`¡£

---

## E7 ¡ª Case Study + ¹ÊÕÏ×¢Èë

```
python3 experiments/07_case_study/e7_case_study.py \
    --duration-s 120 --inject-every 20 --R 100
```
- Êä³ö£º`data/processed/e7_case_study.csv`£¬×îÄ© `extra` ×Ö¶Î¼ÇÂ¼ `misses=0`¡¢`recoveries==injections`¡£
- ÂÛÎÄ¹æÄ££º`--duration-s 86400`¡£

---

## E8 ¡ª RTC ¶ÔÕÕ£¨ÀëÏß stub£©

```
python3 experiments/08_rtc/e8_rtc.py --n-spot 10
```
- Êä³ö£º`data/processed/e8_rtc.csv`£¬`delta_pct` ÖÐÎ»Ó¦ÔÚ ¡À5% ÄÚ¡£

---

## E9 ¡ª Zephyr Ç¨ÒÆ£¨stub£©

```
python3 experiments/09_zephyr/e9_zephyr.py --n-spot 10
```
- ½öÕ¼Î»£»µ± Zephyr ¶Ë¿ÚÂäµØºó°Ñ `rampart_zephyr` ÁÐ¸ÄÎªÊµ²âÖµ²¢ÔÙÅÜÒ»´Î¡£

---

## Ò»¼ü¸´ÏÖ

```
export SUDO_PW=gejiahao
cd experiments
python3 run_all.py --all                # ËõË®°æ£¨Ä¬ÈÏ£©
python3 run_all.py --all --paper        # ÂÛÎÄ¹æÄ£ R=1000
```

Smoke£º

```
python3 run_all.py --steps E1 E5 E6 E8 E9   # ÍêÈ«ÀëÏß£¬< 2 ·ÖÖÓ
```

---

## Êý¾Ý¹éµµÔ¼¶¨

- Ô­Ê¼ multi_proc_pmu stdout£º`data/raw/e2x/<bench>__<config>.txt`£¬**Ö»×·¼Ó£¬²»¸²¸Ç**¡£
- ´¦Àí¹ýµÄ CSV / JSON£º`data/processed/`¡£
- SVG Í¼£º`figs/`¡£
- ²½ÖèÈÕÖ¾£º`run_log/<step>__<UTC>.log`¡¢`run_log/manifest.jsonl`¡£

`run_log/manifest.jsonl` Ã¿ÐÐÒ»Ìõ `{event:"start",step,argv,inputs,...}` »ò `{event:"end",step,outputs,extra}`£¬¿ÉÓÃ£º

```
jq -c 'select(.event=="end")|{step,outputs:[.outputs[].path],extra}' \
    experiments/run_log/manifest.jsonl
```

À´¿ìËÙºË¶ÔÃ¿²½ÊÇ·ñÍê³É¡£
