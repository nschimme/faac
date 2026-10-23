# FAAR verification

The library tests live in this directory and are registered by its local
`meson.build`. They cover the public streaming API, signal quality controls,
rate-matrix invariants, DFT filtering, prefilter setup, and the real FFT.
`frontend/tests/test_faar_cli.py` covers command-line and file-format behavior.
The public API is also exercised by `tests/fuzz/fuzz_faar.c` through the
repository fuzz runner.

Run the production checks from a configured build directory:

```sh
meson test -C build
```

The fuzz target can be built and run with:

```sh
tests/fuzz/run.sh /tmp/faac-fuzz 10 fuzz_faar
```
