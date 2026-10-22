# drosofilaLive v0.15.4 test suite

Run scripts from the Script tab. v0.15.3.2+ copies `.bas` and `.ps1` files to
`<applicationDir>/work/scripts` after a Visual Studio build.

All relative CSV outputs go to `work/results`; HTML reports go to `work/reports`.

## 01 - multi-seed smoke

```text
v0154_01_multiseed_smoke.bas
```

Purpose:
- basic integration of `DRO.SCREEN N, replicates`;
- six candidates, three screen seeds each;
- short validation and CSV/HTML export.

After it finishes:

```powershell
powershell -ExecutionPolicy Bypass -File "<applicationDir>\work\scripts\verify_v0154_multiseed.ps1"
```

## 02 - layered depth-3 selection

```text
v0154_02_layered_depth3.bas
```

Purpose:
- exercise `DRO.SCREENLAYERED 10, 3` at Explorer depth 3;
- verify that multiple depth layers survive candidate selection;
- regression check that CB3892b (`720575940631340815`) is not lost merely because depth-3
  candidates are added.

After it finishes:

```powershell
powershell -ExecutionPolicy Bypass -File "<applicationDir>\work\scripts\verify_v0154_layered.ps1"
```

## 03 - confirmatory CI gate pipeline

```text
v0154_03_confirmed_gate_pipeline.bas
```

This is deliberately a long scientific regression. It reproduces the historical candidate-selection
protocol, confirms the top three using 100 independent seeds, enables `DRO.EPIGATE "CI95"`, and
runs 100-replicate automatic epistasis on the two hits expected to survive the gate.

Known regression targets from the preceding independent campaign:
- DNge062 root `720575940627099338`;
- CB3892b root `720575940631340815`.

The verifier checks structure and gate behavior; it does not hard-code a required biological Delta.

```powershell
powershell -ExecutionPolicy Bypass -File "<applicationDir>\work\scripts\verify_v0154_gate_pipeline.ps1"
```

## Combined verifier

After all three cvBasic scripts have completed:

```powershell
powershell -ExecutionPolicy Bypass -File "<applicationDir>\work\scripts\verify_v0154_all.ps1"
```

The combined verifier only checks already-generated results; it does not launch simulations.
