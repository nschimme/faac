#!/bin/bash
# Default values
export COVERAGE=${COVERAGE:-10}
export MESON_FLAGS=${MESON_FLAGS:-"-Dfloating-point=single"}

echo "--- Running FAAC Benchmark ---"
echo "Flags: $MESON_FLAGS"
echo "Coverage: $COVERAGE%"

podman build -t faac-benchmark-ready . --platform linux/amd64

podman run -it --rm \
    --platform linux/amd64 \
    -v "$(pwd)/..":/app:Z \
    -e COVERAGE="$COVERAGE" \
    -e MESON_FLAGS="$MESON_FLAGS" \
    faac-benchmark-ready
