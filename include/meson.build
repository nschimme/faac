fs = import('fs')
version_parts = library_abi_version.split('.')
assert(version_parts.length() == 3, 'library_abi_version must be MAJOR.MINOR.PATCH')
version_fields = ['MAJOR', 'MINOR', 'PATCH']
version_headers = {
    'FAAC': 'faac.h',
    'FAAD': 'faad3.h',
    'FAAM': 'faam.h',
    'FAAR': 'faar.h',
}
foreach library, header: version_headers
    header_text = fs.read(header)
    foreach index: range(3)
        expected = '#define ' + library + '_VERSION_' + version_fields[index] + ' ' + version_parts[index]
        assert(header_text.contains(expected), header + ' must define ' + expected)
    endforeach
endforeach

headers = ['faam.h', 'faar.h']
if get_option('encoder')
    headers += ['faac.h']
endif
if get_option('decoder')
    headers += ['faad3.h']
endif
install_headers(headers)
