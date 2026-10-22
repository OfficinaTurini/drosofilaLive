# Drosofila Live v0.18-r3 - Layout unification

This maintenance release removes two duplicate top-level asset roots.

Canonical release layout:

```
./help
    /it
    /en-GB
    credits.html
    license.html

./data
    neurons.bin
    edges.bin
    annotations.tsv        (optional)
    /stimuli
    /flywire783_circuit
```

Changes:
- `docs/credits.html` -> `help/credits.html`
- `docs/license.html` -> `help/license.html`
- binary connectome directory `data_bin` -> `data`
- Visual Studio post-build now copies the complete `data` tree in one operation
- Visual Studio post-build copies only the canonical `help` tree; there is no `docs` output
- default connectome path is now `<application>/data`
- UI label changed from `data_bin` to `data`
- source/runtime references to the retired roots were removed

The uploaded beta source archive did not contain the external help/data payload itself, so this source package updates the code/build layout and includes `tools/migrate_layout_v018_r3.ps1` to move those folders in a full working tree safely (no overwrite on collisions).

Scientific core is unchanged from v0.17/v0.18. QwtPlot3D lifecycle workaround is unchanged.
