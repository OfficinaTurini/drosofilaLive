' drosofilaLive v0.11 - all 21 unordered causal-circuit pairs
' WARNING: DRO.EPISTASISALL runs four LIF conditions for every pair.
' Start with 1 trial; raise to 5 or 20 only after validating runtime and outputs.
DRO.DURATION 1000
DRO.SUGAR 100
DRO.TRIALS 1
DRO.SEED 12345

DRO.CLEARWATCH
DRO.WATCH "CB0701"

DRO.BATCHBEGIN
DRO.EPISTASISALL
DRO.BATCHEND
