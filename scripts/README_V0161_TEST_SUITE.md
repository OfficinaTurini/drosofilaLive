# v0.16.1 REPRODUCE test

Use the `.dro` produced by `v016_01_archive_discovery_roundtrip.bas` (no need to run the long source experiment again if that archive already exists).

1. Start drosofilaLive v0.16.1.
2. Load the FlyWire v783 connectome.
3. Open the existing `.dro v3` in Archive.
4. Press **Preflight**.
   - Expected on the original machine/data: `READY FOR EXACT REPRODUCTION`.
5. Press **Reproduce**.
   - Explorer is rebuilt first.
   - Screening, validation and epistasis are re-run with archived seeds/replicate counts/root sets.
6. Expected final status: `REPRODUCIBILITY: PASS`.
7. Press **Open report** and inspect the preflight and metric tables.
8. Optional shell verification from `work\scripts`:

```powershell
powershell -ExecutionPolicy Bypass -File .\verify_v0161_reproduce_report.ps1
```

Negative test: change/replace the local connectome declaration or use an archive from a different environment. Preflight must report `NON-EXACT ENVIRONMENT`; if the embedded stimulus exists, REPRODUCE should still use the archive copy of the stimulus.

Note: v0.16.1 REPRODUCE is intentionally driven from the Archive GUI. No asynchronous `DRO.REPRODUCE` cvBasic command is added in this release; this avoids mixing the script queue state machine with the scientific reproduction state machine before v0.17 freeze.
