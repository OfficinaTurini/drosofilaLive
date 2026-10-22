' drosofilaLive v0.15.4 - full gate pipeline on the previously confirmed region
' Expected from v0.15.3 evidence: DNge062 and CB3892b should survive 100-replicate validation.
PRINT "=== v0.15.4 CONFIRMED GATE PIPELINE ==="

DRO.PATHS
DRO.DURATION 1000
DRO.SUGAR 100
DRO.TRIALS 1
DRO.CLEARWATCH
DRO.WATCH "TARGET"

' Candidate selection block - historical deterministic screen
DRO.SEED 77777
DRO.EXPLORE "TARGET", "UPSTREAM", 2, 20, 100
DRO.SCREEN 10

' Independent confirmatory validation block
DRO.SEED 1000000
DRO.REPLICATES 100
DRO.VALIDATE 3

' Scientific admission gate before pairwise epistasis
DRO.EPIGATE "CI95"

' Third independent seed block for epistasis
DRO.SEED 2000000
DRO.REPLICATES 100
DRO.EPISTATSTOP 2

DRO.DISCOVERYCHECK
DRO.DISCOVERYRANK
DRO.DISCOVERYCSV "v0154_confirmed_gate_pipeline.csv"
DRO.DISCOVERYHTML "v0154_confirmed_gate_pipeline.html"

PRINT "=== CONFIRMED GATE PIPELINE COMPLETE ==="
