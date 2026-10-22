' drosofilaLive v0.11 - pairwise epistasis regression
' 1 trial keeps the first integration test quick. Use 5 or 20 for analysis.
DRO.DURATION 1000
DRO.SUGAR 100
DRO.TRIALS 1
DRO.SEED 12345

DRO.CLEARWATCH
DRO.WATCH "DNge031"
DRO.WATCH "CB0393"
DRO.WATCH "CB0701"

DRO.BATCHBEGIN
DRO.EPISTASIS "DNge031", "CB0393"
DRO.BATCHEND
