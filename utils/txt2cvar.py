#!/usr/bin/env python3

from __future__ import print_function
import sys, os

if len(sys.argv) < 2:
    print('Usage:', sys.argv[0], '<file_path> [var_name]')
    exit(1)

f = open(sys.argv[1])
lines = f.readlines()
var_name = os.path.basename(sys.argv[1])

if len(sys.argv) >= 3:
    var_name = sys.argv[2]

output = 'const char ' + var_name + '[] = {\n'
p = 0
for l in lines:
    for c in l:
        output += '0x%02X,' % ord(c)
        p += 1
        if not p % 16:
            output += '\n'

output += '0x00\n};'
print(output)
