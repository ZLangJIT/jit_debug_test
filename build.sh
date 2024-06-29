set -v

which clang
which clang++
which clang-18
which clang++-18

clang++ --shared -fPIC jit.cpp -o jit.so -O0 -g3