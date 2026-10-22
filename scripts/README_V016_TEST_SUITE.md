# drosofilaLive v0.16.0 regression test

## Scientific archive round-trip

Run in drosofilaLive with the FlyWire v783 connectome loaded:

```text
v016_01_archive_discovery_roundtrip.bas
```

The script performs Explorer -> multi-seed Screen -> independent Validation -> pairwise Epistasis,
saves `.dro v3`, deliberately clears Discovery, reloads the archive and runs `DRO.DISCOVERYCHECK` again.
No LIF run is performed by `DRO.LOAD`.

Then run from PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\verify_v016_archive.ps1
```

The verifier opens the `.dro` as a standard ZIP and checks:

- `formatVersion == 3`;
- `explorer.json`, `discovery.json`, `provenance.json` exist;
- Explorer candidates/edges survived;
- Discovery screen/validation counts survived;
- the three independent phase seeds (12345 / 20000 / 30000) survived separately;
- archive CSV export produced Explorer, Discovery, Discovery-epistasis and provenance CSV files.

## Discovery archive replay regression (v0.16.0-r1)

After the round-trip archive exists, load the validated 7-neuron 3D circuit and run:

```text
v016_02_discovery_archive_replay.bas
```

Expected behavior:

- `DRO.LOAD` restores the Discovery-only v3 archive;
- Archive REPLAY resolves the best stored Discovery comparison;
- playback starts from stored frames only;
- the LIF engine remains idle.

The GUI Replay button is also enabled as soon as replayable stored frames exist. If the 3D circuit is not loaded, pressing it reports that requirement instead of remaining disabled.
