
#if _WIN32
#include <stdio.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <libloaderapi.h>

#define ps(x) \
  printf( \
    "symbol address for %s is %p\n", \
    #x, GetProcAddress(GetModuleHandle(NULL), #x) \
  )

__declspec(dllexport) int main();

int main() {
  ps(_start);
  ps(main);
  ps(_main);
  ps(__main);
  ps(WinMain);
  ps(WinMainW);
  ps(llvm_orc_registerJITLoaderGDBWrapper);
  return 0;
}

#else
#error "unsupported platform"
#endif
