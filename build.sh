set -v

clang++ --shared -fPIC jit.cpp -o jit.so -O0 -g3