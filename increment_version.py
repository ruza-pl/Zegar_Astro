import os
Import("env")

version_file = 'build_number.txt'
build_no = 0

try:
    with open(version_file, 'r') as f:
        build_no = int(f.read().strip())
except:
    build_no = 0

build_no += 1

with open(version_file, 'w') as f:
    f.write(str(build_no))

# Przekaż numer kompilacji do kodu C++ jako makro
env.Append(CPPDEFINES=[("BUILD_NUMBER", build_no)])