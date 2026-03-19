python = import('python').find_installation('python3')
test_script = files('test_encoder.py')

test_types = ['sine', 'sweep', 'silence', 'noise']

foreach t : test_types
    test('encoder_' + t, python, args: [test_script, faac_exe.full_path(), t], timeout: 60)
endforeach
