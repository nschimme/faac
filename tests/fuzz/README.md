# Fuzz harnesses

One libFuzzer-style harness per `fuzz_*.c`, linked against the project's own
static libraries.

- **Replay:** `meson test -C build --suite fuzz`. Without `-Dfuzzing-engine`
  `standalone_main.c` supplies `main()` and feeds the generated corpus
  through each harness, so this works with any compiler.
- **Fuzz:** needs clang with libFuzzer and instrumented libraries:

      CC=clang meson setup build -Db_sanitize=address,undefined \
          -Db_lundef=false -Db_lto=false --buildtype=plain \
          '-Dc_args=-fsanitize=fuzzer-no-link -g -O1' \
          -Dfuzzing-engine=-fsanitize=fuzzer

- **Long runs:** `tests/fuzz/run.sh <build-dir> <seconds> [target...]` keeps a
  corpus per target under `<build-dir>/fuzz`. On macOS, Homebrew LLVM provides
  libFuzzer.
- **Corpus:** `gen_corpus.py` writes the seeds into the build directory.
  Nothing binary is checked in; add a fixed crash's reproducer there as a
  byte literal.

CI (the fuzz-build job): every PR replays the corpus, fuzzes the default
build and the audio-only muxer build for 10 seconds per target, and uploads
any crash. Pushes to master run 60 seconds per target and save the corpus to
the Actions cache, which later runs restore.
