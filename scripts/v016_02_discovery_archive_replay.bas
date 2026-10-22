' drosofilaLive v0.16.0-r1 - replay a Discovery-only .dro without rerunning LIF
PRINT "=== v0.16-r1 DISCOVERY ARCHIVE REPLAY ==="
DRO.LOAD "v016_discovery_roundtrip.dro"
PRINT "Archive loaded. Replay should resolve the best stored Discovery comparison."
DRO.REPLAY
