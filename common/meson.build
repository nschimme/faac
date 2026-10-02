# Source shared across libfaac, libfaad and the frontends.
#
# Nothing here is built as a library of its own. The headers are
# included where needed, and codec_dep's sources (the FFT engine and the
# ISO/IEC 14496-3 SBR/Huffman/SFB tables, non-copyrightable data) are
# compiled straight into each consumer so LTO and dead-code elimination drop
# whatever that library doesn't reference; this measures smaller than one
# shared copy even when both libraries are deployed.

common_inc = include_directories('.')

codec_dep = declare_dependency(
    include_directories: common_inc,
    sources: files(
        'fft.c',
        'sbr_tables.c',
        'huffdata.c',
        'sfb_tables.c',
    ),
)
