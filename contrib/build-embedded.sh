#!/bin/sh
# Configure a flash-constrained build: drops libfaam's tag/chapter retrofit
# APIs and builds libfaac/libfaad as shared objects.
exec meson setup "${1:-buildcam}" -Dembedded=true -Ddefault_library=shared
