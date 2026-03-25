import re

with open('libfaac/huff2.c', 'r') as f:
    content = f.read()

# Replace my previous long patterns with uabs_int(x)
pattern = r'\(\(unsigned int\)\(([^ ]+) < 0 \? -\(unsigned int\)\(\1\) : \(unsigned int\)\(\1\)\)\)'

new_content = re.sub(pattern, r'uabs_int(\1)', content)

with open('libfaac/huff2.c', 'w') as f:
    f.write(new_content)
