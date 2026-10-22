# DROSOFILALIVE CHECKPOINT v0.18-r3

Release-maintenance checkpoint.

- Functional/scientific core: frozen at v0.17.
- v0.18-r2 dock-minimize fix retained.
- Release assets consolidated:
  - all documentation/help under `./help`
  - all runtime datasets under `./data`
- `./docs` and `./data_bin` are retired.
- Credits and licence paths: `help/credits.html`, `help/license.html`.
- Connectome binaries: `data/neurons.bin`, `data/edges.bin`, optional `data/annotations.tsv`.
- Existing `data/stimuli` and `data/flywire783_circuit` retained.
- Post-build copies the full `data` and `help` trees.
- No LIF, Discovery, Replay, Reproduce, Causal Surprise or QwtPlot3D algorithm changes.
