' drosofilaLive v0.17 - Graph vs Causality / Causal Surprise smoke + archive test
PRINT "=== v0.17 CAUSAL SURPRISE ==="

DRO.PATHS
DRO.DURATION 1000
DRO.SUGAR 100
DRO.TRIALS 1
DRO.CLEARWATCH
DRO.WATCH "TARGET"

' Structural ranking.
DRO.SEED 12345
DRO.EXPLORE "TARGET", "UPSTREAM", 2, 10, 30

' Coarse causal screen.
DRO.SCREEN 5, 2

' Independent validation block.
DRO.SEED 20000
DRO.REPLICATES 5
DRO.VALIDATE 3

' Smoke-test gate: rank every statistically valid validation row.
' Scientific guided experiments can replace NONE with CI95, SIGN or BOTH.
DRO.EPIGATE "NONE"

DRO.DISCOVERYCHECK
DRO.SURPRISE
DRO.SURPRISEHTML "v017_causal_surprise.html"

' Persist the analysis as optional causal_surprise.json inside .dro v3.
DRO.SAVE "v017_causal_surprise.dro"
DRO.LOAD "v017_causal_surprise.dro"
DRO.DISCOVERYCHECK
DRO.EXPORTCSV "v017_causal_surprise.csv"

PRINT "=== v0.17 CAUSAL SURPRISE COMPLETE ==="
