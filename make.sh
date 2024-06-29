sudo apk update
sudo apk upgrade
sudo apk add bash git lldb lldb-dev clang18 clang18-dev clang18-static llvm18 llvm18-dev llvm18-static llvm18-gtest cmake make libxml2 libxml2-dev libxml2-static curl

mkdir BUILD_DEBUG
mkdir ROOTFS_DEBUG
cmake -G "Unix Makefiles" -DCMAKE_INSTALL_PREFIX=ROOTFS_DEBUG -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_C_FLAGS="-g3 -O0" -DCMAKE_CXX_FLAGS="-g3 -O0" -DCMAKE_COLOR_DIAGNOSTICS=ON -DCMAKE_COLOR_MAKEFILE=ON -S . -B BUILD_DEBUG

cmake --build BUILD_DEBUG

cmake --install BUILD_DEBUG
