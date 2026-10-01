# Meson build file for libfaab: the FFT engine, SBR/Huffman tables and SFB
# tables shared verbatim between libfaac and libfaad (ISO/IEC 14496-3
# non-copyrightable data/algorithms), plus the portable one-shot-init header
# both use for their own lazy global-table setup.
#
# These are compiled straight into each consumer rather than built as a
# library of their own: that lets LTO and dead-code elimination drop whatever
# a given library doesn't reference, and measures smaller than one shared
# copy even when both libraries are deployed.

faab_dep = declare_dependency(
    include_directories: include_directories('.'),
    sources: files(
        'fft.c',
        'sbr_tables.c',
        'huffdata.c',
        'sfb_tables.c',
    ),
)
