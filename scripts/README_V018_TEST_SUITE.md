# drosofilaLive v0.18-r1 release/documentation verification

v0.18-r1 does not add a new scientific algorithm. Re-run the existing v0.17 scientific suite for scientific regression, then use this release-specific verifier after building:

```powershell
powershell -ExecutionPolicy Bypass -File .\work\scripts\verify_v018_release_docs.ps1
```

or pass the application directory explicitly:

```powershell
powershell -ExecutionPolicy Bypass -File .\work\scripts\verify_v018_release_docs.ps1 -AppDir D:\path\to\drosofilaLive\x64
```

The verifier checks:

- Italian operational HTML Help;
- Italian Foundations primer;
- English (UK) operational HTML Help;
- English (UK) Foundations primer;
- shared stylesheet;
- internal licence source HTML;
- data-source/credits page;
- FlyWire Consortium / FAFB acknowledgement tokens;
- key publication DOIs;
- CC BY-NC 4.0 data-licence notice;
- limitations sections;
- guided Experiment 07 / Causal Surprise coverage;
- Foundations coverage of Drosophila history, FAFB/FlyWire, root IDs, LIF, computational ablation and computational epistasis;
- operational-manual links back to the Foundations primers.

Scientific regression remains the responsibility of the frozen v0.17 suite (`v017_01_causal_surprise.bas` and `verify_v017_surprise.ps1`) plus a v0.16.1 REPRODUCE run.
