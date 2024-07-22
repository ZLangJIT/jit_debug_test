#include <stdio.h>

#if _WIN32

#include <libloaderapi.h>

#define ps(x) \
  printf( \
    "symbol address for %s is %p\n", \
    #x, GetProcAddress(GetModuleHandle(NULL)), #x \
  )

#else
#error "unsupported platform"
#endif

int main() {
  ps(main);
  ps(_main);
  ps(__main);
  ps(WinMain);
  ps(WinMainW);
  ps(llvm_orc_registerJITLoaderGDBWrapper);
  return 0;
}
