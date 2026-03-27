python = import('python').find_installation('python3')
test_script = files('test_encoder.py')

# Dependency for math functions
m_dep = cc.find_library('m', required : false)

# Global include directories from root
inc = include_directories(['..', '../include', '../libfaac'])

# API test - uses only public API
api_test = executable('api_test', 'test_api.c',
  include_directories: inc,
  link_with: libfaac,
  dependencies: [m_dep]
)
test('api_sanity', api_test)

# Encoder/Frontend test (requires ffmpeg-python)
test('encoder_sanity', python, args: [test_script, faac_exe.full_path()], timeout: 300)
