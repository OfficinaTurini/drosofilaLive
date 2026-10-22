# drosofilaLive v0.15.3.2 - cvBasic Discovery test suite

The official runtime copy of this folder is deployed automatically to:

`<applicationDir>/work/scripts`

Generated Discovery CSVs are written to:

`<applicationDir>/work/results`

The source-tree location no longer participates in runtime path resolution.

## Recommended execution order

1. `v0153_01_discovery_smoke.bas` - fast integration smoke test, 22 LIF simulations.
2. `v0153_07_legacy_regression.bas` - verifies old cvBasic experiment/statistics commands still work.
3. `v0153_04_reproducibility_same_seed.bas` - same-seed deterministic regression.
4. `v0153_06_direction_sanity.bas` - Explorer only, no LIF simulation.
5. `v0153_05_graph_parameter_sweep.bas` - graph-threshold sensitivity plus causal screen.
6. `v0153_08_seed_sensitivity.bas` - same graph candidates, five different LIF seeds; 100 simulations.
7. `v0153_02_end_to_end_reference.bas` - first serious full run, 260 LIF simulations.
8. `v0153_03_intensive_discovery.bas` - stress run, 720 LIF simulations with `TRIALS=1`.

Each v0.15.3.2 test begins with `DRO.PATHS`, so the cvBasic output records the exact runtime directories used by that run.

## Simulation-count formula

- `DRO.SCREEN N` -> `2*N` LIF simulations.
- `DRO.VALIDATE H` with `R` replicates -> `2*H*R` LIF simulations.
- `DRO.EPISTATSTOP E` with `R` replicates -> `4*R*C(E,2)` LIF simulations.

Reference run:

- screen Top 10 = 20 runs;
- validate Top 3 x 20 = 120 runs;
- epistasis Top 3 = 3 pairs x 10 x 4 = 120 runs;
- total = 260 LIF simulations.

## CSV output

`DRO.DISCOVERYCSV "name.csv"` now creates these files under `<work>/results` unless an absolute path is supplied:

- `name_explorer.csv`
- `name_hits.csv`
- `name_epistasis.csv`

## PowerShell acceptance checks

The verifier scripts use `$PSScriptRoot` and automatically look in `../results`, therefore their behavior is independent of PowerShell's current directory.

Run individually:

```powershell
powershell -ExecutionPolicy Bypass -File "<applicationDir>\work\scripts\verify_v0153_smoke.ps1"
powershell -ExecutionPolicy Bypass -File "<applicationDir>\work\scripts\verify_v0153_repro.ps1"
powershell -ExecutionPolicy Bypass -File "<applicationDir>\work\scripts\verify_v0153_reference.ps1"
```

After smoke, reproducibility and reference runs have all completed, run the combined acceptance gate:

```powershell
powershell -ExecutionPolicy Bypass -File "<applicationDir>\work\scripts\verify_v0153_all.ps1"
```

An explicit alternate results directory can still be supplied with `-Dir`.

## Automated invariants

`DRO.DISCOVERYCHECK` fails the cvBasic queue if any of these conditions is broken:

- Explorer root IDs are unique and graph score is finite/non-increasing;
- causal root IDs are unique and causal ranks are contiguous;
- causal ranking is non-increasing in `abs(TARGET delta Hz)`;
- fully validated hits contain exactly the requested number of replicates;
- validation means/std are finite, sign consistency is in `[0,1]`, and CI95 contains the mean;
- epistasis pair count is exactly `N*(N-1)/2`;
- valid interaction ranks are contiguous and non-increasing in `abs(mean interaction Hz)`;
- epistasis replicate counts and CI/sign-consistency fields are internally coherent.

No biological Delta-Hz value is hard-coded as an acceptance constant.
