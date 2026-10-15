# Fixed transient border 8 validation

The fixed-8 candidate retains the existing VARFIX transient detector and two-envelope layout, while placing the inner border at half of the SBR time-slot range (8 with the default 16 slots) for each detected transient. The original time-varying border placed the split at 2, 4, 6, or 8 according to the transient position.

`validation.json` contains row-level commands, encoded AAC and FDK-decoded PCM paths/hashes, actual payload rates, profile checks, exact fixed8 reference matches, and strict FAAD/FFmpeg results. `provenance.json` records source/build/clip/decoder hashes. The `.patch` files preserve the uncommitted source delta for both standalone upstream and combined PR68 branches.

The standalone run covers 49 clips × 4 rates (196 rows) and matched its frozen fixed8 full-corpus reference on packet payload and FDK PCM for all rows. The combined PR68 extension covers the four gate clips × 4 rates (16 rows), with stack reference rows at 32 kbps and all-stack rows at 48/64/96; static MOS scoring is pending.

Strict decoder results: 212/212 FAAD and 212/212 FFmpeg candidate streams passed. No MOS is claimed for the 16 combined rows until the scoring job completes. Native timing and matched GCC-13/LTO footprint are pending.
