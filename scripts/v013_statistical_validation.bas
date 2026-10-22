' drosofilaLive v0.13 - statistical validation regression
' Fast first test: 3 independent full-protocol replicates, 1 LIF trial per condition.

DRO.DURATION 1000
DRO.SUGAR 100
DRO.TRIALS 1
DRO.SEED 12345

DRO.CLEARWATCH
DRO.WATCH "DNge031"
DRO.WATCH "CB0393"
DRO.WATCH "CB0701"

DRO.REPLICATES 3
DRO.CLEARSTATS
DRO.STATS "DNge031", "CB0393"
DRO.RANK

' After validation, increase carefully, e.g.:
' DRO.TRIALS 5
' DRO.REPLICATES 20
' DRO.STATS "DNge031", "CB0393"
'
' Full 21-pair statistical scan is intentionally expensive:
' DRO.TRIALS 1
' DRO.REPLICATES 3
' DRO.CLEARSTATS
' DRO.STATSALL
' DRO.RANK
