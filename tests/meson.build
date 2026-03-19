test_util = executable('test_util', 'test_util.c',
    include_directories: ['..', '../include'],
    link_with: faac_internal,
    dependencies: [ libm ]
)
test('util', test_util)

test_bitstream = executable('test_bitstream', 'test_bitstream.c',
    include_directories: ['..', '../include'],
    link_with: faac_internal,
    dependencies: [ libm ]
)
test('bitstream', test_bitstream)

test_fft = executable('test_fft', 'test_fft.c',
    include_directories: ['..', '../include'],
    link_with: faac_internal,
    dependencies: [ libm ]
)
test('fft', test_fft)

test_channels = executable('test_channels', 'test_channels.c',
    include_directories: ['..', '../include'],
    link_with: faac_internal,
    dependencies: [ libm ]
)
test('channels', test_channels)
