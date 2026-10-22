# drosofilaLive v0.17 - Causal Surprise test

`v017_01_causal_surprise.bas` is the functional-freeze smoke test for Graph vs Causality.

## Run

1. Start drosofilaLive v0.17 and load FlyWire v783.
2. Run `v017_01_causal_surprise.bas` from cvBasic.
3. The script performs Explorer, multi-seed screening, independent validation, Causal Surprise analysis, HTML export, `.dro v3` save/load and CSV export.
4. For this smoke test `DRO.EPIGATE "NONE"` is deliberate so every statistically valid validation row is available to the ranking. Guided scientific experiments should use `CI95`, `SIGN` or `BOTH` when appropriate.
5. From `work\scripts`, verify with:

```powershell
powershell -ExecutionPolicy Bypass -File .\verify_v017_surprise.ps1
```

## Expected artifacts

- `work\reports\v017_causal_surprise.html`
- `work\archives\v017_causal_surprise.dro`
- `work\results\v017_causal_surprise_causal_surprise.csv`

The verifier also opens the `.dro` ZIP and checks that optional `causal_surprise.json` exists, that `Surprise = causal percentile - structural percentile`, and that Surprise ranks are contiguous for gate-passed rows.
