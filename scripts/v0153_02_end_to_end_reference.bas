' drosofilaLive v0.15.3.2 - reference end-to-end scientific test
' Recommended first serious test.
' Total LIF simulations: 260
PRINT "=== v0.15.3.2 END-TO-END REFERENCE TEST ==="

DRO.PATHS

DRO.DURATION 1000
DRO.SUGAR 100
DRO.TRIALS 1
DRO.SEED 12345
DRO.CLEARWATCH
DRO.WATCH "TARGET"

' Structural candidate generation
DRO.EXPLORE "TARGET", "UPSTREAM", 2, 10, 100

' Causal single-ablation ranking
DRO.SCREEN 10

' Robustness of the three strongest causal hits
DRO.REPLICATES 20
DRO.VALIDATE 3

' Pairwise interactions among those validated hits
DRO.REPLICATES 10
DRO.EPISTATSTOP 3

' Hard internal invariants + human-readable ranking + machine-readable export
DRO.DISCOVERYCHECK
DRO.DISCOVERYRANK
DRO.DISCOVERYCSV "v0153_reference.csv"
PRINT "=== REFERENCE TEST COMPLETE ==="
